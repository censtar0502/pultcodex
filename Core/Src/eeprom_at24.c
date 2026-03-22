#include "eeprom_at24.h"

#include "i2c.h"

extern I2C_HandleTypeDef hi2c2;

static uint16_t at24_i2c_addr = AT24C256_I2C_BASE_ADDR;

uint16_t AT24_GetAddress(void)
{
  return at24_i2c_addr;
}

HAL_StatusTypeDef AT24_DetectAddress(void)
{
  uint16_t candidate;

  for (candidate = AT24C256_I2C_BASE_ADDR;
       candidate <= (AT24C256_I2C_BASE_ADDR + (7U << 1));
       candidate = (uint16_t)(candidate + 2U))
  {
    if (HAL_I2C_IsDeviceReady(&hi2c2, candidate, 3U, 10U) == HAL_OK)
    {
      at24_i2c_addr = candidate;
      return HAL_OK;
    }
  }

  return HAL_ERROR;
}

HAL_StatusTypeDef AT24_IsReady(uint32_t trials, uint32_t timeout_ms)
{
  return HAL_I2C_IsDeviceReady(&hi2c2,
                               at24_i2c_addr,
                               (uint32_t)trials,
                               (uint32_t)timeout_ms);
}

HAL_StatusTypeDef AT24_Read(uint16_t mem_addr,
                            uint8_t *data,
                            uint16_t size,
                            uint32_t timeout_ms)
{
  if ((data == NULL) || (size == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_I2C_Mem_Read(&hi2c2,
                          at24_i2c_addr,
                          mem_addr,
                          I2C_MEMADD_SIZE_16BIT,
                          data,
                          size,
                          timeout_ms);
}

HAL_StatusTypeDef AT24_Write(uint16_t mem_addr,
                             const uint8_t *data,
                             uint16_t size,
                             uint32_t timeout_ms)
{
  HAL_StatusTypeDef status = HAL_OK;

  if ((data == NULL) || (size == 0U))
  {
    return HAL_ERROR;
  }

  while (size > 0U)
  {
    uint16_t page_offset = (uint16_t)(mem_addr % AT24C256_PAGE_SIZE);
    uint16_t page_space = (uint16_t)(AT24C256_PAGE_SIZE - page_offset);
    uint16_t chunk = (size < page_space) ? size : page_space;

    status = HAL_I2C_Mem_Write(&hi2c2,
                               at24_i2c_addr,
                               mem_addr,
                               I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)data,
                               chunk,
                               timeout_ms);
    if (status != HAL_OK)
    {
      return status;
    }

    status = AT24_IsReady(100U, timeout_ms);
    if (status != HAL_OK)
    {
      return status;
    }

    mem_addr = (uint16_t)(mem_addr + chunk);
    data += chunk;
    size = (uint16_t)(size - chunk);
  }

  return HAL_OK;
}
