#ifndef TRK_PROBE_INTERNAL_H
#define TRK_PROBE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "trk_probe.h"
#include "dispenser_protocol.h"
#include "proto_gaskitlink.h"
#include "usart.h"

#define TRK_PROBE_RX_BUF_SIZE      64U
#define TRK_PROBE_POLL_MS          300UL
#define TRK_PROBE_ACTIVE_POLL_MS   120UL
#define TRK_PROBE_TIMEOUT_MS       150UL
#define TRK_PROBE_FRAME_LEN        18U
#define TRK_PROBE_MASK_TRK1        0x01U
#define TRK_PROBE_MASK_TRK2        0x02U
#define TRK_PROBE_MASK_TRK3        0x04U
#define TRK_PROBE_MASK_ALL         0x07U

#define TRK_PROBE_NUM_CHANNELS     2U
#define TRK_PROBE_USER_MENU_ITEMS  5U
#define TRK_PROBE_ADMIN_MENU_ITEMS 3U
#define TRK_PROBE_OFFLINE_AFTER_MISSES 5U
#define TRK_PROBE_PRESET_MONEY_MAX 999999UL
#define TRK_PROBE_PRESET_VOLUME_CL_MAX 90000UL
#define TRK_PROBE_NV_MAGIC         0x54524B50UL
#define TRK_PROBE_NV_VERSION       3U
#define TRK_PROBE_NV_ADDR          0x0000U
#define TRK_PROBE_NV_TIMEOUT_MS    20U

typedef struct
{
  UART_HandleTypeDef *huart;
  uint8_t trk_id;
  uint8_t addr_hi;
  uint8_t addr_lo;
  const dispenser_protocol_vtable_t *protocol_vtable;
  void *proto_ctx;
  GasKitLinkProtoCtx gaskitlink_ctx;
  uint8_t rx_buf[TRK_PROBE_RX_BUF_SIZE];
  uint8_t frame_buf[8];
  uint8_t frame_len;
  uint8_t tx_buf[TRK_PROBE_FRAME_LEN];
  uint8_t comm_fail_streak;
  uint8_t live_poll_phase;
  uint8_t final_request_sent;
  uint8_t close_request_sent;
  uint32_t last_poll_tick;
  uint32_t last_tx_tick;
  TrkProbeChannelStatus status;
} TrkProbeChannel;

typedef struct
{
  uint8_t enabled;
  uint8_t dispense_mode;
  uint8_t reserved0;
  uint8_t reserved1;
  char price_text[6];
} TrkProbeNvChannel;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
} TrkProbeNvHeader;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  TrkProbeNvChannel channels[TRK_PROBE_NUM_CHANNELS];
  uint32_t crc32;
} TrkProbeNvConfigV2;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  TrkProbeNvChannel channels[TRK_PROBE_NUM_CHANNELS];
  char admin_pin[9];
  uint32_t crc32;
} TrkProbeNvConfig;

extern TrkProbeChannel trk_channels[TRK_PROBE_NUM_CHANNELS];
extern TrkProbeStatus trk_probe_status;

uint8_t TrkProbe_IsActive(uint8_t trk_id);
void TrkProbe_SetPriceValue(TrkProbeChannel *channel, uint32_t price_raw, const char *price_text);
void TrkProbe_SetAscii(TrkProbeChannelStatus *status, const uint8_t *data, uint16_t len);
void TrkProbe_ClearPreset(TrkProbeChannel *channel);
void TrkProbe_ClearFinalDisplay(TrkProbeChannel *channel);
void TrkProbe_ClearHeldTransactionDisplay(TrkProbeChannel *channel);
void TrkProbe_ResetTransactionRuntime(TrkProbeChannel *channel);
uint8_t TrkProbe_ParsePriceEditValue(const TrkProbeChannel *channel, uint32_t *price_raw_out);
void TrkProbe_UpdateFullTankPreset(TrkProbeChannel *channel);
uint8_t TrkProbe_UpdatePresetFromBuffer(TrkProbeChannel *channel);
void TrkProbe_RecalculatePreset(TrkProbeChannel *channel);
void TrkProbe_LoadDefaults(void);
uint8_t TrkProbe_LoadNvConfig(void);
uint8_t TrkProbe_SaveNvConfig(void);
uint8_t TrkProbe_IsValidAdminPin(const char *pin_text);
uint8_t TrkProbe_CheckAdminPin(const char *pin_text);
uint8_t TrkProbe_SetAdminPin(const char *pin_text);
void TrkProbe_LogPrice(uint8_t trk_id, uint32_t price);
uint8_t TrkProbe_IsEnabled(const TrkProbeChannel *channel);
void TrkProbe_RefreshUiFlags(void);
void TrkProbe_NormalizeActiveSelection(void);
TrkProbeChannel *TrkProbe_GetActiveUiChannel(void);
TrkProbeChannel *TrkProbe_GetChannelByTrkId(uint8_t trk_id);

void TrkProbe_RestartRx(TrkProbeChannel *channel);
void TrkProbe_SyncPublicStatus(TrkProbeChannel *channel);
uint8_t TrkProbe_StartTransaction(TrkProbeChannel *channel);
uint8_t TrkProbe_RequestTotalizer(TrkProbeChannel *channel);

#ifdef __cplusplus
}
#endif

#endif /* TRK_PROBE_INTERNAL_H */
