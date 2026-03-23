#ifndef __USB_LOG_H__
#define __USB_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

void UsbLog_Init(void);
void UsbLog_Task(void);
void UsbLog_Write(const uint8_t *data, uint16_t len);
void UsbLog_WriteString(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __USB_LOG_H__ */
