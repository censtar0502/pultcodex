#include "proto_gaskitlink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t GasKitLink_CalcXor(const uint8_t *data, uint16_t len)
{
  uint16_t i;
  uint8_t crc = 0U;

  for (i = 0U; i < len; ++i)
  {
    crc ^= data[i];
  }

  return crc;
}

static uint8_t GasKitLink_FillFrame(GasKitLinkProtoCtx *ctx,
                                    const uint8_t *payload,
                                    uint8_t payload_len,
                                    frame_t *frame)
{
  if ((ctx == NULL) || (payload == NULL) || (frame == NULL) || (payload_len == 0U))
  {
    return 0U;
  }

  if ((uint16_t)(payload_len + 2U) > (uint16_t)sizeof(frame->buf))
  {
    return 0U;
  }

  frame->buf[0] = 0x02U;
  memcpy(&frame->buf[1], payload, payload_len);
  frame->buf[1U + payload_len] = GasKitLink_CalcXor(&frame->buf[1], payload_len);
  frame->len = (uint8_t)(payload_len + 2U);
  return 1U;
}

static uint8_t GasKitLink_BuildSimple(GasKitLinkProtoCtx *ctx, uint8_t command, frame_t *frame)
{
  uint8_t payload[3];

  if (ctx == NULL)
  {
    return 0U;
  }

  payload[0] = ctx->addr_hi;
  payload[1] = ctx->addr_lo;
  payload[2] = command;
  return GasKitLink_FillFrame(ctx, payload, sizeof(payload), frame);
}

static proto_status_t GasKitLink_MapStatus(uint8_t status_raw)
{
  switch (status_raw)
  {
    case (uint8_t)'1':
      return PROTO_STATUS_IDLE;
    case (uint8_t)'2':
      return PROTO_STATUS_CALLING;
    case (uint8_t)'3':
      return PROTO_STATUS_AUTH_WAIT;
    case (uint8_t)'4':
      return PROTO_STATUS_STARTED;
    case (uint8_t)'5':
    case (uint8_t)'7':
      return PROTO_STATUS_PAUSED;
    case (uint8_t)'6':
      return PROTO_STATUS_FUELLING;
    case (uint8_t)'8':
      return PROTO_STATUS_FINISHING;
    case (uint8_t)'9':
      return PROTO_STATUS_FINISHED_HOLD;
    case (uint8_t)'0':
    default:
      return PROTO_STATUS_ERROR;
  }
}

static uint8_t GasKitLink_ParseU32Field(const uint8_t *src, uint8_t len, uint32_t *value_out)
{
  char buf[12];
  uint8_t i;

  if ((src == NULL) || (value_out == NULL) || (len == 0U) || (len >= (uint8_t)sizeof(buf)))
  {
    return 0U;
  }

  for (i = 0U; i < len; ++i)
  {
    if ((src[i] < (uint8_t)'0') || (src[i] > (uint8_t)'9'))
    {
      return 0U;
    }
  }

  memcpy(buf, src, len);
  buf[len] = '\0';
  *value_out = (uint32_t)strtoul(buf, NULL, 10);
  return 1U;
}

static uint8_t GasKitLink_BuildStatusPoll(void *proto_ctx, frame_t *frame)
{
  return GasKitLink_BuildSimple((GasKitLinkProtoCtx *)proto_ctx, (uint8_t)'S', frame);
}

static uint8_t GasKitLink_BuildLiveVolumeRequest(void *proto_ctx, frame_t *frame)
{
  return GasKitLink_BuildSimple((GasKitLinkProtoCtx *)proto_ctx, (uint8_t)'L', frame);
}

static uint8_t GasKitLink_BuildLiveMoneyRequest(void *proto_ctx, frame_t *frame)
{
  return GasKitLink_BuildSimple((GasKitLinkProtoCtx *)proto_ctx, (uint8_t)'R', frame);
}

static uint8_t GasKitLink_BuildFinalRequest(void *proto_ctx, frame_t *frame)
{
  return GasKitLink_BuildSimple((GasKitLinkProtoCtx *)proto_ctx, (uint8_t)'T', frame);
}

static uint8_t GasKitLink_BuildTotalizerRequest(void *proto_ctx, frame_t *frame)
{
  GasKitLinkProtoCtx *ctx = (GasKitLinkProtoCtx *)proto_ctx;
  uint8_t payload[4];

  if ((ctx == NULL) || (frame == NULL))
  {
    return 0U;
  }

  payload[0] = ctx->addr_hi;
  payload[1] = ctx->addr_lo;
  payload[2] = (uint8_t)'C';
  payload[3] = (uint8_t)'1';
  return GasKitLink_FillFrame(ctx, payload, sizeof(payload), frame);
}

static uint8_t GasKitLink_BuildCloseTransaction(void *proto_ctx, frame_t *frame)
{
  return GasKitLink_BuildSimple((GasKitLinkProtoCtx *)proto_ctx, (uint8_t)'N', frame);
}

static uint8_t GasKitLink_BuildStartMoney(void *proto_ctx, uint32_t money, uint32_t price, frame_t *frame)
{
  GasKitLinkProtoCtx *ctx = (GasKitLinkProtoCtx *)proto_ctx;
  uint8_t payload[20];
  int written;

  if ((ctx == NULL) || (frame == NULL) || (money == 0U) || (price == 0U))
  {
    return 0U;
  }

  payload[0] = ctx->addr_hi;
  payload[1] = ctx->addr_lo;
  written = snprintf((char *)&payload[2],
                     sizeof(payload) - 2U,
                     "M1;%06lu;%04lu",
                     (unsigned long)money,
                     (unsigned long)price);
  if ((written <= 0) || ((uint16_t)(2U + written) > (uint16_t)(sizeof(payload) - 1U)))
  {
    return 0U;
  }

  return GasKitLink_FillFrame(ctx, payload, (uint8_t)(2U + written), frame);
}

