static uint32_t TrkProbe_GetPollInterval(const TrkProbeChannel *channel)
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
  channel->live_poll_phase = 0U;
  channel->final_request_sent = 0U;
  channel->close_request_sent = 0U;
  channel->status.live_money = 0U;
  channel->status.live_volume_cl = 0U;
  TrkProbe_ClearFinalDisplay(channel);
  channel->status.preset_edit_buf[0] = '\0';
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
        ((now - channel->last_poll_tick) >= TrkProbe_GetPollInterval(channel)))
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
