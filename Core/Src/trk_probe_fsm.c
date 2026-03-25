#include "trk_probe_fsm.h"

static TrkChannelState TrkProbe_FsmMapProtoStatus(proto_status_t status)
{
  switch (status)
  {
    case PROTO_STATUS_IDLE:
      return TRK_CHANNEL_IDLE;
    case PROTO_STATUS_CALLING:
      return TRK_CHANNEL_CALLING;
    case PROTO_STATUS_AUTH_WAIT:
      return TRK_CHANNEL_AUTH_WAIT;
    case PROTO_STATUS_STARTED:
      return TRK_CHANNEL_STARTED;
    case PROTO_STATUS_PAUSED:
      return TRK_CHANNEL_PAUSED;
    case PROTO_STATUS_FUELLING:
      return TRK_CHANNEL_FUELLING;
    case PROTO_STATUS_FINISHING:
      return TRK_CHANNEL_FINISHING;
    case PROTO_STATUS_FINISHED_HOLD:
      return TRK_CHANNEL_FINISHED_HOLD;
    case PROTO_STATUS_ERROR:
    case PROTO_STATUS_UNKNOWN:
    default:
      return TRK_CHANNEL_ERROR;
  }
}

uint32_t TrkProbe_FsmGetPollInterval(const TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return TRK_PROBE_POLL_MS;
  }

  if ((channel->status.channel_state == (uint8_t)TRK_CHANNEL_STARTED) ||
      (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FUELLING) ||
      (channel->status.channel_state == (uint8_t)TRK_CHANNEL_PAUSED) ||
      (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FINISHING) ||
      (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FINISHED_HOLD))
  {
    return TRK_PROBE_ACTIVE_POLL_MS;
  }

  return TRK_PROBE_POLL_MS;
}

static void TrkProbe_FsmRegisterCommSuccess(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->comm_fail_streak = 0U;
  channel->status.online = 1U;
}

void TrkProbe_FsmOnCommFailure(TrkProbeChannel *channel)
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

void TrkProbe_FsmApplyProtoEvent(TrkProbeChannel *channel, const proto_event_t *event)
{
  TrkChannelState mapped_state;

  if ((channel == NULL) || (event == NULL))
  {
    return;
  }

  mapped_state = TrkProbe_FsmMapProtoStatus(event->status);
  channel->status.last_status = event->status_raw;
  channel->status.last_nozzle = event->nozzle_raw;
  channel->status.channel_state = (uint8_t)mapped_state;

  if (event->transaction_id != '\0')
  {
    channel->status.transaction_id = event->transaction_id;
  }

  switch (event->kind)
  {
    case PROTO_EVENT_LIVE_VOLUME:
      channel->status.live_volume_cl = event->volume_cl;
      break;

    case PROTO_EVENT_LIVE_MONEY:
      channel->status.live_money = event->money;
      break;

    case PROTO_EVENT_FINAL:
      channel->status.final_money = event->money;
      channel->status.final_volume_cl = event->volume_cl;
      channel->status.final_data_ready = 1U;
      break;

    case PROTO_EVENT_STATUS:
    case PROTO_EVENT_NONE:
    default:
      break;
  }

  if (mapped_state == TRK_CHANNEL_IDLE)
  {
    TrkProbe_ResetTransactionRuntime(channel);
  }

  TrkProbe_FsmRegisterCommSuccess(channel);
  ++channel->status.ok_count;
}
