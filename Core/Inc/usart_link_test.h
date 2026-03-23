#ifndef __USART_LINK_TEST_H__
#define __USART_LINK_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"

typedef struct
{
  uint32_t tx1_count;
  uint32_t tx3_count;
  uint32_t rx1_count;
  uint32_t rx3_count;
  uint32_t rx1_ok_count;
  uint32_t rx3_ok_count;
  uint32_t error_count;
  uint8_t tx1_busy;
  uint8_t tx3_busy;
  char last_rx1[24];
  char last_rx3[24];
} UsartLinkTestStatus;

void UsartLinkTest_Init(void);
void UsartLinkTest_Task(void);
void UsartLinkTest_OnRxEvent(UART_HandleTypeDef *huart, uint16_t size);
void UsartLinkTest_OnTxCplt(UART_HandleTypeDef *huart);
const UsartLinkTestStatus *UsartLinkTest_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_LINK_TEST_H__ */
