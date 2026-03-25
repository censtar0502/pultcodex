#include "trk_probe_internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "eeprom_at24.h"
/* TRK1=USART3(PD8/PD9), TRK2=USART1(PB6/PB7). */
#define TRK_PROBE_ACTIVE_MASK      (TRK_PROBE_MASK_TRK1 | TRK_PROBE_MASK_TRK2)

TrkProbeChannel trk_channels[TRK_PROBE_NUM_CHANNELS];
TrkProbeStatus trk_probe_status;

static uint8_t TrkProbe_ParsePriceText(const char *src, uint32_t *price_raw_out);
static uint8_t TrkProbe_ParseMoneyPresetText(const char *src, uint32_t *money_out);
static uint8_t TrkProbe_ParseVolumePresetText(const char *src, uint32_t *volume_cl_out);
static uint8_t TrkProbe_ParseAdminPinText(const char *src);
static uint32_t TrkProbe_Crc32(const void *data, uint32_t len);
static void TrkProbe_ApplyNvConfig(const TrkProbeNvConfig *config);
static void TrkProbe_SetDefaultAdminPin(void);

static char trk_probe_admin_pin[9];
static const char trk_probe_master_pin[] = "88991122";

uint8_t TrkProbe_IsActive(uint8_t trk_id)
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

void TrkProbe_SetAscii(TrkProbeChannelStatus *status, const uint8_t *data, uint16_t len)
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

void TrkProbe_SetPriceValue(TrkProbeChannel *channel, uint32_t price_raw, const char *price_text)
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

void TrkProbe_ClearPreset(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->status.preset_money = 0U;
  channel->status.preset_volume_cl = 0U;
  channel->status.preset_edit_buf[0] = '\0';
}

void TrkProbe_ClearFinalDisplay(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->status.final_data_ready = 0U;
  channel->status.final_money = 0U;
  channel->status.final_volume_cl = 0U;
  channel->status.transaction_id = '\0';
}

void TrkProbe_ClearHeldTransactionDisplay(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  if ((channel->status.final_data_ready != 0U) ||
      (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FINISHING) ||
      (channel->status.channel_state == (uint8_t)TRK_CHANNEL_FINISHED_HOLD))
  {
    TrkProbe_ResetTransactionRuntime(channel);
    TrkProbe_ClearFinalDisplay(channel);
    TrkProbe_ClearPreset(channel);
  }
}

void TrkProbe_ResetTransactionRuntime(TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return;
  }

  channel->live_poll_phase = 0U;
  channel->final_request_sent = 0U;
  channel->close_request_sent = 0U;
  channel->status.transaction_pending = 0U;
  channel->status.live_money = 0U;
  channel->status.live_volume_cl = 0U;
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

static uint8_t TrkProbe_ParseAdminPinText(const char *src)
{
  uint32_t len = 0U;

  if (src == NULL)
  {
    return 0U;
  }

  while (*src != '\0')
  {
    if ((*src < '0') || (*src > '9'))
    {
      return 0U;
    }

    ++len;
    if (len > 8U)
    {
      return 0U;
    }
    ++src;
  }

  return (len >= 4U) ? 1U : 0U;
}

uint8_t TrkProbe_ParsePriceEditValue(const TrkProbeChannel *channel, uint32_t *price_raw_out)
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

void TrkProbe_UpdateFullTankPreset(TrkProbeChannel *channel)
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

uint8_t TrkProbe_UpdatePresetFromBuffer(TrkProbeChannel *channel)
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

void TrkProbe_RecalculatePreset(TrkProbeChannel *channel)
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

