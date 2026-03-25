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
        TrkProbe_ClearHeldTransactionDisplay(channel);
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

        TrkProbe_ClearHeldTransactionDisplay(channel);
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
        TrkProbe_ClearHeldTransactionDisplay(channel);
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
