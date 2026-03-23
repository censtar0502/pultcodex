#include "usart_link_test.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "usart.h"

#define UTEST_RX_BUF_SIZE      64U
#define UTEST_TX_BUF_SIZE      32U
#define UTEST_PERIOD_MS        200UL

static uint8_t utest_rx1_buf[UTEST_RX_BUF_SIZE];
static uint8_t utest_rx3_buf[UTEST_RX_BUF_SIZE];
static uint8_t utest_tx1_buf[UTEST_TX_BUF_SIZE];
static uint8_t utest_tx3_buf[UTEST_TX_BUF_SIZE];
static uint32_t utest_last_tx_ms;
static UsartLinkTestStatus utest_status;

static void UsartLinkTest_StartRx(void)
{
  if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, utest_rx1_buf, sizeof(utest_rx1_buf)) != HAL_OK)
  {
    ++utest_status.error_count;
    AppLog_Message(APP_LOG_LEVEL_ERROR, "UTEST", "USART1 RX start failed");
  }
  else
  {
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
  }

  if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, utest_rx3_buf, sizeof(utest_rx3_buf)) != HAL_OK)
  {
    ++utest_status.error_count;
    AppLog_Message(APP_LOG_LEVEL_ERROR, "UTEST", "USART3 RX start failed");
  }
  else
  {
    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);
  }
}

static uint8_t UsartLinkTest_StartTx(UART_HandleTypeDef *huart, uint8_t *buf, uint16_t len)
{
  if (HAL_UART_Transmit_DMA(huart, buf, len) != HAL_OK)
  {
    ++utest_status.error_count;
    return 0U;
  }

  return 1U;
}

static uint8_t UsartLinkTest_IsExpected(const char *text, const char *prefix)
{
  if ((text == NULL) || (prefix == NULL))
  {
    return 0U;
  }

  return (strncmp(text, prefix, strlen(prefix)) == 0) ? 1U : 0U;
}

void UsartLinkTest_Init(void)
{
  memset(&utest_status, 0, sizeof(utest_status));
  memset(utest_rx1_buf, 0, sizeof(utest_rx1_buf));
  memset(utest_rx3_buf, 0, sizeof(utest_rx3_buf));
  memset(utest_tx1_buf, 0, sizeof(utest_tx1_buf));
  memset(utest_tx3_buf, 0, sizeof(utest_tx3_buf));
  strcpy(utest_status.last_rx1, "-");
  strcpy(utest_status.last_rx3, "-");
  utest_last_tx_ms = HAL_GetTick();
  UsartLinkTest_StartRx();
  AppLog_Message(APP_LOG_LEVEL_INFO, "UTEST", "USART loopback test ready, period=200ms");
}

void UsartLinkTest_Task(void)
{
  uint32_t now = HAL_GetTick();
  int len1;
  int len3;

  if ((now - utest_last_tx_ms) < UTEST_PERIOD_MS)
  {
    return;
  }

  if ((utest_status.tx1_busy != 0U) || (utest_status.tx3_busy != 0U))
  {
    return;
  }

  len1 = snprintf((char *)utest_tx1_buf, sizeof(utest_tx1_buf), "U1>%04lu\r\n", (unsigned long)(utest_status.tx1_count + 1UL));
  len3 = snprintf((char *)utest_tx3_buf, sizeof(utest_tx3_buf), "U3>%04lu\r\n", (unsigned long)(utest_status.tx3_count + 1UL));

  if ((len1 <= 0) || (len3 <= 0))
  {
    ++utest_status.error_count;
    return;
  }

  if (UsartLinkTest_StartTx(&huart1, utest_tx1_buf, (uint16_t)len1) != 0U)
  {
    utest_status.tx1_busy = 1U;
    ++utest_status.tx1_count;
    AppLog_ProtoFrame(2U, APP_LOG_PROTO_DIR_TX, utest_tx1_buf, (size_t)len1);
  }

  if (UsartLinkTest_StartTx(&huart3, utest_tx3_buf, (uint16_t)len3) != 0U)
  {
    utest_status.tx3_busy = 1U;
    ++utest_status.tx3_count;
    AppLog_ProtoFrame(1U, APP_LOG_PROTO_DIR_TX, utest_tx3_buf, (size_t)len3);
  }

  utest_last_tx_ms = now;
}

void UsartLinkTest_OnRxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  uint16_t copy_len;

  if (size == 0U)
  {
    return;
  }

  if (huart->Instance == USART1)
  {
    ++utest_status.rx1_count;
    copy_len = (size < (uint16_t)(sizeof(utest_status.last_rx1) - 1U)) ? size : (uint16_t)(sizeof(utest_status.last_rx1) - 1U);
    memcpy(utest_status.last_rx1, utest_rx1_buf, copy_len);
    utest_status.last_rx1[copy_len] = '\0';
    if (UsartLinkTest_IsExpected(utest_status.last_rx1, "U3>") != 0U)
    {
      ++utest_status.rx1_ok_count;
    }
    else
    {
      ++utest_status.error_count;
    }

    AppLog_ProtoFrame(2U, APP_LOG_PROTO_DIR_RX, utest_rx1_buf, size);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, utest_rx1_buf, sizeof(utest_rx1_buf));
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
  }
  else if (huart->Instance == USART3)
  {
    ++utest_status.rx3_count;
    copy_len = (size < (uint16_t)(sizeof(utest_status.last_rx3) - 1U)) ? size : (uint16_t)(sizeof(utest_status.last_rx3) - 1U);
    memcpy(utest_status.last_rx3, utest_rx3_buf, copy_len);
    utest_status.last_rx3[copy_len] = '\0';
    if (UsartLinkTest_IsExpected(utest_status.last_rx3, "U1>") != 0U)
    {
      ++utest_status.rx3_ok_count;
    }
    else
    {
      ++utest_status.error_count;
    }

    AppLog_ProtoFrame(1U, APP_LOG_PROTO_DIR_RX, utest_rx3_buf, size);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart3, utest_rx3_buf, sizeof(utest_rx3_buf));
    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);
  }
}

void UsartLinkTest_OnTxCplt(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    utest_status.tx1_busy = 0U;
  }
  else if (huart->Instance == USART3)
  {
    utest_status.tx3_busy = 0U;
  }
}

const UsartLinkTestStatus *UsartLinkTest_GetStatus(void)
{
  return &utest_status;
}
