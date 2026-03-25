#include "trk_probe.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "eeprom_at24.h"
#include "usart.h"

#define TRK_PROBE_RX_BUF_SIZE      64U
#define TRK_PROBE_POLL_MS          300UL
#define TRK_PROBE_TIMEOUT_MS       150UL
#define TRK_PROBE_FRAME_LEN        18U
#define TRK_PROBE_MASK_TRK1        0x01U
#define TRK_PROBE_MASK_TRK2        0x02U
#define TRK_PROBE_MASK_TRK3        0x04U
#define TRK_PROBE_MASK_ALL         0x07U

#define TRK_PROBE_NUM_CHANNELS     2U
#define TRK_PROBE_MENU_ITEMS       4U
#define TRK_PROBE_OFFLINE_AFTER_MISSES 5U
#define TRK_PROBE_PRESET_MONEY_MAX 999999UL
#define TRK_PROBE_PRESET_VOLUME_CL_MAX 90000UL
#define TRK_PROBE_NV_MAGIC         0x54524B50UL
#define TRK_PROBE_NV_VERSION       2U
#define TRK_PROBE_NV_ADDR          0x0000U
#define TRK_PROBE_NV_TIMEOUT_MS    20U

/* TRK1=USART3(PD8/PD9), TRK2=USART1(PB6/PB7). */
#define TRK_PROBE_ACTIVE_MASK      (TRK_PROBE_MASK_TRK1 | TRK_PROBE_MASK_TRK2)

typedef struct
{
  UART_HandleTypeDef *huart;
  uint8_t trk_id;
  uint8_t addr_hi;
  uint8_t addr_lo;
  uint8_t rx_buf[TRK_PROBE_RX_BUF_SIZE];
  uint8_t frame_buf[8];
  uint8_t frame_len;
  uint8_t tx_buf[TRK_PROBE_FRAME_LEN];
  uint8_t comm_fail_streak;
  uint8_t live_poll_phase;
  uint8_t final_request_sent;
  uint8_t close_request_sent;
  uint32_t last_poll_tick;
  uint32_t last_tx_tick;
  TrkProbeChannelStatus status;
} TrkProbeChannel;

typedef struct
{
  uint8_t enabled;
  uint8_t dispense_mode;
  uint8_t reserved0;
  uint8_t reserved1;
  char price_text[6];
} TrkProbeNvChannel;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  TrkProbeNvChannel channels[TRK_PROBE_NUM_CHANNELS];
  uint32_t crc32;
} TrkProbeNvConfig;

static TrkProbeChannel trk_channels[TRK_PROBE_NUM_CHANNELS];
static TrkProbeStatus trk_probe_status;

static void TrkProbe_HandleStatusResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);
static uint8_t TrkProbe_HasPendingRxData(const TrkProbeChannel *channel);
static void TrkProbe_UpdateStateFromStatus(TrkProbeChannel *channel, uint8_t status_raw);
static void TrkProbe_RefreshUiFlags(void);
static void TrkProbe_SyncPublicStatus(TrkProbeChannel *channel);
static void TrkProbe_SetPriceValue(TrkProbeChannel *channel, uint32_t price_raw, const char *price_text);
static uint8_t TrkProbe_ParsePriceText(const char *src, uint32_t *price_raw_out);
static uint8_t TrkProbe_ParsePriceEditValue(const TrkProbeChannel *channel, uint32_t *price_raw_out);
static TrkProbeChannel *TrkProbe_GetActiveUiChannel(void);
static TrkProbeChannel *TrkProbe_GetChannelByTrkId(uint8_t trk_id);
static void TrkProbe_LogPrice(uint8_t trk_id, uint32_t price);
static uint8_t TrkProbe_IsEnabled(const TrkProbeChannel *channel);
static uint8_t TrkProbe_CanSelectTrk(uint8_t trk_id);
static void TrkProbe_SelectTrk(uint8_t trk_id);
static void TrkProbe_ExitToMain(void);
static void TrkProbe_SetNotice(const char *text, uint32_t duration_ms);
static void TrkProbe_CycleDispenseMode(TrkProbeChannel *channel);
static uint8_t TrkProbe_SendFrame(TrkProbeChannel *channel, const uint8_t *payload, uint16_t payload_len);
static uint8_t TrkProbe_StartTransaction(TrkProbeChannel *channel);
static uint8_t TrkProbe_SendSimpleCommand(TrkProbeChannel *channel, char command);
static void TrkProbe_ClearPreset(TrkProbeChannel *channel);
static void TrkProbe_UpdateFullTankPreset(TrkProbeChannel *channel);
static uint8_t TrkProbe_ParseMoneyPresetText(const char *src, uint32_t *money_out);
static uint8_t TrkProbe_ParseVolumePresetText(const char *src, uint32_t *volume_cl_out);
static uint8_t TrkProbe_UpdatePresetFromBuffer(TrkProbeChannel *channel);
static void TrkProbe_RecalculatePreset(TrkProbeChannel *channel);
static void TrkProbe_ResetTransactionRuntime(TrkProbeChannel *channel);
static void TrkProbe_ParseLiveVolumeResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);
static void TrkProbe_ParseLiveMoneyResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);
static void TrkProbe_ParseFinalResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);
static void TrkProbe_HandleRxFrame(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);
static uint32_t TrkProbe_Crc32(const void *data, uint32_t len);
static void TrkProbe_RegisterCommSuccess(TrkProbeChannel *channel);
static void TrkProbe_RegisterCommFailure(TrkProbeChannel *channel);
static void TrkProbe_LoadDefaults(void);
static void TrkProbe_ApplyNvConfig(const TrkProbeNvConfig *config);
static uint8_t TrkProbe_LoadNvConfig(void);
static uint8_t TrkProbe_SaveNvConfig(void);

static uint8_t TrkProbe_IsActive(uint8_t trk_id)
{
  if (trk_id == 1U)
  {
    return ((TRK_PROBE_ACTIVE_MASK & TRK_PROBE_MASK_TRK1) != 0U) ? 1U : 0U;
  }

  if (trk_id == 2U)
  {
    return ((TRK_PROBE_ACTIVE_MASK & TRK_PROBE_MASK_TRK2) != 0U) ? 1U : 0U;
  }

  if (trk_id == 3U)
  {
    return ((TRK_PROBE_ACTIVE_MASK & TRK_PROBE_MASK_TRK3) != 0U) ? 1U : 0U;
  }

  return 0U;
}

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

