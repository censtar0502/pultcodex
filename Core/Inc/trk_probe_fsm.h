#ifndef TRK_PROBE_FSM_H
#define TRK_PROBE_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "trk_probe_internal.h"

uint32_t TrkProbe_FsmGetPollInterval(const TrkProbeChannel *channel);
void TrkProbe_FsmOnCommFailure(TrkProbeChannel *channel);
void TrkProbe_FsmApplyProtoEvent(TrkProbeChannel *channel, const proto_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* TRK_PROBE_FSM_H */