static uint8_t GasKitLink_BuildStartVolume(void *proto_ctx, uint32_t volume_cl, uint32_t price, frame_t *frame)
{
  GasKitLinkProtoCtx *ctx = (GasKitLinkProtoCtx *)proto_ctx;
  uint8_t payload[20];
  int written;

  if ((ctx == NULL) || (frame == NULL) || (volume_cl == 0U) || (price == 0U))
  {
    return 0U;
  }

  payload[0] = ctx->addr_hi;
  payload[1] = ctx->addr_lo;
  written = snprintf((char *)&payload[2],
                     sizeof(payload) - 2U,
                     "V1;%06lu;%04lu",
                     (unsigned long)volume_cl,
                     (unsigned long)price);
  if ((written <= 0) || ((uint16_t)(2U + written) > (uint16_t)(sizeof(payload) - 1U)))
  {
    return 0U;
  }

  return GasKitLink_FillFrame(ctx, payload, (uint8_t)(2U + written), frame);
}

static uint8_t GasKitLink_ParseResponse(void *proto_ctx,
                                        const uint8_t *data,
                                        uint16_t len,
                                        proto_event_t *event_out)
{
  GasKitLinkProtoCtx *ctx = (GasKitLinkProtoCtx *)proto_ctx;
  uint8_t crc_expected;

  if ((ctx == NULL) || (data == NULL) || (event_out == NULL) || (len < 5U))
  {
    return 0U;
  }

  crc_expected = GasKitLink_CalcXor(&data[1], (uint16_t)(len - 2U));
  if ((data[0] != 0x02U) ||
      (data[1] != ctx->addr_hi) ||
      (data[2] != ctx->addr_lo) ||
      (crc_expected != data[len - 1U]))
  {
    return 0U;
  }

  memset(event_out, 0, sizeof(*event_out));

  switch (data[3])
  {
    case (uint8_t)'S':
      if (len < 7U)
      {
        return 0U;
      }
      event_out->kind = PROTO_EVENT_STATUS;
      event_out->status_raw = data[4];
      event_out->nozzle_raw = data[5];
      event_out->status = GasKitLink_MapStatus(data[4]);
      return 1U;

    case (uint8_t)'L':
      if ((len < 15U) || (data[7] != (uint8_t)';') ||
          (GasKitLink_ParseU32Field(&data[8], 6U, &event_out->volume_cl) == 0U))
      {
        return 0U;
      }
      event_out->kind = PROTO_EVENT_LIVE_VOLUME;
      event_out->nozzle_raw = data[4];
      event_out->transaction_id = (char)data[5];
      event_out->status_raw = data[6];
      event_out->status = GasKitLink_MapStatus(data[6]);
      return 1U;

    case (uint8_t)'R':
      if ((len < 15U) || (data[7] != (uint8_t)';') ||
          (GasKitLink_ParseU32Field(&data[8], 6U, &event_out->money) == 0U))
      {
        return 0U;
      }
      event_out->kind = PROTO_EVENT_LIVE_MONEY;
      event_out->nozzle_raw = data[4];
      event_out->transaction_id = (char)data[5];
      event_out->status_raw = data[6];
      event_out->status = GasKitLink_MapStatus(data[6]);
      return 1U;

    case (uint8_t)'T':
      if ((len < 27U) ||
          (data[7] != (uint8_t)';') ||
          (data[14] != (uint8_t)';') ||
          (data[21] != (uint8_t)';') ||
          (GasKitLink_ParseU32Field(&data[8], 6U, &event_out->money) == 0U) ||
          (GasKitLink_ParseU32Field(&data[15], 6U, &event_out->volume_cl) == 0U))
      {
        return 0U;
      }
      event_out->kind = PROTO_EVENT_FINAL;
      event_out->nozzle_raw = data[4];
      event_out->transaction_id = (char)data[5];
      event_out->status_raw = data[6];
      event_out->status = GasKitLink_MapStatus(data[6]);
      return 1U;

    case (uint8_t)'C':
      if ((len < 16U) ||
          (data[4] != (uint8_t)'1') ||
          (data[5] != (uint8_t)';') ||
          (GasKitLink_ParseU32Field(&data[6], 9U, &event_out->volume_cl) == 0U))
      {
        return 0U;
      }
      event_out->kind = PROTO_EVENT_TOTALIZER;
      return 1U;

    default:
      return 0U;
  }
}

const dispenser_protocol_vtable_t gaskitlink_vtable =
{
  .build_status_poll = GasKitLink_BuildStatusPoll,
  .build_live_volume_request = GasKitLink_BuildLiveVolumeRequest,
  .build_live_money_request = GasKitLink_BuildLiveMoneyRequest,
  .build_final_request = GasKitLink_BuildFinalRequest,
  .build_totalizer_request = GasKitLink_BuildTotalizerRequest,
  .build_close_transaction = GasKitLink_BuildCloseTransaction,
  .build_start_money = GasKitLink_BuildStartMoney,
  .build_start_volume = GasKitLink_BuildStartVolume,
  .parse_response = GasKitLink_ParseResponse
};
