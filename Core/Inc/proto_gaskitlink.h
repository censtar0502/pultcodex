#ifndef PROTO_GASKITLINK_H
#define PROTO_GASKITLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dispenser_protocol.h"

typedef struct
{
  uint8_t addr_hi;
  uint8_t addr_lo;
} GasKitLinkProtoCtx;

extern const dispenser_protocol_vtable_t gaskitlink_vtable;

#ifdef __cplusplus
}
#endif

#endif /* PROTO_GASKITLINK_H */
