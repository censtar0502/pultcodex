#include "trk_probe.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "usart.h"

#define TRK_PROBE_RX_BUF_SIZE      64U
#define TRK_PROBE_POLL_MS          300UL
#define TRK_PROBE_TIMEOUT_MS       60UL
#define TRK_PROBE_FRAME_LEN        5U

typedef struct
{
  UART_HandleTypeDef *huart;
  uint8_t trk_id;
  uint8_t addr_hi;
  uint8_t addr_lo;
  uint8_t rx_buf[TRK_PROBE_RX_BUF_SIZE];
  uint16_t rx_prev_pos;
  uint8_t frame_buf[8];
  uint8_t frame_len;
  uint8_t tx_buf[TRK_PROBE_FRAME_LEN];
  uint32_t last_poll_tick;
  uint32_t last_tx_tick;
  TrkProbeChannelStatus status;
} TrkProbeChannel;

static TrkProbeChannel trk_channels[2];
static TrkProbeStatus trk_probe_status;

static void TrkProbe_HandleStatusResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);

static uint8_t TrkProbe_CalcXor(const uint8_t *data, uint16_t len)
{
  uint16_t i;
  uint8_t crc = 0U;

  for (i = 0U; i < len; ++i)
  {
    crc ^= data[i];
  }

  return crc;
}

static void TrkProbe_SetAscii(TrkProbeChannelStatus *status, const uint8_t *data, uint16_t len)
{
  uint16_t copy_len;
  uint16_t i;

  if (status == NULL)
  {
    return;
  }

  copy_len = (len < (uint16_t)(sizeof(status->last_ascii) - 1U)) ? len : (uint16_t)(sizeof(status->last_ascii) - 1U);
  for (i = 0U; i < copy_len; ++i)
  {
    uint8_t ch = data[i];
    status->last_ascii[i] = ((ch >= 32U) && (ch <= 126U)) ? (char)ch : '.';
  }

  status->last_ascii[copy_len] = '\0';
}

static void TrkProbe_FeedByte(TrkProbeChannel *channel, uint8_t value)
{
  if (channel == NULL)
  {
    return;
  }

  if ((channel->frame_len == 0U) && (value != 0x02U))
  {
    return;
  }

  if (channel->frame_len >= sizeof(channel->frame_buf))
  {
    channel->frame_len = 0U;
  }

  channel->frame_buf[channel->frame_len++] = value;
  if (channel->frame_len == 7U)
  {
    TrkProbe_SetAscii(&channel->status, channel->frame_buf, channel->frame_len);
    TrkProbe_HandleStatusResponse(channel, channel->frame_buf, channel->frame_len);
    channel->frame_len = 0U;
  }
}

static void TrkProbe_ProcessChunk(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  uint16_t i;

  if ((channel == NULL) || (data == NULL) || (len == 0U))
  {
    return;
  }

  ++channel->status.rx_count;
  channel->status.last_rx_len = len;
  channel->status.last_rx_tick = HAL_GetTick();
  AppLog_ProtoFrame(channel->trk_id, APP_LOG_PROTO_DIR_RX, data, len);

  for (i = 0U; i < len; ++i)
  {
    TrkProbe_FeedByte(channel, data[i]);
  }
}

static void TrkProbe_StartRx(TrkProbeChannel *channel)
{
  if ((channel == NULL) || (channel->huart == NULL))
  {
    return;
  }

  if (HAL_UARTEx_ReceiveToIdle_DMA(channel->huart, channel->rx_buf, sizeof(channel->rx_buf)) != HAL_OK)
  {
    ++channel->status.timeout_count;
    AppLog_Message(APP_LOG_LEVEL_ERROR, "TRK", "RX DMA start failed");
    return;
  }

  __HAL_DMA_DISABLE_IT(channel->huart->hdmarx, DMA_IT_HT);
}

static TrkProbeChannel *TrkProbe_FindByHandle(UART_HandleTypeDef *huart)
{
  uint32_t i;

  for (i = 0U; i < 2U; ++i)
  {
    if (trk_channels[i].huart == huart)
    {
      return &trk_channels[i];
    }
  }

  return NULL;
}

static TrkProbeChannelStatus *TrkProbe_PublicStatus(uint8_t trk_id)
{
  return (trk_id == 1U) ? &trk_probe_status.trk1 : &trk_probe_status.trk2;
}

static void TrkProbe_SyncPublicStatus(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  *TrkProbe_PublicStatus(channel->trk_id) = channel->status;
}

static void TrkProbe_SendPoll(TrkProbeChannel *channel)
{
  if ((channel == NULL) || (channel->huart == NULL))
  {
    return;
  }

  channel->tx_buf[0] = 0x02U;
  channel->tx_buf[1] = channel->addr_hi;
  channel->tx_buf[2] = channel->addr_lo;
  channel->tx_buf[3] = (uint8_t)'S';
  channel->tx_buf[4] = TrkProbe_CalcXor(&channel->tx_buf[1], 3U);

  if (HAL_UART_Transmit_DMA(channel->huart, channel->tx_buf, sizeof(channel->tx_buf)) != HAL_OK)
  {
    ++channel->status.timeout_count;
    AppLog_Message(APP_LOG_LEVEL_ERROR, "TRK", "TX DMA start failed");
    TrkProbe_SyncPublicStatus(channel);
    return;
  }

  channel->status.tx_busy = 1U;
  channel->status.waiting_reply = 1U;
  channel->last_tx_tick = HAL_GetTick();
  channel->last_poll_tick = channel->last_tx_tick;
  ++channel->status.tx_count;
  AppLog_ProtoFrame(channel->trk_id, APP_LOG_PROTO_DIR_TX, channel->tx_buf, sizeof(channel->tx_buf));
  TrkProbe_SyncPublicStatus(channel);
}

