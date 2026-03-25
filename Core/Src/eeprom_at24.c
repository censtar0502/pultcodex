#include "eeprom_at24.h"

#include <string.h>

#include "i2c.h"

extern I2C_HandleTypeDef hi2c2;

static uint16_t at24_i2c_addr = AT24C256_I2C_BASE_ADDR;
typedef enum
{
  AT24_SERVICE_STATE_IDLE = 0,
  AT24_SERVICE_STATE_WRITE_WAIT_TX,
  AT24_SERVICE_STATE_WRITE_WAIT_READY
} At24ServiceState;

typedef struct
{
  At24ServiceState state;
  const uint8_t *data;
  uint16_t mem_addr;
  uint16_t size;
  uint16_t offset;
  uint16_t chunk;
  uint32_t ready_deadline_ms;
  uint32_t ready_timeout_deadline_ms;
  HAL_StatusTypeDef last_error;
} At24Service;

static At24Service at24_service;

static HAL_StatusTypeDef AT24_ServiceStartNextPage(void);

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

void AT24_Service_Init(void)
{
  memset(&at24_service, 0, sizeof(at24_service));
  at24_service.last_error = HAL_OK;
}

uint8_t AT24_Service_IsBusy(void)
{
  return (at24_service.state != AT24_SERVICE_STATE_IDLE) ? 1U : 0U;
}

static HAL_StatusTypeDef AT24_ServiceStartNextPage(void)
{
  uint16_t page_offset;
  uint16_t page_space;

  if ((at24_service.data == NULL) ||
      (at24_service.size == 0U) ||
      (at24_service.offset >= at24_service.size))
  {
    at24_service.state = AT24_SERVICE_STATE_IDLE;
    return HAL_ERROR;
  }

  page_offset = (uint16_t)(at24_service.mem_addr % AT24C256_PAGE_SIZE);
  page_space = (uint16_t)(AT24C256_PAGE_SIZE - page_offset);
  at24_service.chunk = (uint16_t)((at24_service.size - at24_service.offset) < page_space
                        ? (at24_service.size - at24_service.offset)
                        : page_space);

  if (HAL_I2C_Mem_Write_IT(&hi2c2,
                           at24_i2c_addr,
                           at24_service.mem_addr,
                           I2C_MEMADD_SIZE_16BIT,
                           (uint8_t *)(at24_service.data + at24_service.offset),
                           at24_service.chunk) != HAL_OK)
  {
    at24_service.last_error = HAL_ERROR;
    at24_service.state = AT24_SERVICE_STATE_IDLE;
    return HAL_ERROR;
  }

  at24_service.last_error = HAL_OK;
  at24_service.state = AT24_SERVICE_STATE_WRITE_WAIT_TX;
  return HAL_OK;
}

HAL_StatusTypeDef AT24_WriteAsync(uint16_t mem_addr, const uint8_t *data, uint16_t size)
{
  if ((data == NULL) || (size == 0U))
  {
    return HAL_ERROR;
  }

  if (AT24_Service_IsBusy() != 0U)
  {
    return HAL_BUSY;
  }

  at24_service.data = data;
  at24_service.mem_addr = mem_addr;
  at24_service.size = size;
  at24_service.offset = 0U;
  at24_service.chunk = 0U;
  at24_service.ready_deadline_ms = 0U;
  at24_service.ready_timeout_deadline_ms = 0U;
  at24_service.last_error = HAL_OK;

  return AT24_ServiceStartNextPage();
}

void AT24_Service_Task(void)
{
  HAL_StatusTypeDef ready_status;
  uint32_t now;

  if (at24_service.state != AT24_SERVICE_STATE_WRITE_WAIT_READY)
  {
    return;
  }

  now = HAL_GetTick();
  if ((int32_t)(now - at24_service.ready_deadline_ms) < 0)
  {
    return;
  }

  ready_status = HAL_I2C_IsDeviceReady(&hi2c2,
                                       at24_i2c_addr,
                                       1U,
                                       0U);
  if (ready_status == HAL_OK)
  {
    at24_service.offset = (uint16_t)(at24_service.offset + at24_service.chunk);
    at24_service.mem_addr = (uint16_t)(at24_service.mem_addr + at24_service.chunk);

    if (at24_service.offset >= at24_service.size)
    {
      at24_service.state = AT24_SERVICE_STATE_IDLE;
      at24_service.data = NULL;
      at24_service.size = 0U;
      at24_service.chunk = 0U;
      return;
    }

    (void)AT24_ServiceStartNextPage();
    return;
  }

  if ((int32_t)(now - at24_service.ready_timeout_deadline_ms) >= 0)
  {
    at24_service.last_error = HAL_TIMEOUT;
    at24_service.state = AT24_SERVICE_STATE_IDLE;
    at24_service.data = NULL;
    at24_service.size = 0U;
    at24_service.chunk = 0U;
    return;
  }

  at24_service.ready_deadline_ms = now + 1U;
}

void AT24_OnMemTxCplt(I2C_HandleTypeDef *hi2c)
{
  if ((hi2c != &hi2c2) ||
      (at24_service.state != AT24_SERVICE_STATE_WRITE_WAIT_TX))
  {
    return;
  }

  at24_service.state = AT24_SERVICE_STATE_WRITE_WAIT_READY;
  at24_service.ready_deadline_ms = HAL_GetTick() + 1U;
  at24_service.ready_timeout_deadline_ms = HAL_GetTick() + 20U;
}

void AT24_OnMemRxCplt(I2C_HandleTypeDef *hi2c)
{
  (void)hi2c;
}

void AT24_OnError(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != &hi2c2)
  {
    return;
  }

  at24_service.last_error = HAL_ERROR;
  at24_service.state = AT24_SERVICE_STATE_IDLE;
  at24_service.data = NULL;
  at24_service.size = 0U;
  at24_service.chunk = 0U;
}