void TrkProbe_LoadDefaults(void)
{
  memset(&trk_channels, 0, sizeof(trk_channels));
  memset(&trk_probe_status, 0, sizeof(trk_probe_status));
  TrkProbe_SetDefaultAdminPin();

  trk_channels[0].huart = &huart3;
  trk_channels[0].trk_id = 1U;
  trk_channels[0].addr_hi = 0x00U;
  trk_channels[0].addr_lo = 0x01U;
  trk_channels[0].protocol_vtable = &gaskitlink_vtable;
  trk_channels[0].gaskitlink_ctx.addr_hi = trk_channels[0].addr_hi;
  trk_channels[0].gaskitlink_ctx.addr_lo = trk_channels[0].addr_lo;
  trk_channels[0].proto_ctx = &trk_channels[0].gaskitlink_ctx;
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
  trk_channels[1].protocol_vtable = &gaskitlink_vtable;
  trk_channels[1].gaskitlink_ctx.addr_hi = trk_channels[1].addr_hi;
  trk_channels[1].gaskitlink_ctx.addr_lo = trk_channels[1].addr_lo;
  trk_channels[1].proto_ctx = &trk_channels[1].gaskitlink_ctx;
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
  trk_probe_status.admin_menu_index = 0U;
  trk_probe_status.admin_pin_change_mode = 0U;
  trk_probe_status.pending_return_to_menu = 0U;
  trk_probe_status.admin_pin_edit_buf[0] = '\0';
}

static void TrkProbe_SetDefaultAdminPin(void)
{
  (void)snprintf(trk_probe_admin_pin,
                 sizeof(trk_probe_admin_pin),
                 "%s",
                 "123456");
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

  if (TrkProbe_ParseAdminPinText(config->admin_pin) != 0U)
  {
    (void)snprintf(trk_probe_admin_pin,
                   sizeof(trk_probe_admin_pin),
                   "%s",
                   config->admin_pin);
  }
  else
  {
    TrkProbe_SetDefaultAdminPin();
  }

  TrkProbe_NormalizeActiveSelection();
}

