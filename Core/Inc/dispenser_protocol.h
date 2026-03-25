#ifndef DISPENSER_PROTOCOL_H
#define DISPENSER_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "protocol_types.h"

typedef struct
{
  uint8_t (*build_status_poll)(void *proto_ctx, frame_t *frame);
  uint8_t (*build_live_volume_request)(void *proto_ctx, frame_t *frame);
  uint8_t (*build_live_money_request)(void *proto_ctx, frame_t *frame);
  uint8_t (*build_final_request)(void *proto_ctx, frame_t *frame);
  uint8_t (*build_totalizer_request)(void *proto_ctx, frame_t *frame);
  uint8_t (*build_close_transaction)(void *proto_ctx, frame_t *frame);
  uint8_t (*build_start_money)(void *proto_ctx, uint32_t money, uint32_t price, frame_t *frame);
  uint8_t (*build_start_volume)(void *proto_ctx, uint32_t volume_cl, uint32_t price, frame_t *frame);
  uint8_t (*parse_response)(void *proto_ctx, const uint8_t *data, uint16_t len, proto_event_t *event_out);
} dispenser_protocol_vtable_t;

#ifdef __cplusplus
}
#endif

#endif /* DISPENSER_PROTOCOL_H */
