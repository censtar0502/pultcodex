#include "ssd1322.h"

#include <string.h>

#include "main.h"
#include "spi.h"

extern SPI_HandleTypeDef hspi2;

static uint8_t ssd1322_fb[SSD1322_FB_SIZE];

static void ssd1322_select(void)
{
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
}

static void ssd1322_unselect(void)
{
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

static void ssd1322_write_command(uint8_t cmd)
{
  HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
  ssd1322_select();
  HAL_SPI_Transmit(&hspi2, &cmd, 1U, HAL_MAX_DELAY);
  ssd1322_unselect();
}

static void ssd1322_write_data(const uint8_t *data, uint16_t size)
{
  HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
  ssd1322_select();
  HAL_SPI_Transmit(&hspi2, (uint8_t *)data, size, HAL_MAX_DELAY);
  ssd1322_unselect();
}

static void ssd1322_reset(void)
{
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(20);
}

static void ssd1322_set_window(uint8_t col_start, uint8_t col_end,
                               uint8_t row_start, uint8_t row_end)
{
  ssd1322_write_command(0x15);
  ssd1322_write_command(col_start);
  ssd1322_write_command(col_end);

  ssd1322_write_command(0x75);
  ssd1322_write_command(row_start);
  ssd1322_write_command(row_end);
}

uint8_t *SSD1322_GetFramebuffer(void)
{
  return ssd1322_fb;
}

void SSD1322_Clear(uint8_t gray4)
{
  uint8_t packed = (uint8_t)((gray4 << 4) | (gray4 & 0x0F));
  memset(ssd1322_fb, packed, sizeof(ssd1322_fb));
}

void SSD1322_Flush(void)
{
  /* Typical 256x64 SSD1322 modules use this visible RAM window. */
  ssd1322_set_window(0x1C, 0x5B, 0x00, 0x3F);
  ssd1322_write_command(0x5C);
  ssd1322_write_data(ssd1322_fb, (uint16_t)sizeof(ssd1322_fb));
}

void SSD1322_TestPattern(void)
{
  uint32_t row;
  uint32_t col_byte;

  for (row = 0; row < SSD1322_HEIGHT; ++row)
  {
    uint32_t row_offset = row * (SSD1322_WIDTH / 2U);
    for (col_byte = 0; col_byte < (SSD1322_WIDTH / 2U); ++col_byte)
    {
      uint8_t gray = (uint8_t)(col_byte / 8U);
      ssd1322_fb[row_offset + col_byte] = (uint8_t)((gray << 4) | gray);
    }
  }

  SSD1322_Flush();
}

void SSD1322_Init(void)
{
  static const uint8_t init_seq[] = {
    0xFD, 0x12,       /* Unlock command set */
    0xAE,             /* Display OFF */
    0xB3, 0x91,       /* Front clock divider / oscillator */
    0xCA, 0x3F,       /* Multiplex ratio for 64 rows */
    0xA2, 0x00,       /* Display offset */
    0xA1, 0x00,       /* Start line */
    0xA0, 0x14, 0x11, /* Remap and dual COM mode */
    0xAB, 0x01,       /* Enable internal regulator */
    0xB4, 0xA0, 0xFD, /* Display enhancement */
    0xC1, 0x9F,       /* Contrast current */
    0xC7, 0x0F,       /* Master current */
    0xB1, 0xE2,       /* Phase length */
    0xD1, 0x82, 0x20, /* Display enhancement B */
    0xBB, 0x1F,       /* Pre-charge voltage */
    0xB6, 0x08,       /* Second pre-charge period */
    0xBE, 0x07,       /* VCOMH */
    0xA6,             /* Normal display */
    0xAF              /* Display ON */
  };

  size_t i = 0U;

  ssd1322_reset();

  while (i < sizeof(init_seq))
  {
    uint8_t cmd = init_seq[i++];
    ssd1322_write_command(cmd);

    switch (cmd)
    {
      case 0xFD:
      case 0xB3:
      case 0xCA:
      case 0xA2:
      case 0xA1:
      case 0xAB:
      case 0xC1:
      case 0xC7:
      case 0xB1:
      case 0xBB:
      case 0xB6:
      case 0xBE:
        ssd1322_write_command(init_seq[i++]);
        break;

      case 0xA0:
      case 0xB4:
      case 0xD1:
        ssd1322_write_command(init_seq[i++]);
        ssd1322_write_command(init_seq[i++]);
        break;

      default:
        break;
    }
  }

  SSD1322_Clear(0x0);
  SSD1322_Flush();
}
