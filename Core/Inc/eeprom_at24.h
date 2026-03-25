#ifndef __EEPROM_AT24_H__
#define __EEPROM_AT24_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"

#define AT24C256_I2C_BASE_ADDR  (0x50U << 1)
#define AT24C256_PAGE_SIZE      64U

uint16_t AT24_GetAddress(void);
HAL_StatusTypeDef AT24_DetectAddress(void);
HAL_StatusTypeDef AT24_IsReady(uint32_t trials, uint32_t timeout_ms);
HAL_StatusTypeDef AT24_Read(uint16_t mem_addr, uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef AT24_Write(uint16_t mem_addr, const uint8_t *data, uint16_t size, uint32_t timeout_ms);
void AT24_Service_Init(void);
void AT24_Service_Task(void);
uint8_t AT24_Service_IsBusy(void);
HAL_StatusTypeDef AT24_WriteAsync(uint16_t mem_addr, const uint8_t *data, uint16_t size);
void AT24_OnMemTxCplt(I2C_HandleTypeDef *hi2c);
void AT24_OnMemRxCplt(I2C_HandleTypeDef *hi2c);
void AT24_OnError(I2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_AT24_H__ */
