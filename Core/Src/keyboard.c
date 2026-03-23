#include "keyboard.h"

#include "gpio.h"
#include "main.h"

#define KBD_ROWS                5U
#define KBD_COLS                4U
#define KBD_DEBOUNCE_SAMPLES    3U
#define KBD_QUEUE_SIZE          8U

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} KeyboardGpio;

static const KeyboardGpio kbd_rows[KBD_ROWS] = {
  {KBD_ROW1_GPIO_Port, KBD_ROW1_Pin},
  {KBD_ROW2_GPIO_Port, KBD_ROW2_Pin},
  {KBD_ROW3_GPIO_Port, KBD_ROW3_Pin},
  {KBD_ROW4_GPIO_Port, KBD_ROW4_Pin},
  {KBD_COL1_GPIO_Port, KBD_COL1_Pin}
};

static const KeyboardGpio kbd_cols[KBD_COLS] = {
  {KBD_COL5_RES_GPIO_Port, KBD_COL5_RES_Pin},
  {KBD_COL4_GPIO_Port, KBD_COL4_Pin},
  {KBD_COL3_GPIO_Port, KBD_COL3_Pin},
  {KBD_COL2_GPIO_Port, KBD_COL2_Pin}
};

static const char kbd_map[KBD_ROWS][KBD_COLS] = {
  {'A', 'F', 'G', 'H'},
  {'B', '1', '2', '3'},
  {'C', '4', '5', '6'},
  {'D', '7', '8', '9'},
  {'E', '.', '0', 'K'}
};

static KeyboardEvent kbd_queue[KBD_QUEUE_SIZE];
static volatile uint8_t kbd_queue_head;
static volatile uint8_t kbd_queue_tail;
static uint32_t kbd_stable_mask;
static uint32_t kbd_candidate_mask;
static uint8_t kbd_candidate_count;

static void Keyboard_SetAllRowsHigh(void)
{
  uint32_t row;

  for (row = 0U; row < KBD_ROWS; ++row)
  {
    HAL_GPIO_WritePin(kbd_rows[row].port, kbd_rows[row].pin, GPIO_PIN_SET);
  }
}

static void Keyboard_PushEvent(char key, uint8_t pressed)
{
  uint8_t next_head = (uint8_t)((kbd_queue_head + 1U) % KBD_QUEUE_SIZE);

  if (next_head == kbd_queue_tail)
  {
    return;
  }

  kbd_queue[kbd_queue_head].key = key;
  kbd_queue[kbd_queue_head].pressed = pressed;
  kbd_queue_head = next_head;
}

static uint32_t Keyboard_ScanRawMask(void)
{
  uint32_t raw_mask = 0U;
  uint32_t row;
  uint32_t col;

  for (row = 0U; row < KBD_ROWS; ++row)
  {
    Keyboard_SetAllRowsHigh();
    HAL_GPIO_WritePin(kbd_rows[row].port, kbd_rows[row].pin, GPIO_PIN_RESET);
    __NOP();
    __NOP();

    for (col = 0U; col < KBD_COLS; ++col)
    {
      if (HAL_GPIO_ReadPin(kbd_cols[col].port, kbd_cols[col].pin) == GPIO_PIN_RESET)
      {
        raw_mask |= (1UL << (row * KBD_COLS + col));
      }
    }
  }

  Keyboard_SetAllRowsHigh();
  return raw_mask;
}

void Keyboard_Init(void)
{
  kbd_queue_head = 0U;
  kbd_queue_tail = 0U;
  kbd_stable_mask = 0U;
  kbd_candidate_mask = 0U;
  kbd_candidate_count = 0U;
  Keyboard_SetAllRowsHigh();
}

void Keyboard_Task10ms(void)
{
  uint32_t raw_mask = Keyboard_ScanRawMask();

  if (raw_mask != kbd_candidate_mask)
  {
    kbd_candidate_mask = raw_mask;
    kbd_candidate_count = 1U;
    return;
  }

  if (kbd_candidate_count < KBD_DEBOUNCE_SAMPLES)
  {
    ++kbd_candidate_count;
  }

  if ((kbd_candidate_count >= KBD_DEBOUNCE_SAMPLES) && (kbd_stable_mask != kbd_candidate_mask))
  {
    uint32_t changed = kbd_stable_mask ^ kbd_candidate_mask;
    uint32_t index;

    for (index = 0U; index < (KBD_ROWS * KBD_COLS); ++index)
    {
      uint32_t bit = (1UL << index);

      if ((changed & bit) != 0U)
      {
        uint32_t row = index / KBD_COLS;
        uint32_t col = index % KBD_COLS;
        uint8_t pressed = ((kbd_candidate_mask & bit) != 0U) ? 1U : 0U;

        Keyboard_PushEvent(kbd_map[row][col], pressed);
      }
    }

    kbd_stable_mask = kbd_candidate_mask;
  }
}

uint8_t Keyboard_GetEvent(KeyboardEvent *event)
{
  if ((event == 0) || (kbd_queue_head == kbd_queue_tail))
  {
    return 0U;
  }

  *event = kbd_queue[kbd_queue_tail];
  kbd_queue_tail = (uint8_t)((kbd_queue_tail + 1U) % KBD_QUEUE_SIZE);
  return 1U;
}

const char *Keyboard_GetLegend(char key)
{
  switch (key)
  {
    case 'A': return "TOT";
    case 'B': return "TIM/CAL";
    case 'C': return "DAY/SEL";
    case 'D': return "MTH/PRI";
    case 'E': return "RES";
    case 'F': return "COU/ESC";
    case 'G': return "</SET";
    case 'H': return ">/INQ";
    case 'K': return "INV/OK";
    case '.': return "DOT";
    case '0': return "NUM 0";
    case '1': return "NUM 1";
    case '2': return "NUM 2";
    case '3': return "NUM 3";
    case '4': return "NUM 4";
    case '5': return "NUM 5";
    case '6': return "NUM 6";
    case '7': return "NUM 7";
    case '8': return "NUM 8";
    case '9': return "NUM 9";
    default:  return "UNKNOWN";
  }
}
