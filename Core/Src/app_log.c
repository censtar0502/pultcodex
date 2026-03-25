#include "app_log.h"

#include <stdio.h>
#include <string.h>

#include "usb_log.h"

#define APP_LOG_ENABLE_GENERAL     1U
#define APP_LOG_ENABLE_KEYBOARD    1U
#define APP_LOG_PROTO_MASK         APP_LOG_PROTO_BOTH
#define APP_LOG_PROTO_MAX_BYTES    64U

static const char *AppLog_LevelText(AppLogLevel level)
{
  switch (level)
  {
    case APP_LOG_LEVEL_WARN:
      return "WARN";
    case APP_LOG_LEVEL_ERROR:
      return "ERROR";
    case APP_LOG_LEVEL_INFO:
    default:
      return "INFO";
  }
}

static uint8_t AppLog_IsProtoEnabled(uint8_t trk_id)
{
  if (trk_id == 1U)
  {
    return ((APP_LOG_PROTO_MASK & APP_LOG_PROTO_TRK1) != 0U) ? 1U : 0U;
  }

  if (trk_id == 2U)
  {
    return ((APP_LOG_PROTO_MASK & APP_LOG_PROTO_TRK2) != 0U) ? 1U : 0U;
  }

  if (trk_id == 3U)
  {
    return ((APP_LOG_PROTO_MASK & APP_LOG_PROTO_TRK3) != 0U) ? 1U : 0U;
  }

  return 0U;
}

void AppLog_Init(void)
{
  UsbLog_Init();
}

void AppLog_Task(void)
{
  UsbLog_Task();
}

void AppLog_Message(AppLogLevel level, const char *module, const char *message)
{
#if (APP_LOG_ENABLE_GENERAL == 1U)
  char line[128];

  (void)snprintf(line,
                 sizeof(line),
                 "[%s] %s: %s\r\n",
                 AppLog_LevelText(level),
                 (module != NULL) ? module : "APP",
                 (message != NULL) ? message : "");
  UsbLog_WriteString(line);
#else
  (void)level;
  (void)module;
  (void)message;
#endif
}

void AppLog_KeyEvent(const KeyboardEvent *event)
{
#if (APP_LOG_ENABLE_KEYBOARD == 1U)
  char line[96];

  if (event == NULL)
  {
    return;
  }

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] KEY: KEY=%c LEGEND=%s STATE=%s\r\n",
                 event->key,
                 Keyboard_GetLegend(event->key),
                 (event->pressed != 0U) ? "DOWN" : "UP");
  UsbLog_WriteString(line);
#else
  (void)event;
#endif
}

void AppLog_ProtoFrame(uint8_t trk_id,
                       AppLogProtoDirection direction,
                       const uint8_t *data,
                       size_t len)
{
  char line[256];
  size_t i;
  size_t pos;

  if ((data == NULL) || (len == 0U) || (AppLog_IsProtoEnabled(trk_id) == 0U))
  {
    return;
  }

  pos = (size_t)snprintf(line,
                         sizeof(line),
                         "[INFO] TRK%u %s:",
                         (unsigned int)trk_id,
                         (direction == APP_LOG_PROTO_DIR_TX) ? "TX" : "RX");

  for (i = 0U; (i < len) && (i < APP_LOG_PROTO_MAX_BYTES); ++i)
  {
    if ((pos + 4U) >= sizeof(line))
    {
      break;
    }

    pos += (size_t)snprintf(&line[pos], sizeof(line) - pos, " %02X", data[i]);
  }

  if ((i < len) && ((pos + 5U) < sizeof(line)))
  {
    pos += (size_t)snprintf(&line[pos], sizeof(line) - pos, " ...");
  }

  if ((pos + 3U) < sizeof(line))
  {
    (void)snprintf(&line[pos], sizeof(line) - pos, "\r\n");
  }

  UsbLog_WriteString(line);
}
