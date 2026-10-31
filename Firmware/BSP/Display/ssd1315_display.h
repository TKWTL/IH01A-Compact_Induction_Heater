#ifndef IH_SSD1315_DISPLAY_H
#define IH_SSD1315_DISPLAY_H
#include <stdint.h>
#include "u8g2.h"
#define SSD1315_WIDTH 128u
#define SSD1315_HEIGHT 64u
#define SSD1315_FB_SIZE (SSD1315_WIDTH * SSD1315_HEIGHT / 8u)
void SSD1315_Display_Init(u8g2_t *screen);
void SSD1315_Display_RequestFlush(const uint8_t *framebuffer);
void SSD1315_Display_RequestContrast(uint8_t contrast);
void SSD1315_Display_RequestPowerSave(uint8_t enable);
void SSD1315_Display_Service(void);
uint8_t SSD1315_Display_IsReady(void);
uint32_t SSD1315_Display_GetFrameCount(void);
#endif