static void TrkProbe_SetPriceValue(TrkProbeChannel *channel, uint32_t price_raw, const char *price_text)
{
  char safe_price_text[6];

  if (channel == NULL)
  {
    return;
  }

  (void)snprintf(safe_price_text,
                 sizeof(safe_price_text),
                 "%s",
                 (price_text != NULL) ? price_text : "0");

  channel->status.price = price_raw;
  channel->status.price_edit_buf[0] = '\0';
  (void)snprintf(channel->status.price_text,
                 sizeof(channel->status.price_text),
                 "%s",
                 safe_price_text);
}

static void TrkProbe_ClearPreset(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->status.preset_money = 0U;
  channel->status.preset_volume_cl = 0U;
  channel->status.preset_edit_buf[0] = '\0';
}

static void TrkProbe_ResetTransactionRuntime(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->live_poll_phase = 0U;
  channel->final_request_sent = 0U;
  channel->close_request_sent = 0U;
  channel->status.transaction_pending = 0U;
  channel->status.final_data_ready = 0U;
  channel->status.transaction_id = '\0';
  channel->status.live_money = 0U;
  channel->status.live_volume_cl = 0U;
  channel->status.final_money = 0U;
  channel->status.final_volume_cl = 0U;
}

static uint32_t TrkProbe_Crc32(const void *data, uint32_t len)
{
  const uint8_t *src = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t i;
  uint8_t bit;

  if (data == NULL)
  {
    return 0U;
  }

  for (i = 0U; i < len; ++i)
  {
    crc ^= src[i];
    for (bit = 0U; bit < 8U; ++bit)
    {
      if ((crc & 1UL) != 0U)
      {
        crc = (crc >> 1U) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return ~crc;
}

static uint8_t TrkProbe_ParsePriceText(const char *src, uint32_t *price_raw_out)
{
  uint32_t price_raw = 0U;
  uint8_t seen_dot = 0U;
  uint8_t digits_total = 0U;
  uint8_t digits_before_dot = 0U;
  uint8_t digits_after_dot = 0U;

  if ((src == NULL) || (price_raw_out == NULL))
  {
    return 0U;
  }

  while (*src != '\0')
  {
    if ((*src >= '0') && (*src <= '9'))
    {
      if (digits_total >= 4U)
      {
        return 0U;
      }

      price_raw = (price_raw * 10UL) + (uint32_t)(*src - '0');
      ++digits_total;

      if (seen_dot == 0U)
      {
        ++digits_before_dot;
      }
      else
      {
        ++digits_after_dot;
      }
    }
    else if (*src == '.')
    {
      if (seen_dot != 0U)
      {
        return 0U;
      }
      seen_dot = 1U;
    }
    else
    {
      return 0U;
    }

    ++src;
  }

  if (digits_before_dot == 0U)
  {
    return 0U;
  }

  if ((seen_dot != 0U) && ((digits_after_dot == 0U) || (digits_after_dot > 2U)))
  {
    return 0U;
  }

  if ((price_raw == 0UL) || (price_raw > 9999UL))
  {
    return 0U;
  }

  *price_raw_out = price_raw;
  return 1U;
}

static uint8_t TrkProbe_ParsePriceEditValue(const TrkProbeChannel *channel, uint32_t *price_raw_out)
{
  if ((channel == NULL) || (price_raw_out == NULL))
  {
    return 0U;
  }

  return TrkProbe_ParsePriceText(channel->status.price_edit_buf, price_raw_out);
}

static uint8_t TrkProbe_ParseMoneyPresetText(const char *src, uint32_t *money_out)
{
  uint32_t money = 0U;

  if ((src == NULL) || (money_out == NULL))
  {
    return 0U;
  }

  if (*src == '\0')
  {
    *money_out = 0U;
    return 1U;
  }

  while (*src != '\0')
  {
    if ((*src < '0') || (*src > '9'))
    {
      return 0U;
    }

    money = (money * 10UL) + (uint32_t)(*src - '0');
    if (money > TRK_PROBE_PRESET_MONEY_MAX)
    {
      return 0U;
    }

    ++src;
  }

  *money_out = money;
  return 1U;
}

static uint8_t TrkProbe_ParseVolumePresetText(const char *src, uint32_t *volume_cl_out)
{
  uint32_t liters = 0U;
  uint32_t frac = 0U;
  uint8_t seen_dot = 0U;
  uint8_t frac_digits = 0U;

  if ((src == NULL) || (volume_cl_out == NULL))
  {
    return 0U;
  }

  if (*src == '\0')
  {
    *volume_cl_out = 0U;
    return 1U;
  }

  while (*src != '\0')
  {
    if ((*src >= '0') && (*src <= '9'))
    {
      if (seen_dot == 0U)
      {
        liters = (liters * 10UL) + (uint32_t)(*src - '0');
        if (liters > 900UL)
        {
          return 0U;
        }
      }
      else
      {
        if (frac_digits >= 2U)
        {
          return 0U;
        }

        frac = (frac * 10UL) + (uint32_t)(*src - '0');
        ++frac_digits;
      }
    }
    else if (*src == '.')
    {
      if (seen_dot != 0U)
      {
        return 0U;
      }

      seen_dot = 1U;
    }
    else
    {
      return 0U;
    }

    ++src;
  }

  if ((seen_dot != 0U) && (frac_digits == 0U))
  {
    return 0U;
  }

  if (frac_digits == 1U)
  {
    frac *= 10UL;
  }

  *volume_cl_out = (liters * 100UL) + frac;
  if (*volume_cl_out > TRK_PROBE_PRESET_VOLUME_CL_MAX)
  {
    return 0U;
  }

  return 1U;
}

static void TrkProbe_UpdateFullTankPreset(TrkProbeChannel *channel)
{
  uint32_t volume_cl;

  if (channel == NULL)
  {
    return;
  }

  channel->status.preset_edit_buf[0] = '\0';
  channel->status.preset_money = TRK_PROBE_PRESET_MONEY_MAX;

  if (channel->status.price == 0U)
  {
    channel->status.preset_volume_cl = 0U;
    return;
  }

  volume_cl = (TRK_PROBE_PRESET_MONEY_MAX * 100UL) / channel->status.price;
  if (volume_cl > TRK_PROBE_PRESET_VOLUME_CL_MAX)
  {
    volume_cl = TRK_PROBE_PRESET_VOLUME_CL_MAX;
  }

  channel->status.preset_volume_cl = volume_cl;
}

static uint8_t TrkProbe_UpdatePresetFromBuffer(TrkProbeChannel *channel)
{
  uint32_t parsed_value;

  if (channel == NULL)
  {
    return 0U;
  }

  if ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_MONEY)
  {
    if (TrkProbe_ParseMoneyPresetText(channel->status.preset_edit_buf, &parsed_value) == 0U)
    {
      return 0U;
    }

    channel->status.preset_money = parsed_value;
    if (channel->status.price == 0U)
    {
      channel->status.preset_volume_cl = 0U;
    }
    else
    {
      channel->status.preset_volume_cl = (parsed_value * 100UL) / channel->status.price;
      if (channel->status.preset_volume_cl > TRK_PROBE_PRESET_VOLUME_CL_MAX)
      {
        channel->status.preset_volume_cl = TRK_PROBE_PRESET_VOLUME_CL_MAX;
      }
    }
    return 1U;
  }

  if ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_VOLUME)
  {
    if (TrkProbe_ParseVolumePresetText(channel->status.preset_edit_buf, &parsed_value) == 0U)
    {
      return 0U;
    }

    channel->status.preset_volume_cl = parsed_value;
    channel->status.preset_money = (parsed_value * channel->status.price) / 100UL;
    if (channel->status.preset_money > TRK_PROBE_PRESET_MONEY_MAX)
    {
      channel->status.preset_money = TRK_PROBE_PRESET_MONEY_MAX;
    }
    return 1U;
  }

  TrkProbe_UpdateFullTankPreset(channel);
  return 1U;
}

static void TrkProbe_RecalculatePreset(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  switch ((TrkDispenseMode)channel->status.dispense_mode)
  {
    case TRK_DISPENSE_MODE_MONEY:
      if (channel->status.price == 0U)
      {
        channel->status.preset_volume_cl = 0U;
      }
      else
      {
        channel->status.preset_volume_cl = (channel->status.preset_money * 100UL) / channel->status.price;
        if (channel->status.preset_volume_cl > TRK_PROBE_PRESET_VOLUME_CL_MAX)
        {
          channel->status.preset_volume_cl = TRK_PROBE_PRESET_VOLUME_CL_MAX;
        }
      }
      break;

    case TRK_DISPENSE_MODE_VOLUME:
      channel->status.preset_money = (channel->status.preset_volume_cl * channel->status.price) / 100UL;
      if (channel->status.preset_money > TRK_PROBE_PRESET_MONEY_MAX)
      {
        channel->status.preset_money = TRK_PROBE_PRESET_MONEY_MAX;
      }
      break;

    case TRK_DISPENSE_MODE_FULL:
    default:
      TrkProbe_UpdateFullTankPreset(channel);
      break;
  }
}

static void TrkProbe_LoadDefaults(void)
{
  memset(&trk_channels, 0, sizeof(trk_channels));
  memset(&trk_probe_status, 0, sizeof(trk_probe_status));

  trk_channels[0].huart = &huart3;
  trk_channels[0].trk_id = 1U;
  trk_channels[0].addr_hi = 0x00U;
  trk_channels[0].addr_lo = 0x01U;
  trk_channels[0].status.enabled = 1U;
  trk_channels[0].status.channel_state = (uint8_t)TRK_CHANNEL_OFFLINE;
  trk_channels[0].status.dispense_mode = (uint8_t)TRK_DISPENSE_MODE_MONEY;
  TrkProbe_SetPriceValue(&trk_channels[0], 1100U, "1100");
  TrkProbe_ClearPreset(&trk_channels[0]);
  TrkProbe_ResetTransactionRuntime(&trk_channels[0]);
  strcpy(trk_channels[0].status.last_ascii, "-");

  trk_channels[1].huart = &huart1;
  trk_channels[1].trk_id = 2U;
  trk_channels[1].addr_hi = 0x00U;
  trk_channels[1].addr_lo = 0x01U;
  trk_channels[1].status.enabled = 1U;
  trk_channels[1].status.channel_state = (uint8_t)TRK_CHANNEL_OFFLINE;
  trk_channels[1].status.dispense_mode = (uint8_t)TRK_DISPENSE_MODE_MONEY;
  TrkProbe_SetPriceValue(&trk_channels[1], 1100U, "1100");
  TrkProbe_ClearPreset(&trk_channels[1]);
  TrkProbe_ResetTransactionRuntime(&trk_channels[1]);
  strcpy(trk_channels[1].status.last_ascii, "-");

  trk_probe_status.active_ui_trk = 1U;
  trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_MAIN;
  trk_probe_status.menu_index = 0U;
  trk_probe_status.pending_return_to_menu = 0U;
}

static void TrkProbe_ApplyNvConfig(const TrkProbeNvConfig *config)
{
  uint32_t i;

  if (config == NULL)
  {
    return;
  }

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    trk_channels[i].status.enabled = (config->channels[i].enabled != 0U) ? 1U : 0U;

    {
      uint32_t price_raw;

      if (TrkProbe_ParsePriceText(config->channels[i].price_text, &price_raw) != 0U)
      {
        TrkProbe_SetPriceValue(&trk_channels[i],
                               price_raw,
                               config->channels[i].price_text);
      }
    }

    if (config->channels[i].dispense_mode <= (uint8_t)TRK_DISPENSE_MODE_FULL)
    {
      trk_channels[i].status.dispense_mode = config->channels[i].dispense_mode;
      if ((TrkDispenseMode)trk_channels[i].status.dispense_mode == TRK_DISPENSE_MODE_FULL)
      {
        TrkProbe_UpdateFullTankPreset(&trk_channels[i]);
      }
    }
  }
}

