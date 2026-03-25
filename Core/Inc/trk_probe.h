#ifndef __TRK_PROBE_H__
#define __TRK_PROBE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"
#include "keyboard.h"

typedef enum
{
  TRK_CHANNEL_OFFLINE = 0,
  TRK_CHANNEL_IDLE,
  TRK_CHANNEL_CALLING,
  TRK_CHANNEL_AUTH_WAIT,
  TRK_CHANNEL_STARTED,
  TRK_CHANNEL_PAUSED,
  TRK_CHANNEL_FUELLING,
  TRK_CHANNEL_FINISHING,
  TRK_CHANNEL_FINISHED_HOLD,
  TRK_CHANNEL_ERROR
} TrkChannelState;

typedef enum
{
  TRK_UI_MODE_MAIN = 0,
  TRK_UI_MODE_MENU,
  TRK_UI_MODE_EDIT_PRICE
} TrkUiMode;

typedef enum
{
  TRK_DISPENSE_MODE_MONEY = 0,
  TRK_DISPENSE_MODE_VOLUME,
  TRK_DISPENSE_MODE_FULL
} TrkDispenseMode;

typedef struct
{
  uint32_t tx_count;
  uint32_t rx_count;
  uint32_t ok_count;
  uint32_t crc_error_count;
  uint32_t timeout_count;
  uint8_t tx_busy;
  uint8_t waiting_reply;
  uint8_t online;
  uint8_t last_status;
  uint8_t last_nozzle;
  uint16_t last_rx_len;
  uint32_t last_rx_tick;
  uint8_t enabled;
  uint32_t price;
  char price_text[6];
  uint32_t preset_money;
  uint32_t preset_volume_cl;
  uint32_t live_money;
  uint32_t live_volume_cl;
  uint32_t final_money;
  uint32_t final_volume_cl;
  uint32_t totalizer_volume_cl;
  uint8_t channel_state;
  uint8_t dispense_mode;
  uint8_t ui_selected;
  uint8_t transaction_pending;
  uint8_t final_data_ready;
  uint8_t totalizer_ready;
  uint8_t totalizer_view;
  char transaction_id;
  char preset_edit_buf[10];
  char price_edit_buf[6];
  char last_ascii[20];
} TrkProbeChannelStatus;

typedef struct
{
  TrkProbeChannelStatus trk1;
  TrkProbeChannelStatus trk2;
  TrkProbeChannelStatus trk3;
  uint8_t active_ui_trk;
  uint8_t ui_mode;
  uint8_t menu_index;
  uint8_t pending_return_to_menu;
  uint32_t notice_until_ms;
  char notice[24];
} TrkProbeStatus;

void TrkProbe_Init(void);
void TrkProbe_Task(void);
void TrkProbe_HandleKey(const KeyboardEvent *event);
void TrkProbe_OnRxEvent(UART_HandleTypeDef *huart, uint16_t size);
void TrkProbe_OnTxCplt(UART_HandleTypeDef *huart);
const TrkProbeStatus *TrkProbe_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __TRK_PROBE_H__ */
