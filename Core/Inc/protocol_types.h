#ifndef PROTOCOL_TYPES_H
#define PROTOCOL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  uint8_t buf[32];
  uint8_t len;
} frame_t;

typedef enum
{
  PROTO_STATUS_UNKNOWN = 0,
  PROTO_STATUS_IDLE,
  PROTO_STATUS_CALLING,
  PROTO_STATUS_AUTH_WAIT,
  PROTO_STATUS_STARTED,
  PROTO_STATUS_PAUSED,
  PROTO_STATUS_FUELLING,
  PROTO_STATUS_FINISHING,
  PROTO_STATUS_FINISHED_HOLD,
  PROTO_STATUS_ERROR
} proto_status_t;

typedef enum
{
  PROTO_EVENT_NONE = 0,
  PROTO_EVENT_STATUS,
  PROTO_EVENT_LIVE_VOLUME,
  PROTO_EVENT_LIVE_MONEY,
  PROTO_EVENT_FINAL,
  PROTO_EVENT_TOTALIZER
} proto_event_kind_t;

typedef struct
{
  proto_event_kind_t kind;
  proto_status_t status;
  uint32_t volume_cl;
  uint32_t money;
  char transaction_id;
  uint8_t status_raw;
  uint8_t nozzle_raw;
} proto_event_t;

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_TYPES_H */