static uint8_t TrkProbe_LoadNvConfig(void)
{
  TrkProbeNvConfig config;
  uint32_t crc_expected;

  if (AT24_DetectAddress() != HAL_OK)
  {
    return 0U;
  }

  if (AT24_Read(TRK_PROBE_NV_ADDR,
                (uint8_t *)&config,
                (uint16_t)sizeof(config),
                TRK_PROBE_NV_TIMEOUT_MS) != HAL_OK)
  {
    return 0U;
  }

  if ((config.magic != TRK_PROBE_NV_MAGIC) ||
      (config.version != TRK_PROBE_NV_VERSION) ||
      (config.size != (uint16_t)sizeof(config)))
  {
    return 0U;
  }

  crc_expected = TrkProbe_Crc32(&config, (uint32_t)(sizeof(config) - sizeof(config.crc32)));
  if (crc_expected != config.crc32)
  {
    return 0U;
  }

  TrkProbe_ApplyNvConfig(&config);
  return 1U;
}

static uint8_t TrkProbe_SaveNvConfig(void)
{
  TrkProbeNvConfig config;
  uint32_t i;

  if (AT24_DetectAddress() != HAL_OK)
  {
    return 0U;
  }

  memset(&config, 0, sizeof(config));
  config.magic = TRK_PROBE_NV_MAGIC;
  config.version = TRK_PROBE_NV_VERSION;
  config.size = (uint16_t)sizeof(config);

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    config.channels[i].enabled = trk_channels[i].status.enabled;
    config.channels[i].dispense_mode = trk_channels[i].status.dispense_mode;
    (void)snprintf(config.channels[i].price_text,
                   sizeof(config.channels[i].price_text),
                   "%s",
                   trk_channels[i].status.price_text);
  }

  config.crc32 = TrkProbe_Crc32(&config, (uint32_t)(sizeof(config) - sizeof(config.crc32)));

  if (AT24_Write(TRK_PROBE_NV_ADDR,
                 (const uint8_t *)&config,
                 (uint16_t)sizeof(config),
                 TRK_PROBE_NV_TIMEOUT_MS) != HAL_OK)
  {
    return 0U;
  }

  return 1U;
}

