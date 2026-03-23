#include "usb_log.h"

#include <string.h>

#include "usbd_cdc_if.h"

#define USB_LOG_BUFFER_SIZE 512U
#define USB_LOG_CHUNK_SIZE  64U

static uint8_t usb_log_buffer[USB_LOG_BUFFER_SIZE];
static volatile uint16_t usb_log_head;
static volatile uint16_t usb_log_tail;

static uint16_t UsbLog_NextIndex(uint16_t idx)
{
  return (uint16_t)((idx + 1U) % USB_LOG_BUFFER_SIZE);
}

void UsbLog_Init(void)
{
  usb_log_head = 0U;
  usb_log_tail = 0U;
}

void UsbLog_Write(const uint8_t *data, uint16_t len)
{
  uint16_t i;

  if ((data == NULL) || (len == 0U))
  {
    return;
  }

  for (i = 0U; i < len; ++i)
  {
    uint16_t next_head = UsbLog_NextIndex(usb_log_head);

    if (next_head == usb_log_tail)
    {
      break;
    }

    usb_log_buffer[usb_log_head] = data[i];
    usb_log_head = next_head;
  }
}

void UsbLog_WriteString(const char *text)
{
  if (text == NULL)
  {
    return;
  }

  UsbLog_Write((const uint8_t *)text, (uint16_t)strlen(text));
}

void UsbLog_Task(void)
{
  static uint8_t tx_chunk[USB_LOG_CHUNK_SIZE];
  uint16_t count = 0U;

  if ((usb_log_head == usb_log_tail) || (CDC_IsReady_FS() == 0U))
  {
    return;
  }

  while ((usb_log_tail != usb_log_head) && (count < USB_LOG_CHUNK_SIZE))
  {
    tx_chunk[count++] = usb_log_buffer[usb_log_tail];
    usb_log_tail = UsbLog_NextIndex(usb_log_tail);
  }

  if (count != 0U)
  {
    if (CDC_Transmit_FS(tx_chunk, count) != USBD_OK)
    {
      uint16_t idx = count;

      while (idx > 0U)
      {
        --idx;
        usb_log_tail = (uint16_t)((usb_log_tail + USB_LOG_BUFFER_SIZE - 1U) % USB_LOG_BUFFER_SIZE);
      }
    }
  }
}
