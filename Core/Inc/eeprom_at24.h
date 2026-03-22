#ifndef __EEPROM_AT24_H__
#define __EEPROM_AT24_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"

#define AT24C256_I2C_ADDR       (0x50U << 1)
#define AT24C256_PAGE_SIZE      64U

HAL_StatusTypeDef AT24_IsReady(uint32_t trials, uint32_t timeout_ms);
HAL_StatusTypeDef AT24_Read(uint16_t mem_addr, uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef AT24_Write(uint16_t mem_addr, const uint8_t *data, uint16_t size, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_AT24_H__ */