static void TrkProbe_LogPrice(uint8_t trk_id, uint32_t price)
{
  char price_text[12];
  char message[48];
  static const char *const trk_labels[] = {"TRK1", "TRK2", "TRK3"};
  uint8_t idx = (trk_id >= 1U && trk_id <= 3U) ? (trk_id - 1U) : 0U;

  (void)snprintf(price_text, sizeof(price_text), "%04lu", (unsigned long)price);
  (void)snprintf(message, sizeof(message), "price_raw=%s", price_text);
  AppLog_Message(APP_LOG_LEVEL_INFO, trk_labels[idx], message);
}

static uint8_t TrkProbe_IsEnabled(const TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return 0U;
  }

  return (channel->status.enabled != 0U) ? 1U : 0U;
}

static void TrkProbe_RefreshUiFlags(void)
{
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    trk_channels[i].status.ui_selected =
      (trk_probe_status.active_ui_trk == trk_channels[i].trk_id) ? 1U : 0U;
  }
}

static TrkProbeChannel *TrkProbe_GetActiveUiChannel(void)
{
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    if (trk_channels[i].trk_id == trk_probe_status.active_ui_trk)
    {
      return &trk_channels[i];
    }
  }

  return &trk_channels[0];
}

static TrkProbeChannel *TrkProbe_GetChannelByTrkId(uint8_t trk_id)
{
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    if (trk_channels[i].trk_id == trk_id)
    {
      return &trk_channels[i];
    }
  }

  return NULL;
}

static uint8_t TrkProbe_CanSelectTrk(uint8_t trk_id)
{
  TrkProbeChannel *target = TrkProbe_GetChannelByTrkId(trk_id);
  TrkProbeChannel *other = TrkProbe_GetChannelByTrkId((trk_id == 1U) ? 2U : 1U);

  if ((target == NULL) || (TrkProbe_IsEnabled(target) == 0U))
  {
    return 0U;
  }

  if ((other == NULL) || (TrkProbe_IsEnabled(other) == 0U))
  {
    return 0U;
  }

  return 1U;
}

static void TrkProbe_SelectTrk(uint8_t trk_id)
{
  TrkProbeChannel *target = TrkProbe_GetChannelByTrkId(trk_id);

  if ((target == NULL) || (TrkProbe_IsEnabled(target) == 0U))
  {
    return;
  }

  trk_probe_status.active_ui_trk = trk_id;
  TrkProbe_RefreshUiFlags();
  TrkProbe_SyncPublicStatus(&trk_channels[0]);
  TrkProbe_SyncPublicStatus(&trk_channels[1]);
}

static void TrkProbe_ExitToMain(void)
{
  uint32_t i;

  trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_MAIN;
  trk_probe_status.pending_return_to_menu = 0U;
  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    trk_channels[i].status.price_edit_buf[0] = '\0';
    TrkProbe_SyncPublicStatus(&trk_channels[i]);
  }
}

static void TrkProbe_SetNotice(const char *text, uint32_t duration_ms)
{
  if (text == NULL)
  {
    trk_probe_status.notice[0] = '\0';
    trk_probe_status.notice_until_ms = 0U;
    return;
  }

  (void)snprintf(trk_probe_status.notice,
                 sizeof(trk_probe_status.notice),
                 "%s",
                 text);
  trk_probe_status.notice_until_ms = HAL_GetTick() + duration_ms;
}

static void TrkProbe_CycleDispenseMode(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  switch ((TrkDispenseMode)channel->status.dispense_mode)
  {
    case TRK_DISPENSE_MODE_MONEY:
      channel->status.dispense_mode = (uint8_t)TRK_DISPENSE_MODE_VOLUME;
      break;

    case TRK_DISPENSE_MODE_VOLUME:
      channel->status.dispense_mode = (uint8_t)TRK_DISPENSE_MODE_FULL;
      break;

    case TRK_DISPENSE_MODE_FULL:
    default:
      channel->status.dispense_mode = (uint8_t)TRK_DISPENSE_MODE_MONEY;
      break;
  }

  TrkProbe_ClearPreset(channel);
  if ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_FULL)
  {
    TrkProbe_UpdateFullTankPreset(channel);
  }
}

static void TrkProbe_RegisterCommSuccess(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->comm_fail_streak = 0U;
  channel->status.online = 1U;
}

static void TrkProbe_RegisterCommFailure(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  if (channel->comm_fail_streak < 0xFFU)
  {
    ++channel->comm_fail_streak;
  }

  if (channel->comm_fail_streak >= TRK_PROBE_OFFLINE_AFTER_MISSES)
  {
    channel->status.online = 0U;
    channel->status.channel_state = (uint8_t)TRK_CHANNEL_OFFLINE;
  }
}

static void TrkProbe_UpdateStateFromStatus(TrkProbeChannel *channel, uint8_t status_raw)
{
  if (channel == NULL)
  {
    return;
  }

  switch (status_raw)
  {
    case (uint8_t)'1':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_IDLE;
      TrkProbe_ResetTransactionRuntime(channel);
      break;

    case (uint8_t)'2':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_CALLING;
      break;

    case (uint8_t)'3':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_AUTH_WAIT;
      break;

    case (uint8_t)'4':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_STARTED;
      break;

    case (uint8_t)'5':
    case (uint8_t)'7':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_PAUSED;
      break;

    case (uint8_t)'6':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_FUELLING;
      break;

    case (uint8_t)'8':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_FINISHING;
      break;

    case (uint8_t)'9':
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_FINISHED_HOLD;
      break;

    case (uint8_t)'0':
    default:
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_ERROR;
      break;
  }
}

