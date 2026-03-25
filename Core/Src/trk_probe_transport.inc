static void TrkProbe_HandleRxFrame(TrkProbeChannel *channel, const uint8_t *data, uint16_t len);

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
    TrkProbe_FsmOnCommFailure(channel);
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

void TrkProbe_RestartRx(TrkProbeChannel *channel)
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

void TrkProbe_SyncPublicStatus(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  *TrkProbe_PublicStatus(channel->trk_id) = channel->status;
}

static uint8_t TrkProbe_SendFrame(TrkProbeChannel *channel, const frame_t *frame)
{
  if ((channel == NULL) || (channel->huart == NULL) || (frame == NULL) || (frame->len == 0U))
  {
    return 0U;
  }

  if ((TrkProbe_IsActive(channel->trk_id) == 0U) ||
      (TrkProbe_IsEnabled(channel) == 0U) ||
      (frame->len > (uint8_t)sizeof(channel->tx_buf)))
  {
    return 0U;
  }

  memcpy(channel->tx_buf, frame->buf, frame->len);

  if (HAL_UART_Transmit_DMA(channel->huart, channel->tx_buf, frame->len) != HAL_OK)
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
                    frame->len);
  TrkProbe_SyncPublicStatus(channel);
  return 1U;
}

static uint8_t TrkProbe_SendSimpleCommand(TrkProbeChannel *channel, char command)
{
  frame_t frame;

  if ((channel == NULL) || (channel->protocol_vtable == NULL))
  {
    return 0U;
  }

  switch (command)
  {
    case 'S':
      if (channel->protocol_vtable->build_status_poll == NULL)
      {
        return 0U;
      }
      if (channel->protocol_vtable->build_status_poll(channel->proto_ctx, &frame) == 0U)
      {
        return 0U;
      }
      break;

    case 'L':
      if (channel->protocol_vtable->build_live_volume_request == NULL)
      {
        return 0U;
      }
      if (channel->protocol_vtable->build_live_volume_request(channel->proto_ctx, &frame) == 0U)
      {
        return 0U;
      }
      break;

    case 'R':
      if (channel->protocol_vtable->build_live_money_request == NULL)
      {
        return 0U;
      }
      if (channel->protocol_vtable->build_live_money_request(channel->proto_ctx, &frame) == 0U)
      {
        return 0U;
      }
      break;

    case 'T':
      if (channel->protocol_vtable->build_final_request == NULL)
      {
        return 0U;
      }
      if (channel->protocol_vtable->build_final_request(channel->proto_ctx, &frame) == 0U)
      {
        return 0U;
      }
      break;

    case 'N':
      if (channel->protocol_vtable->build_close_transaction == NULL)
      {
        return 0U;
      }
      if (channel->protocol_vtable->build_close_transaction(channel->proto_ctx, &frame) == 0U)
      {
        return 0U;
      }
      break;

    default:
      return 0U;
  }

  return TrkProbe_SendFrame(channel, &frame);
}

static void TrkProbe_SendPoll(TrkProbeChannel *channel)
{
  (void)TrkProbe_SendSimpleCommand(channel, 'S');
}

uint8_t TrkProbe_StartTransaction(TrkProbeChannel *channel)
{
  frame_t frame;

  if ((channel == NULL) || (channel->protocol_vtable == NULL))
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

  if ((TrkDispenseMode)channel->status.dispense_mode == TRK_DISPENSE_MODE_MONEY)
  {
    if ((channel->status.preset_money == 0U) ||
        (channel->protocol_vtable->build_start_money == NULL))
    {
      return 0U;
    }

    if (channel->protocol_vtable->build_start_money(channel->proto_ctx,
                                                    channel->status.preset_money,
                                                    channel->status.price,
                                                    &frame) == 0U)
    {
      return 0U;
    }
  }
  else
  {
    if ((channel->status.preset_volume_cl == 0U) ||
        (channel->protocol_vtable->build_start_volume == NULL))
    {
      return 0U;
    }

    if (channel->protocol_vtable->build_start_volume(channel->proto_ctx,
                                                     channel->status.preset_volume_cl,
                                                     channel->status.price,
                                                     &frame) == 0U)
    {
      return 0U;
    }
  }

  if (TrkProbe_SendFrame(channel, &frame) == 0U)
  {
    return 0U;
  }
  channel->status.transaction_pending = 1U;
  channel->live_poll_phase = 0U;
  channel->final_request_sent = 0U;
  channel->close_request_sent = 0U;
  channel->status.live_money = 0U;
  channel->status.live_volume_cl = 0U;
  TrkProbe_ClearFinalDisplay(channel);
  channel->status.preset_edit_buf[0] = '\0';
  return 1U;
}

static void TrkProbe_HandleRxFrame(TrkProbeChannel *channel, const uint8_t *data, uint16_t len)
{
  proto_event_t event;

  if ((channel == NULL) || (data == NULL) || (len < 5U))
  {
    if (channel != NULL)
    {
      ++channel->status.crc_error_count;
      channel->status.waiting_reply = 0U;
      TrkProbe_FsmOnCommFailure(channel);
    }
    return;
  }

  ++channel->status.rx_count;
  channel->status.last_rx_len = len;
  channel->status.last_rx_tick = HAL_GetTick();
  TrkProbe_SetAscii(&channel->status, data, len);
  AppLog_ProtoFrame(channel->trk_id, APP_LOG_PROTO_DIR_RX, data, len);

  if ((channel->protocol_vtable == NULL) ||
      (channel->protocol_vtable->parse_response == NULL) ||
      (channel->protocol_vtable->parse_response(channel->proto_ctx, data, len, &event) == 0U))
  {
    ++channel->status.crc_error_count;
    channel->status.waiting_reply = 0U;
    TrkProbe_FsmOnCommFailure(channel);
    return;
  }

  channel->status.waiting_reply = 0U;
  TrkProbe_FsmApplyProtoEvent(channel, &event);
}

void TrkProbe_Init(void)
{
  uint32_t i;

  TrkProbe_LoadDefaults();
  (void)TrkProbe_LoadNvConfig();
  TrkProbe_NormalizeActiveSelection();
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
      TrkProbe_FsmOnCommFailure(channel);
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
        ((now - channel->last_poll_tick) >= TrkProbe_FsmGetPollInterval(channel)))
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
        if ((channel->status.final_data_ready == 0U) &&
            (channel->final_request_sent == 0U))
        {
          sent = TrkProbe_SendSimpleCommand(channel, 'T');
          if (sent != 0U)
          {
            channel->final_request_sent = 1U;
          }
        }
        else if (channel->close_request_sent == 0U)
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
