/* SSD1315 128x64 full-frame buffer, 1024 bytes. */
#include "u8g2.h"
uint8_t *u8g2_m_16_8_f(uint8_t *tile_cnt)
{
    static uint8_t buffer[128u*64u/8u];
    *tile_cnt=8u;
    return buffer;
}