static void TrkProbe_ProcessChunk(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  uint16_t start = 0U;

  if ((channel == NULL) || (data == NULL) || (len == 0U))
  {
    return;
  }

  while ((start < len) && (data[start] != 0x02U))
  {
    ++start;
  }

  if (start >= len)
  {
    ++channel->status.crc_error_count;
    channel->status.waiting_reply = 0U;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  TrkProbe_HandleRxFrame(channel, &data[start], (uint16_t)(len - start));
}

static void TrkProbe_StartRx(TrkProbeChannel *channel)
{
  if ((channel == NULL) || (channel->huart == NULL))
  {
    return;
  }

  if (TrkProbe_IsActive(channel->trk_id) == 0U)
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

static void TrkProbe_RestartRx(TrkProbeChannel *channel)
{
  if ((channel == NULL) || (channel->huart == NULL))
  {
    return;
  }

  channel->frame_len = 0U;
  (void)HAL_UART_DMAStop(channel->huart);
  memset(channel->rx_buf, 0, sizeof(channel->rx_buf));
  TrkProbe_StartRx(channel);
}

static uint8_t TrkProbe_HasPendingRxData(const TrkProbeChannel *channel)
{
  uint32_t remaining;

  if ((channel == NULL) ||
      (channel->huart == NULL) ||
      (channel->huart->hdmarx == NULL))
  {
    return 0U;
  }

  remaining = __HAL_DMA_GET_COUNTER(channel->huart->hdmarx);
  if (remaining >= TRK_PROBE_RX_BUF_SIZE)
  {
    return 0U;
  }

  return 1U;
}

static TrkProbeChannel *TrkProbe_FindByHandle(UART_HandleTypeDef *huart)
{
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
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
  if (trk_id == 1U)
  {
    return &trk_probe_status.trk1;
  }

  if (trk_id == 3U)
  {
    return &trk_probe_status.trk3;
  }

  return &trk_probe_status.trk2;
}

static void TrkProbe_SyncPublicStatus(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  *TrkProbe_PublicStatus(channel->trk_id) = channel->status;
}

static uint8_t TrkProbe_SendFrame(TrkProbeChannel *channel, const uint8_t *payload, uint16_t payload_len)
{
  uint16_t i;

  if ((channel == NULL) || (channel->huart == NULL) || (payload == NULL) || (payload_len == 0U))
  {
    return 0U;
  }

  if ((TrkProbe_IsActive(channel->trk_id) == 0U) ||
      (TrkProbe_IsEnabled(channel) == 0U) ||
      (payload_len > (uint16_t)(sizeof(channel->tx_buf) - 2U)))
  {
    return 0U;
  }

  channel->tx_buf[0] = 0x02U;
  for (i = 0U; i < payload_len; ++i)
  {
    channel->tx_buf[1U + i] = payload[i];
  }
  channel->tx_buf[1U + payload_len] = TrkProbe_CalcXor(&channel->tx_buf[1], payload_len);

  if (HAL_UART_Transmit_DMA(channel->huart, channel->tx_buf, (uint16_t)(payload_len + 2U)) != HAL_OK)
  {
    ++channel->status.timeout_count;
    AppLog_Message(APP_LOG_LEVEL_ERROR, "TRK", "TX DMA start failed");
    TrkProbe_SyncPublicStatus(channel);
    return 0U;
  }

  channel->status.tx_busy = 1U;
  channel->status.waiting_reply = 1U;
  channel->last_tx_tick = HAL_GetTick();
  channel->last_poll_tick = channel->last_tx_tick;
  ++channel->status.tx_count;
  AppLog_ProtoFrame(channel->trk_id,
                    APP_LOG_PROTO_DIR_TX,
                    channel->tx_buf,
                    (uint16_t)(payload_len + 2U));
  TrkProbe_SyncPublicStatus(channel);
  return 1U;
}

static uint8_t TrkProbe_SendSimpleCommand(TrkProbeChannel *channel, char command)
{
  uint8_t payload[3];

  if (channel == NULL)
  {
    return 0U;
  }

  payload[0] = channel->addr_hi;
  payload[1] = channel->addr_lo;
  payload[2] = (uint8_t)command;
  return TrkProbe_SendFrame(channel, payload, sizeof(payload));
}

static void TrkProbe_SendPoll(TrkProbeChannel *channel)
{
  (void)TrkProbe_SendSimpleCommand(channel, 'S');
}

static uint8_t TrkProbe_StartTransaction(TrkProbeChannel *channel)
{
  uint8_t payload[20];
  int written;

  if (channel == NULL)
  {
    return 0U;
  }

  if ((channel->status.online == 0U) ||
      (channel->status.tx_busy != 0U) ||
      (channel->status.waiting_reply != 0U) ||
      ((channel->status.channel_state != (uint8_t)TRK_CHANNEL_IDLE) &&
       (channel->status.channel_state != (uint8_t)TRK_CHANNEL_CALLING)))
  {
    return 0U;
  }

  if (channel->status.price == 0U)
  {
    return 0U;
  }

  payload[0] = channel->addr_hi;
  payload[1] = channel->addr_lo;

  if ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_MONEY)
  {
    if (channel->status.preset_money == 0U)
    {
      return 0U;
    }

    written = snprintf((char *)&payload[2],
                       sizeof(payload) - 2U,
                       "M1;%06lu;%04lu",
                       (unsigned long)channel->status.preset_money,
                       (unsigned long)channel->status.price);
  }
  else
  {
    if (channel->status.preset_volume_cl == 0U)
    {
      return 0U;
    }

    written = snprintf((char *)&payload[2],
                       sizeof(payload) - 2U,
                       "V1;%06lu;%04lu",
                       (unsigned long)channel->status.preset_volume_cl,
                       (unsigned long)channel->status.price);
  }

  if ((written <= 0) || ((uint16_t)(2U + written) > (uint16_t)(sizeof(payload) - 1U)))
  {
    return 0U;
  }

  if (TrkProbe_SendFrame(channel, payload, (uint16_t)(2U + written)) == 0U)
  {
    return 0U;
  }

  channel->status.transaction_pending = 1U;
  channel->status.final_data_ready = 0U;
  channel->live_poll_phase = 0U;
  channel->final_request_sent = 0U;
  channel->close_request_sent = 0U;
  channel->status.live_money = 0U;
  channel->status.live_volume_cl = 0U;
  channel->status.final_money = 0U;
  channel->status.final_volume_cl = 0U;
  channel->status.transaction_id = '\0';
  return 1U;
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
    channel->status.waiting_reply = 0U;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  if ((data[0] != 0x02U) || (data[1] != channel->addr_hi) || (data[2] != channel->addr_lo) || (data[3] != (uint8_t)'S'))
  {
    ++channel->status.crc_error_count;
    channel->status.waiting_reply = 0U;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  channel->status.last_status = data[4];
  channel->status.last_nozzle = data[5];
  channel->status.waiting_reply = 0U;
  TrkProbe_RegisterCommSuccess(channel);
  TrkProbe_UpdateStateFromStatus(channel, data[4]);
  ++channel->status.ok_count;
}

static void TrkProbe_ParseLiveVolumeResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  char value_buf[7];

  if ((channel == NULL) || (data == NULL) || (len < 15U) || (data[7] != (uint8_t)';'))
  {
    ++channel->status.crc_error_count;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  memcpy(value_buf, &data[8], 6U);
  value_buf[6] = '\0';

  channel->status.live_volume_cl = (uint32_t)strtoul(value_buf, NULL, 10);
  channel->status.last_nozzle = data[4];
  channel->status.transaction_id = (char)data[5];
  TrkProbe_UpdateStateFromStatus(channel, data[6]);
  TrkProbe_RegisterCommSuccess(channel);
  ++channel->status.ok_count;
}

static void TrkProbe_ParseLiveMoneyResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  char value_buf[7];

  if ((channel == NULL) || (data == NULL) || (len < 15U) || (data[7] != (uint8_t)';'))
  {
    ++channel->status.crc_error_count;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  memcpy(value_buf, &data[8], 6U);
  value_buf[6] = '\0';

  channel->status.live_money = (uint32_t)strtoul(value_buf, NULL, 10);
  channel->status.last_nozzle = data[4];
  channel->status.transaction_id = (char)data[5];
  TrkProbe_UpdateStateFromStatus(channel, data[6]);
  TrkProbe_RegisterCommSuccess(channel);
  ++channel->status.ok_count;
}

static void TrkProbe_ParseFinalResponse(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  char money_buf[7];
  char volume_buf[7];

  if ((channel == NULL) || (data == NULL) || (len < 27U) ||
      (data[7] != (uint8_t)';') || (data[14] != (uint8_t)';') || (data[21] != (uint8_t)';'))
  {
    ++channel->status.crc_error_count;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  memcpy(money_buf, &data[8], 6U);
  money_buf[6] = '\0';
  memcpy(volume_buf, &data[15], 6U);
  volume_buf[6] = '\0';

  channel->status.final_money = (uint32_t)strtoul(money_buf, NULL, 10);
  channel->status.final_volume_cl = (uint32_t)strtoul(volume_buf, NULL, 10);
  channel->status.last_nozzle = data[4];
  channel->status.transaction_id = (char)data[5];
  channel->status.final_data_ready = 1U;
  TrkProbe_UpdateStateFromStatus(channel, data[6]);
  TrkProbe_RegisterCommSuccess(channel);
  ++channel->status.ok_count;
}

static void TrkProbe_HandleRxFrame(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  uint8_t crc_expected;

  if ((channel == NULL) || (data == NULL) || (len < 5U))
  {
    if (channel != NULL)
    {
      ++channel->status.crc_error_count;
      channel->status.waiting_reply = 0U;
      TrkProbe_RegisterCommFailure(channel);
    }
    return;
  }

  ++channel->status.rx_count;
  channel->status.last_rx_len = len;
  channel->status.last_rx_tick = HAL_GetTick();
  TrkProbe_SetAscii(&channel->status, data, len);
  AppLog_ProtoFrame(channel->trk_id, APP_LOG_PROTO_DIR_RX, data, len);

  crc_expected = TrkProbe_CalcXor(&data[1], (uint16_t)(len - 2U));
  if ((data[0] != 0x02U) ||
      (data[1] != channel->addr_hi) ||
      (data[2] != channel->addr_lo) ||
      (crc_expected != data[len - 1U]))
  {
    ++channel->status.crc_error_count;
    channel->status.waiting_reply = 0U;
    TrkProbe_RegisterCommFailure(channel);
    return;
  }

  channel->status.waiting_reply = 0U;

  switch (data[3])
  {
    case (uint8_t)'S':
      TrkProbe_HandleStatusResponse(channel, data, len);
      break;

    case (uint8_t)'L':
      TrkProbe_ParseLiveVolumeResponse(channel, data, len);
      break;

    case (uint8_t)'R':
      TrkProbe_ParseLiveMoneyResponse(channel, data, len);
      break;

    case (uint8_t)'T':
      TrkProbe_ParseFinalResponse(channel, data, len);
      break;

    default:
      ++channel->status.crc_error_count;
      TrkProbe_RegisterCommFailure(channel);
      break;
  }
}

void TrkProbe_Init(void)
{
  uint32_t i;

  TrkProbe_LoadDefaults();
  (void)TrkProbe_LoadNvConfig();
  TrkProbe_RefreshUiFlags();

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    TrkProbe_StartRx(&trk_channels[i]);
    TrkProbe_SyncPublicStatus(&trk_channels[i]);
  }

  AppLog_Message(APP_LOG_LEVEL_INFO, "TRK",
                 "Dual-channel status polling ready");
}

void TrkProbe_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    TrkProbeChannel *channel = &trk_channels[i];

    if (TrkProbe_IsActive(channel->trk_id) == 0U)
    {
      continue;
    }

    if (TrkProbe_IsEnabled(channel) == 0U)
    {
      channel->comm_fail_streak = 0U;
      channel->status.online = 0U;
      channel->status.waiting_reply = 0U;
      channel->status.tx_busy = 0U;
      channel->status.channel_state = (uint8_t)TRK_CHANNEL_OFFLINE;
      TrkProbe_SyncPublicStatus(channel);
      continue;
    }

    if ((channel->status.waiting_reply != 0U) &&
        (TrkProbe_HasPendingRxData(channel) == 0U) &&
        ((now - channel->last_tx_tick) >= TRK_PROBE_TIMEOUT_MS))
    {
      channel->status.waiting_reply = 0U;
      channel->frame_len = 0U;
      ++channel->status.timeout_count;
      TrkProbe_RegisterCommFailure(channel);
      {
        static const char *const trk_labels[] = {"TRK1", "TRK2", "TRK3"};
        uint8_t idx = (channel->trk_id >= 1U && channel->trk_id <= 3U)
                      ? (channel->trk_id - 1U) : 0U;
        AppLog_Message(APP_LOG_LEVEL_WARN, trk_labels[idx], "poll timeout");
      }
      TrkProbe_RestartRx(channel);
      TrkProbe_SyncPublicStatus(channel);
    }

    if ((channel->status.tx_busy == 0U) &&
        (channel->status.waiting_reply == 0U) &&
        ((now - channel->last_poll_tick) >= TRK_PROBE_POLL_MS))
    {
      uint8_t sent = 0U;

      if (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FINISHING)
      {
        if (channel->final_request_sent == 0U)
        {
          sent = TrkProbe_SendSimpleCommand(channel, 'T');
          if (sent != 0U)
          {
            channel->final_request_sent = 1U;
          }
        }
      }
      else if (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FINISHED_HOLD)
      {
        if (channel->close_request_sent == 0U)
        {
          sent = TrkProbe_SendSimpleCommand(channel, 'N');
          if (sent != 0U)
          {
            channel->close_request_sent = 1U;
          }
        }
      }
      else if ((channel->status.channel_state == (uint8_t)TRK_CHANNEL_STARTED) ||
               (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FUELLING) ||
               (channel->status.channel_state == (uint8_t)TRK_CHANNEL_PAUSED))
      {
        if (channel->live_poll_phase == 1U)
        {
          sent = TrkProbe_SendSimpleCommand(channel, 'L');
        }
        else if (channel->live_poll_phase == 2U)
        {
          sent = TrkProbe_SendSimpleCommand(channel, 'R');
        }
        else
        {
          sent = TrkProbe_SendSimpleCommand(channel, 'S');
        }

        if (sent != 0U)
        {
          channel->live_poll_phase = (uint8_t)((channel->live_poll_phase + 1U) % 3U);
        }
      }

      if (sent == 0U)
      {
        TrkProbe_SendPoll(channel);
      }
    }
  }

  if ((trk_probe_status.notice_until_ms != 0U) &&
      ((int32_t)(HAL_GetTick() - trk_probe_status.notice_until_ms) >= 0))
  {
    if (trk_probe_status.pending_return_to_menu != 0U)
    {
      trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_MENU;
      trk_probe_status.pending_return_to_menu = 0U;
    }

    trk_probe_status.notice[0] = '\0';
    trk_probe_status.notice_until_ms = 0U;
  }
}

