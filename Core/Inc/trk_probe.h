#ifndef __TRK_PROBE_H__
#define __TRK_PROBE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"

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
  char last_ascii[20];
} TrkProbeChannelStatus;

typedef struct
{
  TrkProbeChannelStatus trk1;
  TrkProbeChannelStatus trk2;
} TrkProbeStatus;

void TrkProbe_Init(void);
void TrkProbe_Task(void);
void TrkProbe_OnRxEvent(UART_HandleTypeDef *huart, uint16_t size);
void TrkProbe_OnTxCplt(UART_HandleTypeDef *huart);
const TrkProbeStatus *TrkProbe_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __TRK_PROBE_H__ */
