#ifndef __APP_LOG_H__
#define __APP_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "keyboard.h"

typedef enum
{
  APP_LOG_LEVEL_INFO = 0,
  APP_LOG_LEVEL_WARN,
  APP_LOG_LEVEL_ERROR
} AppLogLevel;

typedef enum
{
  APP_LOG_PROTO_NONE = 0x00,
  APP_LOG_PROTO_TRK1 = 0x01,
  APP_LOG_PROTO_TRK2 = 0x02,
  APP_LOG_PROTO_TRK3 = 0x04,
  APP_LOG_PROTO_BOTH = 0x03,
  APP_LOG_PROTO_ALL  = 0x07
} AppLogProtoMask;

typedef enum
{
  APP_LOG_PROTO_DIR_TX = 0,
  APP_LOG_PROTO_DIR_RX
} AppLogProtoDirection;

void AppLog_Init(void);
void AppLog_Task(void);

void AppLog_Message(AppLogLevel level, const char *module, const char *message);
void AppLog_KeyEvent(const KeyboardEvent *event);
void AppLog_ProtoFrame(uint8_t trk_id,
                       AppLogProtoDirection direction,
                       const uint8_t *data,
                       size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOG_H__ */
