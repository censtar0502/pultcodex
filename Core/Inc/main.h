/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PC14_OSC32_IN_Pin GPIO_PIN_14
#define PC14_OSC32_IN_GPIO_Port GPIOC
#define PC15_OSC32_OUT_Pin GPIO_PIN_15
#define PC15_OSC32_OUT_GPIO_Port GPIOC
#define PH0_OSC_IN_Pin GPIO_PIN_0
#define PH0_OSC_IN_GPIO_Port GPIOH
#define PH1_OSC_OUT_Pin GPIO_PIN_1
#define PH1_OSC_OUT_GPIO_Port GPIOH
#define B1_Pin GPIO_PIN_0
#define B1_GPIO_Port GPIOA
#define DBG_USART2_TX_Pin GPIO_PIN_2
#define DBG_USART2_TX_GPIO_Port GPIOA
#define DBG_USART2_RX_Pin GPIO_PIN_3
#define DBG_USART2_RX_GPIO_Port GPIOA
#define KBD_ROW1_Pin GPIO_PIN_7
#define KBD_ROW1_GPIO_Port GPIOE
#define KBD_ROW2_Pin GPIO_PIN_8
#define KBD_ROW2_GPIO_Port GPIOE
#define KBD_ROW3_Pin GPIO_PIN_9
#define KBD_ROW3_GPIO_Port GPIOE
#define KBD_ROW4_Pin GPIO_PIN_10
#define KBD_ROW4_GPIO_Port GPIOE
#define KBD_COL1_Pin GPIO_PIN_11
#define KBD_COL1_GPIO_Port GPIOE
#define KBD_COL2_Pin GPIO_PIN_12
#define KBD_COL2_GPIO_Port GPIOE
#define KBD_COL3_Pin GPIO_PIN_13
#define KBD_COL3_GPIO_Port GPIOE
#define KBD_COL4_Pin GPIO_PIN_14
#define KBD_COL4_GPIO_Port GPIOE
#define KBD_COL5_RES_Pin GPIO_PIN_15
#define KBD_COL5_RES_GPIO_Port GPIOE
#define EEPROM_I2C2_SCL_Pin GPIO_PIN_10
#define EEPROM_I2C2_SCL_GPIO_Port GPIOB
#define EEPROM_I2C2_SDA_Pin GPIO_PIN_11
#define EEPROM_I2C2_SDA_GPIO_Port GPIOB
#define OLED_SPI2_SCK_Pin GPIO_PIN_13
#define OLED_SPI2_SCK_GPIO_Port GPIOB
#define OLED_SPI2_MOSI_Pin GPIO_PIN_15
#define OLED_SPI2_MOSI_GPIO_Port GPIOB
#define TRK2_USART6_TX_Pin GPIO_PIN_6
#define TRK2_USART6_TX_GPIO_Port GPIOC
#define TRK2_USART6_RX_Pin GPIO_PIN_7
#define TRK2_USART6_RX_GPIO_Port GPIOC
#define TRK1_USART3_TX_Pin GPIO_PIN_8
#define TRK1_USART3_TX_GPIO_Port GPIOD
#define TRK1_USART3_RX_Pin GPIO_PIN_9
#define TRK1_USART3_RX_GPIO_Port GPIOD
#define OLED_CS_Pin GPIO_PIN_10
#define OLED_CS_GPIO_Port GPIOD
#define OLED_DC_Pin GPIO_PIN_11
#define OLED_DC_GPIO_Port GPIOD
#define LD4_Pin GPIO_PIN_12
#define LD4_GPIO_Port GPIOD
#define LD3_Pin GPIO_PIN_13
#define LD3_GPIO_Port GPIOD
#define LD5_Pin GPIO_PIN_14
#define LD5_GPIO_Port GPIOD
#define LD6_Pin GPIO_PIN_15
#define LD6_GPIO_Port GPIOD
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define OLED_RST_Pin GPIO_PIN_6
#define OLED_RST_GPIO_Port GPIOD
#define TRK2_USART1_TX_Pin GPIO_PIN_6
#define TRK2_USART1_TX_GPIO_Port GPIOB
#define TRK2_USART1_RX_Pin GPIO_PIN_7
#define TRK2_USART1_RX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