static void TrkProbe_HandleStatusResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  uint8_t crc_expected;

  if ((channel == NULL) || (data == NULL) || (len < 7U))
  {
    if (channel != NULL)
    {
      ++channel->status.crc_error_count;
    }
    return;
  }

  crc_expected = TrkProbe_CalcXor(&data[1], (uint16_t)(len - 2U));
  if (crc_expected != data[len - 1U])
  {
    ++channel->status.crc_error_count;
    channel->status.online = 0U;
    return;
  }

  if ((data[0] != 0x02U) || (data[1] != channel->addr_hi) || (data[2] != channel->addr_lo) || (data[3] != (uint8_t)'S'))
  {
    ++channel->status.crc_error_count;
    channel->status.online = 0U;
    return;
  }

  channel->status.last_status = data[4];
  channel->status.last_nozzle = data[5];
  channel->status.online = 1U;
  channel->status.waiting_reply = 0U;
  ++channel->status.ok_count;
}

void TrkProbe_Init(void)
{
  memset(&trk_channels, 0, sizeof(trk_channels));
  memset(&trk_probe_status, 0, sizeof(trk_probe_status));

  trk_channels[0].huart = &huart3;
  trk_channels[0].trk_id = 1U;
  trk_channels[0].addr_hi = 0x00U;
  trk_channels[0].addr_lo = 0x01U;
  strcpy(trk_channels[0].status.last_ascii, "-");

  trk_channels[1].huart = &huart1;
  trk_channels[1].trk_id = 2U;
  trk_channels[1].addr_hi = 0x00U;
  trk_channels[1].addr_lo = 0x02U;
  strcpy(trk_channels[1].status.last_ascii, "-");

  TrkProbe_StartRx(&trk_channels[0]);
  TrkProbe_StartRx(&trk_channels[1]);
  TrkProbe_SyncPublicStatus(&trk_channels[0]);
  TrkProbe_SyncPublicStatus(&trk_channels[1]);
  AppLog_Message(APP_LOG_LEVEL_INFO, "TRK", "GasKitLink probe ready, poll=300ms, timeout=60ms");
}

void TrkProbe_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t i;

  for (i = 0U; i < 2U; ++i)
  {
    TrkProbeChannel *channel = &trk_channels[i];

    if ((channel->status.waiting_reply != 0U) &&
        ((now - channel->last_tx_tick) >= TRK_PROBE_TIMEOUT_MS))
    {
      channel->status.waiting_reply = 0U;
      channel->status.online = 0U;
      ++channel->status.timeout_count;
      AppLog_Message(APP_LOG_LEVEL_WARN,
                     (channel->trk_id == 1U) ? "TRK1" : "TRK2",
                     "status poll timeout");
      TrkProbe_SyncPublicStatus(channel);
    }

    if ((channel->status.tx_busy == 0U) &&
        (channel->status.waiting_reply == 0U) &&
        ((now - channel->last_poll_tick) >= TRK_PROBE_POLL_MS))
    {
      TrkProbe_SendPoll(channel);
    }
  }
}

void TrkProbe_OnRxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  TrkProbeChannel *channel = TrkProbe_FindByHandle(huart);
  uint16_t new_pos;

  if ((channel == NULL) || (size == 0U))
  {
    return;
  }

  new_pos = size;
  if (new_pos > TRK_PROBE_RX_BUF_SIZE)
  {
    new_pos = TRK_PROBE_RX_BUF_SIZE;
  }

  if (new_pos > channel->rx_prev_pos)
  {
    TrkProbe_ProcessChunk(channel,
                          &channel->rx_buf[channel->rx_prev_pos],
                          (uint16_t)(new_pos - channel->rx_prev_pos));
  }
  else if (new_pos < channel->rx_prev_pos)
  {
    TrkProbe_ProcessChunk(channel,
                          &channel->rx_buf[channel->rx_prev_pos],
                          (uint16_t)(TRK_PROBE_RX_BUF_SIZE - channel->rx_prev_pos));
    if (new_pos > 0U)
    {
      TrkProbe_ProcessChunk(channel, channel->rx_buf, new_pos);
    }
  }

  channel->rx_prev_pos = new_pos;
  TrkProbe_SyncPublicStatus(channel);
}

void TrkProbe_OnTxCplt(UART_HandleTypeDef *huart)
{
  TrkProbeChannel *channel = TrkProbe_FindByHandle(huart);

  if (channel == NULL)
  {
    return;
  }

  channel->status.tx_busy = 0U;
  TrkProbe_SyncPublicStatus(channel);
}

const TrkProbeStatus *TrkProbe_GetStatus(void)
{
  return &trk_probe_status;
}