uint8_t TrkProbe_LoadNvConfig(void)
{
  TrkProbeNvHeader header;
  TrkProbeNvConfig config;
  TrkProbeNvConfigV2 config_v2;
  uint32_t crc_expected;
  uint32_t i;

  if (AT24_DetectAddress() != HAL_OK)
  {
    return 0U;
  }

  if (AT24_Read(TRK_PROBE_NV_ADDR,
                (uint8_t *)&header,
                (uint16_t)sizeof(header),
                TRK_PROBE_NV_TIMEOUT_MS) != HAL_OK)
  {
    return 0U;
  }

  if (header.magic != TRK_PROBE_NV_MAGIC)
  {
    return 0U;
  }

  if ((header.version == TRK_PROBE_NV_VERSION) &&
      (header.size == (uint16_t)sizeof(config)))
  {
    if (AT24_Read(TRK_PROBE_NV_ADDR,
                  (uint8_t *)&config,
                  (uint16_t)sizeof(config),
                  TRK_PROBE_NV_TIMEOUT_MS) != HAL_OK)
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

  if ((header.version == 2U) &&
      (header.size == (uint16_t)sizeof(config_v2)))
  {
    if (AT24_Read(TRK_PROBE_NV_ADDR,
                  (uint8_t *)&config_v2,
                  (uint16_t)sizeof(config_v2),
                  TRK_PROBE_NV_TIMEOUT_MS) != HAL_OK)
    {
      return 0U;
    }

    crc_expected = TrkProbe_Crc32(&config_v2,
                                  (uint32_t)(sizeof(config_v2) - sizeof(config_v2.crc32)));
    if (crc_expected != config_v2.crc32)
    {
      return 0U;
    }

    for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
    {
      trk_channels[i].status.enabled = (config_v2.channels[i].enabled != 0U) ? 1U : 0U;

      {
        uint32_t price_raw;

        if (TrkProbe_ParsePriceText(config_v2.channels[i].price_text, &price_raw) != 0U)
        {
          TrkProbe_SetPriceValue(&trk_channels[i],
                                 price_raw,
                                 config_v2.channels[i].price_text);
        }
      }

      if (config_v2.channels[i].dispense_mode <= (uint8_t)TRK_DISPENSE_MODE_FULL)
      {
        trk_channels[i].status.dispense_mode = config_v2.channels[i].dispense_mode;
        if ((TrkDispenseMode)trk_channels[i].status.dispense_mode == TRK_DISPENSE_MODE_FULL)
        {
          TrkProbe_UpdateFullTankPreset(&trk_channels[i]);
        }
      }
    }

    TrkProbe_SetDefaultAdminPin();
    TrkProbe_NormalizeActiveSelection();
    return 1U;
  }

  return 0U;
}

uint8_t TrkProbe_SaveNvConfig(void)
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
  (void)snprintf(config.admin_pin,
                 sizeof(config.admin_pin),
                 "%s",
                 trk_probe_admin_pin);

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

uint8_t TrkProbe_IsValidAdminPin(const char *pin_text)
{
  return TrkProbe_ParseAdminPinText(pin_text);
}

uint8_t TrkProbe_CheckAdminPin(const char *pin_text)
{
  if (TrkProbe_ParseAdminPinText(pin_text) == 0U)
  {
    return 0U;
  }

  if (strcmp(pin_text, trk_probe_admin_pin) == 0)
  {
    return 1U;
  }

  if (strcmp(pin_text, trk_probe_master_pin) == 0)
  {
    return 1U;
  }

  return 0U;
}

uint8_t TrkProbe_SetAdminPin(const char *pin_text)
{
  if (TrkProbe_ParseAdminPinText(pin_text) == 0U)
  {
    return 0U;
  }

  (void)snprintf(trk_probe_admin_pin,
                 sizeof(trk_probe_admin_pin),
                 "%s",
                 pin_text);
  return TrkProbe_SaveNvConfig();
}

void TrkProbe_LogPrice(uint8_t trk_id, uint32_t price)
{
  char price_text[12];
  char message[48];
  static const char *const trk_labels[] = {"TRK1", "TRK2", "TRK3"};
  uint8_t idx = (trk_id >= 1U && trk_id <= 3U) ? (trk_id - 1U) : 0U;

  (void)snprintf(price_text, sizeof(price_text), "%04lu", (unsigned long)price);
  (void)snprintf(message, sizeof(message), "price_raw=%s", price_text);
  AppLog_Message(APP_LOG_LEVEL_INFO, trk_labels[idx], message);
}

uint8_t TrkProbe_IsEnabled(const TrkProbeChannel *channel)
{
  if (channel == NULL)
  {
    return 0U;
  }

  return (channel->status.enabled != 0U) ? 1U : 0U;
}

void TrkProbe_RefreshUiFlags(void)
{
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    trk_channels[i].status.ui_selected =
      (trk_probe_status.active_ui_trk == trk_channels[i].trk_id) ? 1U : 0U;
  }
}

void TrkProbe_NormalizeActiveSelection(void)
{
  TrkProbeChannel *active = TrkProbe_GetChannelByTrkId(trk_probe_status.active_ui_trk);
  uint32_t i;

  if ((active != NULL) && (TrkProbe_IsEnabled(active) != 0U))
  {
    return;
  }

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    if (TrkProbe_IsEnabled(&trk_channels[i]) != 0U)
    {
      trk_probe_status.active_ui_trk = trk_channels[i].trk_id;
      return;
    }
  }

  trk_probe_status.active_ui_trk = trk_channels[0].trk_id;
}

TrkProbeChannel *TrkProbe_GetActiveUiChannel(void)
{
  uint32_t i;

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    if (trk_channels[i].trk_id == trk_probe_status.active_ui_trk)
    {
      if (TrkProbe_IsEnabled(&trk_channels[i]) != 0U)
      {
        return &trk_channels[i];
      }
      break;
    }
  }

  for (i = 0U; i < TRK_PROBE_NUM_CHANNELS; ++i)
  {
    if (TrkProbe_IsEnabled(&trk_channels[i]) != 0U)
    {
      return &trk_channels[i];
    }
  }

  return &trk_channels[0];
}

TrkProbeChannel *TrkProbe_GetChannelByTrkId(uint8_t trk_id)
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
