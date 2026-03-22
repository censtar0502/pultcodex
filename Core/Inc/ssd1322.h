#ifndef __SSD1322_H__
#define __SSD1322_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SSD1322_WIDTH          256U
#define SSD1322_HEIGHT         64U
#define SSD1322_FB_SIZE        ((SSD1322_WIDTH * SSD1322_HEIGHT) / 2U)

void SSD1322_Init(void);
void SSD1322_Clear(uint8_t gray4);
void SSD1322_Flush(void);
void SSD1322_TestPattern(void);
uint8_t *SSD1322_GetFramebuffer(void);

#ifdef __cplusplus
}
#endif

#endif /* __SSD1322_H__ */