void TrkProbe_OnRxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  TrkProbeChannel *channel = TrkProbe_FindByHandle(huart);

  if ((channel == NULL) || (size == 0U))
  {
    return;
  }

  if (TrkProbe_IsActive(channel->trk_id) == 0U)
  {
    return;
  }

  if (size > TRK_PROBE_RX_BUF_SIZE)
  {
    size = TRK_PROBE_RX_BUF_SIZE;
  }

  TrkProbe_ProcessChunk(channel, channel->rx_buf, size);
  TrkProbe_RestartRx(channel);
  TrkProbe_SyncPublicStatus(channel);
}

void TrkProbe_OnTxCplt(UART_HandleTypeDef *huart)
{
  TrkProbeChannel *channel = TrkProbe_FindByHandle(huart);

  if (channel == NULL)
  {
    return;
  }

  if (TrkProbe_IsActive(channel->trk_id) == 0U)
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

void TrkProbe_HandleKey(const KeyboardEvent *event)
{
  TrkProbeChannel *channel;
  TrkProbeChannel *target;
  size_t len;

  if ((event == NULL) || (event->pressed == 0U))
  {
    return;
  }

  if (trk_probe_status.ui_mode == (uint8_t)TRK_UI_MODE_MAIN)
  {
    if (event->key == 'F')
    {
      trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_MENU;
      trk_probe_status.menu_index = 0U;
      TrkProbe_SyncPublicStatus(&trk_channels[0]);
      TrkProbe_SyncPublicStatus(&trk_channels[1]);
      return;
    }

    if (event->key == 'B')
    {
      channel = TrkProbe_GetActiveUiChannel();
      if ((channel != NULL) && (TrkProbe_IsEnabled(channel) != 0U))
      {
        TrkProbe_CycleDispenseMode(channel);
        if (TrkProbe_SaveNvConfig() == 0U)
        {
          TrkProbe_SetNotice("Save failed", 1200U);
        }
        TrkProbe_SyncPublicStatus(channel);
      }
      return;
    }

    if (event->key == 'K')
    {
      channel = TrkProbe_GetActiveUiChannel();
      if ((channel != NULL) && (TrkProbe_IsEnabled(channel) != 0U))
      {
        if (TrkProbe_StartTransaction(channel) != 0U)
        {
          TrkProbe_SetNotice("Start sent", 1200U);
        }
        else
        {
          TrkProbe_SetNotice("Start blocked", 1200U);
        }
        TrkProbe_SyncPublicStatus(channel);
      }
      return;
    }

    if (event->key == 'E')
    {
      channel = TrkProbe_GetActiveUiChannel();
      if ((channel != NULL) && (TrkProbe_IsEnabled(channel) != 0U))
      {
        len = strlen(channel->status.preset_edit_buf);
        if (len > 0U)
        {
          channel->status.preset_edit_buf[len - 1U] = '\0';
          if (TrkProbe_UpdatePresetFromBuffer(channel) == 0U)
          {
            TrkProbe_ClearPreset(channel);
          }
        }
        else
        {
          TrkProbe_ClearPreset(channel);
          if ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_FULL)
          {
            TrkProbe_UpdateFullTankPreset(channel);
          }
        }
        TrkProbe_SyncPublicStatus(channel);
      }
      return;
    }

    if ((event->key >= '0') && (event->key <= '9'))
    {
      channel = TrkProbe_GetActiveUiChannel();
      if ((channel != NULL) &&
          (TrkProbe_IsEnabled(channel) != 0U) &&
          ((TrkDispenseMode)channel->status.dispense_mode != TRK_DISPENSE_MODE_FULL))
      {
        char backup[sizeof(channel->status.preset_edit_buf)];

        (void)snprintf(backup, sizeof(backup), "%s", channel->status.preset_edit_buf);
        len = strlen(channel->status.preset_edit_buf);
        if (len < (sizeof(channel->status.preset_edit_buf) - 1U))
        {
          channel->status.preset_edit_buf[len] = event->key;
          channel->status.preset_edit_buf[len + 1U] = '\0';
          if (TrkProbe_UpdatePresetFromBuffer(channel) == 0U)
          {
            (void)snprintf(channel->status.preset_edit_buf,
                           sizeof(channel->status.preset_edit_buf),
                           "%s",
                           backup);
            (void)TrkProbe_UpdatePresetFromBuffer(channel);
            TrkProbe_SetNotice("Bad preset", 1200U);
          }
          TrkProbe_SyncPublicStatus(channel);
        }
      }
      return;
    }

    if (event->key == '.')
    {
      channel = TrkProbe_GetActiveUiChannel();
      if ((channel != NULL) &&
          (TrkProbe_IsEnabled(channel) != 0U) &&
          ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_VOLUME))
      {
        len = strlen(channel->status.preset_edit_buf);
        if ((len > 0U) &&
            (len < (sizeof(channel->status.preset_edit_buf) - 1U)) &&
            (strchr(channel->status.preset_edit_buf, '.') == NULL))
        {
          channel->status.preset_edit_buf[len] = '.';
          channel->status.preset_edit_buf[len + 1U] = '\0';
          TrkProbe_SyncPublicStatus(channel);
        }
      }
      return;
    }

    if ((event->key == 'G') && (TrkProbe_CanSelectTrk(1U) != 0U))
    {
      TrkProbe_SelectTrk(1U);
    }
    else if ((event->key == 'H') && (TrkProbe_CanSelectTrk(2U) != 0U))
    {
      TrkProbe_SelectTrk(2U);
    }
    return;
  }

  if (trk_probe_status.ui_mode == (uint8_t)TRK_UI_MODE_MENU)
  {
    if (event->key == 'E')
    {
      TrkProbe_ExitToMain();
      return;
    }

    if (event->key == 'A')
    {
      if (trk_probe_status.menu_index > 0U)
      {
        --trk_probe_status.menu_index;
      }
      return;
    }

    if (event->key == 'B')
    {
      if (trk_probe_status.menu_index + 1U < TRK_PROBE_MENU_ITEMS)
      {
        ++trk_probe_status.menu_index;
      }
      return;
    }

    if (event->key == 'K')
    {
      switch (trk_probe_status.menu_index)
      {
        case 0U:
          target = &trk_channels[0];
          target->status.enabled = (target->status.enabled == 0U) ? 1U : 0U;
          if (target->status.enabled != 0U)
          {
            TrkProbe_RestartRx(target);
          }
          else if (trk_probe_status.active_ui_trk == target->trk_id)
          {
            TrkProbe_SelectTrk(2U);
          }
          if (TrkProbe_SaveNvConfig() == 0U)
          {
            TrkProbe_SetNotice("Save failed", 1200U);
          }
          TrkProbe_SyncPublicStatus(target);
          return;

        case 1U:
          target = &trk_channels[1];
          target->status.enabled = (target->status.enabled == 0U) ? 1U : 0U;
          if (target->status.enabled != 0U)
          {
            TrkProbe_RestartRx(target);
          }
          else if (trk_probe_status.active_ui_trk == target->trk_id)
          {
            TrkProbe_SelectTrk(1U);
          }
          if (TrkProbe_SaveNvConfig() == 0U)
          {
            TrkProbe_SetNotice("Save failed", 1200U);
          }
          TrkProbe_SyncPublicStatus(target);
          return;

        case 2U:
          channel = &trk_channels[0];
          channel->status.price_edit_buf[0] = '\0';
          trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_EDIT_PRICE;
          trk_probe_status.active_ui_trk = channel->trk_id;
          trk_probe_status.pending_return_to_menu = 0U;
          TrkProbe_SetNotice(NULL, 0U);
          TrkProbe_RefreshUiFlags();
          TrkProbe_SyncPublicStatus(channel);
          return;

        case 3U:
        default:
          channel = &trk_channels[1];
          channel->status.price_edit_buf[0] = '\0';
          trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_EDIT_PRICE;
          trk_probe_status.active_ui_trk = channel->trk_id;
          trk_probe_status.pending_return_to_menu = 0U;
          TrkProbe_SetNotice(NULL, 0U);
          TrkProbe_RefreshUiFlags();
          TrkProbe_SyncPublicStatus(channel);
          return;
      }
    }
    return;
  }

  if (trk_probe_status.ui_mode != (uint8_t)TRK_UI_MODE_EDIT_PRICE)
  {
    return;
  }

  channel = TrkProbe_GetActiveUiChannel();
  if (channel == NULL)
  {
    return;
  }

  if (event->key == 'E')
  {
    len = strlen(channel->status.price_edit_buf);
    if (len > 0U)
    {
      channel->status.price_edit_buf[len - 1U] = '\0';
    }
    else
    {
      trk_probe_status.ui_mode = (uint8_t)TRK_UI_MODE_MENU;
      channel->status.price_edit_buf[0] = '\0';
    }
    return;
  }

  if (event->key == 'K')
  {
    uint32_t parsed_price = channel->status.price;

    if ((channel->status.price_edit_buf[0] != '\0') &&
        (TrkProbe_ParsePriceEditValue(channel, &parsed_price) == 0U))
    {
      TrkProbe_SetNotice("Bad price", 1200U);
      trk_probe_status.pending_return_to_menu = 0U;
      return;
    }

    if (channel->status.price_edit_buf[0] != '\0')
    {
      TrkProbe_SetPriceValue(channel,
                             parsed_price,
                             channel->status.price_edit_buf);
    }
    else
    {
      channel->status.price_edit_buf[0] = '\0';
    }

    TrkProbe_RecalculatePreset(channel);
    TrkProbe_LogPrice(channel->trk_id, channel->status.price);
    if (TrkProbe_SaveNvConfig() != 0U)
    {
      TrkProbe_SetNotice("Price saved", 1200U);
    }
    else
    {
      TrkProbe_SetNotice("Save failed", 1200U);
    }
    trk_probe_status.pending_return_to_menu = 1U;
    TrkProbe_SyncPublicStatus(channel);
    return;
  }

  if ((event->key >= '0') && (event->key <= '9'))
  {
    len = strlen(channel->status.price_edit_buf);
    if (len < (sizeof(channel->status.price_edit_buf) - 1U))
    {
      channel->status.price_edit_buf[len] = event->key;
      channel->status.price_edit_buf[len + 1U] = '\0';
    }
    TrkProbe_SyncPublicStatus(channel);
    return;
  }

  if (event->key == '.')
  {
    if (strchr(channel->status.price_edit_buf, '.') == NULL)
    {
      len = strlen(channel->status.price_edit_buf);
      if ((len > 0U) && (len < (sizeof(channel->status.price_edit_buf) - 1U)))
      {
        channel->status.price_edit_buf[len] = '.';
        channel->status.price_edit_buf[len + 1U] = '\0';
      }
    }
    TrkProbe_SyncPublicStatus(channel);
  }
}
