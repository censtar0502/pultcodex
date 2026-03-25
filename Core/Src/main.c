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
#include "trk_probe.h"

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
static void ShowBootScreen(void);
static void ShowTrkProbeStatus(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void ShowBootScreen(void)
{
  SSD1322_Clear(0x00U);
  SSD1322_DrawString8x8(8U, 8U, "PULTCODEX", 0x0FU);
  SSD1322_DrawString8x8(8U, 24U, "IDLE FSM START", 0x0FU);
  SSD1322_DrawString8x8(8U, 40U, "WAITING FOR STATUS", 0x0FU);
  SSD1322_Flush();
}

static const char *ChannelMainText(const TrkProbeChannelStatus *ch)
{
  if (ch->enabled == 0U)
  {
    return "Off";
  }

  if (ch->online == 0U)
  {
    return "Not Connect!";
  }

  switch ((TrkDispenseMode)ch->dispense_mode)
  {
    case TRK_DISPENSE_MODE_VOLUME:
      return "L:0";
    case TRK_DISPENSE_MODE_FULL:
      return "H";
    case TRK_DISPENSE_MODE_MONEY:
    default:
      return "A:0";
  }
}

static void FormatPriceText(uint32_t price_tenths, char *buf, size_t buf_size)
{
  if ((buf == NULL) || (buf_size == 0U))
  {
    return;
  }

  if ((price_tenths % 10U) == 0U)
  {
    (void)snprintf(buf, buf_size, "%lu", (unsigned long)(price_tenths / 10U));
  }
  else
  {
    (void)snprintf(buf,
                   buf_size,
                   "%lu.%lu",
                   (unsigned long)(price_tenths / 10U),
                   (unsigned long)(price_tenths % 10U));
  }
}

static void ShowMainChannelPanel(uint8_t x, const char *label,
                                 const TrkProbeChannelStatus *ch)
{
  char line[24];
  const char *main_text = ChannelMainText(ch);

  (void)snprintf(line, sizeof(line), "%c%s",
                 (ch->ui_selected != 0U) ? '>' : ' ',
                 label);
  SSD1322_DrawString8x8(x, 0U, line, 0x0FU);

  if ((strcmp(main_text, "A:0") == 0) ||
      (strcmp(main_text, "L:0") == 0) ||
      (strcmp(main_text, "H") == 0) ||
      (strcmp(main_text, "Off") == 0))
  {
    SSD1322_DrawString16x16(x, 20U, main_text, 0x0FU);
  }
  else
  {
    SSD1322_DrawString8x8(x, 24U, main_text, 0x0FU);
  }
}

static void ShowTrkProbeStatus(void)
{
  const TrkProbeStatus *status = TrkProbe_GetStatus();
  static TrkProbeStatus last_drawn_status;
  static uint32_t last_draw_ms;
  char line[42];
  uint32_t now = HAL_GetTick();

  if ((memcmp(&last_drawn_status, status, sizeof(last_drawn_status)) == 0) &&
      ((now - last_draw_ms) < 250U))
  {
    return;
  }

  last_drawn_status = *status;
  last_draw_ms = now;

  SSD1322_Clear(0x00U);

  if (status->ui_mode == (uint8_t)TRK_UI_MODE_MAIN)
  {
    ShowMainChannelPanel(8U, "TRK1", &status->trk1);
    ShowMainChannelPanel(136U, "TRK2", &status->trk2);
  }
  else if (status->ui_mode == (uint8_t)TRK_UI_MODE_MENU)
  {
    SSD1322_DrawString8x8(8U, 0U, "SETUP MENU", 0x0FU);
    (void)snprintf(line, sizeof(line), "%c TRK1 %s",
                   (status->menu_index == 0U) ? '>' : ' ',
                   (status->trk1.enabled != 0U) ? "ON" : "OFF");
    SSD1322_DrawString8x8(8U, 16U, line, 0x0FU);
    (void)snprintf(line, sizeof(line), "%c TRK2 %s",
                   (status->menu_index == 1U) ? '>' : ' ',
                   (status->trk2.enabled != 0U) ? "ON" : "OFF");
    SSD1322_DrawString8x8(8U, 28U, line, 0x0FU);
    (void)snprintf(line, sizeof(line), "%c TRK1 PRICE",
                   (status->menu_index == 2U) ? '>' : ' ');
    SSD1322_DrawString8x8(8U, 40U, line, 0x0FU);
    (void)snprintf(line, sizeof(line), "%c TRK2 PRICE",
                   (status->menu_index == 3U) ? '>' : ' ');
    SSD1322_DrawString8x8(8U, 52U, line, 0x0FU);
  }
  else
  {
    const TrkProbeChannelStatus *edit_ch =
      (status->active_ui_trk == 2U) ? &status->trk2 : &status->trk1;
    char old_price[12];

    SSD1322_DrawString8x8(8U, 0U, "EDIT PRICE", 0x0FU);
    (void)snprintf(line, sizeof(line), "TRK%u PRICE",
                   (unsigned int)status->active_ui_trk);
    SSD1322_DrawString8x8(8U, 18U, line, 0x0FU);
    FormatPriceText(edit_ch->price, old_price, sizeof(old_price));
    (void)snprintf(line, sizeof(line), "OLD:%s", old_price);
    SSD1322_DrawString8x8(8U, 30U, line, 0x0FU);
    (void)snprintf(line, sizeof(line), "NEW:%s",
                   (edit_ch->price_edit_buf[0] != '\0') ? edit_ch->price_edit_buf : "_");
    SSD1322_DrawString8x8(8U, 42U, line, 0x0FU);
    if (status->notice[0] != '\0')
    {
      SSD1322_DrawString8x8(120U, 0U, status->notice, 0x0FU);
    }
    SSD1322_DrawString8x8(8U, 52U, "K SAVE  E CLR", 0x0FU);
  }

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
  TrkProbe_Init();
  ShowBootScreen();
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
        TrkProbe_HandleKey(&event);
      }
    }

    TrkProbe_Task();
    ShowTrkProbeStatus();
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
  TrkProbe_OnRxEvent(huart, size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  TrkProbe_OnTxCplt(huart);
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
