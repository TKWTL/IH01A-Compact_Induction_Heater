/* SSD1315 128x64 driver, derived from trimmed u8g2 BSD-2-Clause source.
 * External VCC; NO internal charge pump. Reset is hardware-only on IH01A. */
#include "u8x8.h"

static const uint8_t s_power_save_off[] =
{
    U8X8_START_TRANSFER(),
    U8X8_C(0xAF),
    U8X8_END_TRANSFER(),
    U8X8_END()
};

static const uint8_t s_power_save_on[] =
{
    U8X8_START_TRANSFER(),
    U8X8_C(0xAE),
    U8X8_END_TRANSFER(),
    U8X8_END()
};

/* 4-wire SPI SSD1315 128x64, powered from EXTERNAL VCC.
 * 8D 10 is mandatory: never enable the internal charge pump (8D 14). */
static const uint8_t s_init_seq[] =
{
    U8X8_START_TRANSFER(),
    U8X8_C(0xAE),
    U8X8_CA(0x8D, 0x10),       /* internal charge pump OFF */
    U8X8_CA(0x20, 0x02),       /* page addressing mode */
    U8X8_CA(0x81, 0x7F),       /* contrast */
    U8X8_C(0xA1),              /* segment remap */
    U8X8_C(0xC8),              /* COM scan direction */
    U8X8_CA(0xA8, 0x3F),       /* 64 multiplex */
    U8X8_CA(0xD3, 0x00),       /* display offset zero */
    U8X8_C(0x40),              /* RAM start line */
    U8X8_CA(0xDA, 0x12),       /* alternative COM pin configuration */
    U8X8_CA(0xD5, 0x80),
    U8X8_CA(0xD9, 0x22),       /* external VCC precharge */
    U8X8_CA(0xDB, 0x30),
    U8X8_C(0xA4),
    U8X8_C(0xA6),
    U8X8_C(0xAF),
    U8X8_END_TRANSFER(),
    U8X8_END()
};

static const u8x8_display_info_t s_display_info =
{
    0,          /* chip_enable_level */
    1,          /* chip_disable_level */
    20,         /* post_chip_enable_wait_ns */
    10,         /* pre_chip_disable_wait_ns */
    0,          /* reset_pulse_width_ms: reset is external RC, not MCU-driven */
    0,          /* post_reset_wait_ms: panel has already settled before UI init */
    100,        /* sda_setup_time_ns */
    100,        /* sck_pulse_width_ns */
    8000000UL,  /* SPI clock target */
    0,          /* SPI mode 0 */
    4,          /* unused I2C timing */
    40,
    150,
    16,         /* native tile width: 128 px */
    8,          /* native tile height: 64 px */
    0,
    0,
    128,        /* native pixel width */
    64          /* native pixel height */
};

static uint8_t SSD1315_Generic(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    uint8_t x;
    uint8_t c;
    uint8_t *ptr;

    switch(msg)
    {
        case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
            u8x8_cad_SendSequence(u8x8, arg_int ? s_power_save_on : s_power_save_off);
            return 1;

#ifdef U8X8_WITH_SET_CONTRAST
        case U8X8_MSG_DISPLAY_SET_CONTRAST:
            u8x8_cad_StartTransfer(u8x8);
            u8x8_cad_SendCmd(u8x8, 0x81);
            u8x8_cad_SendArg(u8x8, arg_int);
            u8x8_cad_EndTransfer(u8x8);
            return 1;
#endif

        case U8X8_MSG_DISPLAY_DRAW_TILE:
            u8x8_cad_StartTransfer(u8x8);
            x = (uint8_t)(((u8x8_tile_t *)arg_ptr)->x_pos * 8U + u8x8->x_offset);
            u8x8_cad_SendCmd(u8x8, (uint8_t)(0x10U | (x >> 4)));
            u8x8_cad_SendCmd(u8x8, (uint8_t)(x & 0x0FU));
            u8x8_cad_SendCmd(u8x8, (uint8_t)(0xB0U | ((u8x8_tile_t *)arg_ptr)->y_pos));
            do
            {
                c = ((u8x8_tile_t *)arg_ptr)->cnt;
                ptr = ((u8x8_tile_t *)arg_ptr)->tile_ptr;
                u8x8_cad_SendData(u8x8, (uint8_t)(c * 8U), ptr);
                --arg_int;
            } while(arg_int > 0U);
            u8x8_cad_EndTransfer(u8x8);
            return 1;

        default:
            return 0;
    }
}

uint8_t u8x8_d_ssd1315_128x64(u8x8_t *u8x8,
                                        uint8_t msg,
                                        uint8_t arg_int,
                                        void *arg_ptr)
{
    if(SSD1315_Generic(u8x8, msg, arg_int, arg_ptr))
        return 1;

    switch(msg)
    {
        case U8X8_MSG_DISPLAY_INIT:
            u8x8_d_helper_display_init(u8x8);
            u8x8_cad_SendSequence(u8x8, s_init_seq);
            return 1;

        case U8X8_MSG_DISPLAY_SETUP_MEMORY:
            u8x8_d_helper_display_setup_memory(u8x8, &s_display_info);
            return 1;

        default:
            return 0;
    }
}
