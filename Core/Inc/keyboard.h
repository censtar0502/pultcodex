#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  char key;
  uint8_t pressed;
} KeyboardEvent;

void Keyboard_Init(void);
void Keyboard_Task10ms(void);
uint8_t Keyboard_GetEvent(KeyboardEvent *event);
const char *Keyboard_GetLegend(char key);

#ifdef __cplusplus
}
#endif

#endif /* __KEYBOARD_H__ */
