/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "app_log.h"
#include "keyboard.h"
#include "ssd1322.h"
#include "usart_link_test.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void ShowKeyboardTestScreen(void);
static void ShowKeyboardEvent(const KeyboardEvent *event);
static void ShowUsartLinkStatus(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void ShowKeyboardTestScreen(void)
{
  SSD1322_Clear(0x00U);
  SSD1322_DrawString8x8(8U, 8U, "KEYBOARD TEST", 0x0FU);
  SSD1322_DrawString8x8(8U, 24U, "PRESS ANY KEY", 0x0FU);
  SSD1322_DrawString8x8(8U, 40U, "WAITING...", 0x0FU);
  SSD1322_Flush();
}

static void ShowKeyboardEvent(const KeyboardEvent *event)
{
  char key_line[20];

  SSD1322_Clear(0x00U);
  SSD1322_DrawString8x8(8U, 8U, "KEYBOARD TEST", 0x0FU);
  (void)snprintf(key_line, sizeof(key_line), "KEY: %c", event->key);
  SSD1322_DrawString8x8(8U, 24U, key_line, 0x0FU);
  SSD1322_DrawString8x8(8U, 40U, Keyboard_GetLegend(event->key), 0x0FU);
  SSD1322_DrawString8x8(144U, 24U, "STATE: DOWN", 0x0FU);

  SSD1322_Flush();
}

static void ShowUsartLinkStatus(void)
{
  const UsartLinkTestStatus *status = UsartLinkTest_GetStatus();
  static UsartLinkTestStatus last_drawn_status;
  static uint32_t last_draw_ms;
  char line[32];
  uint32_t now = HAL_GetTick();

  if ((memcmp(&last_drawn_status, status, sizeof(last_drawn_status)) == 0) &&
      ((now - last_draw_ms) < 250U))
  {
    return;
  }

  last_drawn_status = *status;
  last_draw_ms = now;

  SSD1322_Clear(0x00U);
  SSD1322_DrawString8x8(8U, 0U, "USART LOOP TEST", 0x0FU);

  (void)snprintf(line, sizeof(line), "U1 T%lu R%lu O%lu",
                 (unsigned long)status->tx1_count,
                 (unsigned long)status->rx1_count,
                 (unsigned long)status->rx1_ok_count);
  SSD1322_DrawString8x8(8U, 16U, line, 0x0FU);

  (void)snprintf(line, sizeof(line), "U3 T%lu R%lu O%lu",
                 (unsigned long)status->tx3_count,
                 (unsigned long)status->rx3_count,
                 (unsigned long)status->rx3_ok_count);
  SSD1322_DrawString8x8(8U, 28U, line, 0x0FU);

  (void)snprintf(line, sizeof(line), "E:%lu", (unsigned long)status->error_count);
  SSD1322_DrawString8x8(8U, 40U, line, 0x0FU);

  (void)snprintf(line, sizeof(line), "1<-%s", status->last_rx1);
  SSD1322_DrawString8x8(80U, 40U, line, 0x0FU);

  (void)snprintf(line, sizeof(line), "3<-%s", status->last_rx3);
  SSD1322_DrawString8x8(80U, 52U, line, 0x0FU);

  SSD1322_Flush();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_I2C2_Init();
  MX_SPI2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  if (HAL_TIM_Base_Start_IT(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_Base_Start_IT(&htim4) != HAL_OK)
  {
    Error_Handler();
  }

  SSD1322_Init();
  Keyboard_Init();
  AppLog_Init();
  UsartLinkTest_Init();
  ShowKeyboardTestScreen();
  AppLog_Message(APP_LOG_LEVEL_INFO, "BOOT", "PULTCODEX USB CDC READY");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    KeyboardEvent event;

    if (Keyboard_GetEvent(&event) != 0U)
    {
      AppLog_KeyEvent(&event);

      if (event.pressed != 0U)
      {
        ShowKeyboardEvent(&event);
      }
    }

    UsartLinkTest_Task();
    ShowUsartLinkStatus();
    AppLog_Task();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM4)
  {
    Keyboard_Task10ms();
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  UsartLinkTest_OnRxEvent(huart, size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  UsartLinkTest_OnTxCplt(huart);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
