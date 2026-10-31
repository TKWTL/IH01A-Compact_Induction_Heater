/* SSD1315 (128x64), 4-wire SPI1 / STM32G071.
 * PA15 HW NSS, PB3 SCK, PB5 MOSI, PB4 D/C.
 * OLED RES# shares STM32G071 PF2/NRST; no dedicated reset GPIO.
 * The panel's VCC is externally powered; the u8g2 sequence disables the
 * internal charge pump (8D 10). Never issue 8D 14 here.
 * SPI1 is dedicated to OLED. No DMA used in the baseline: one 128-byte page
 * is transmitted per cooperative service pass so USBPD_DPM_Run() remains
 * scheduled between pages. TX failures are retried on the next pass.
 */
#include "ssd1315_display.h"
#include "spi.h"
#include "main.h"
#include <string.h>

static u8g2_t *s_screen;
static uint8_t s_active[SSD1315_FB_SIZE];
static uint8_t s_pending[SSD1315_FB_SIZE];
static uint8_t s_has_pending;
static uint8_t s_active_frame;
static uint8_t s_page;
static uint8_t s_power_save;
static uint8_t s_power_requested;
static uint8_t s_power_dirty;
static uint8_t s_contrast_dirty;
static uint8_t s_contrast;
static uint8_t s_ready;
static uint32_t s_frames;

static uint8_t spi_send(const uint8_t *buf, uint16_t len, uint8_t data_mode)
{
    if(HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY)
        return 0u;
    if(data_mode)
        LL_GPIO_SetOutputPin(D_C_GPIO_Port, D_C_Pin);
    else
        LL_GPIO_ResetOutputPin(D_C_GPIO_Port, D_C_Pin);
    return (HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, 2u) == HAL_OK) ? 1u : 0u;
}

static uint8_t U8X8_Byte(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    switch(msg)
    {
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_START_TRANSFER:
    case U8X8_MSG_BYTE_END_TRANSFER:
        return 1u;
    case U8X8_MSG_BYTE_SET_DC:
        if(arg_int) LL_GPIO_SetOutputPin(D_C_GPIO_Port, D_C_Pin);
        else LL_GPIO_ResetOutputPin(D_C_GPIO_Port, D_C_Pin);
        return 1u;
    case U8X8_MSG_BYTE_SEND:
        /* arg_int is at most 255 for the trimmed u8g2 byte interface. */
        return (HAL_SPI_Transmit(&hspi1, (uint8_t *)arg_ptr,
                                 arg_int, 2u) == HAL_OK) ? 1u : 0u;
    default:
        return 0u;
    }
}

static uint8_t U8X8_GPIO(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8; (void)arg_int; (void)arg_ptr;
    /* OLED RES# and MCU NRST share one net, so the application cannot
     * pulse display reset without resetting itself. NRST_MODE must remain
     * reset input/output for internal resets to appear on the pin.
     * The initial 250ms settle time is provided by thread_ui. */
    switch(msg)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
    case U8X8_MSG_GPIO_RESET:
        return 1u;
    case U8X8_MSG_DELAY_MILLI:
        HAL_Delay(arg_int);
        return 1u;
    default:
        return 1u;
    }
}

void SSD1315_Display_Init(u8g2_t *screen)
{
    if(screen == 0) return;
    s_screen = screen;
    s_page = 0u;
    s_has_pending = 0u;
    s_active_frame = 0u;
    s_power_save = 0u;
    s_power_dirty = 0u;
    s_contrast_dirty = 0u;
    s_frames = 0u;
    u8g2_Setup_ssd1315_128x64_f(screen, U8G2_R2, U8X8_Byte, U8X8_GPIO);
    u8g2_InitDisplay(screen);
    u8g2_SetPowerSave(screen, 0u);
    u8g2_ClearBuffer(screen);
    s_ready = 1u;
}

void SSD1315_Display_RequestFlush(const uint8_t *framebuffer)
{
    if(!s_ready || framebuffer == 0) return;
    memcpy(s_pending, framebuffer, SSD1315_FB_SIZE);
    s_has_pending = 1u;
}
void SSD1315_Display_RequestContrast(uint8_t value)
{
    s_contrast = value;
    s_contrast_dirty = 1u;
}
void SSD1315_Display_RequestPowerSave(uint8_t enable)
{
    s_power_requested = enable ? 1u : 0u;
    s_power_dirty = 1u;
}

void SSD1315_Display_Service(void)
{
    uint8_t cmd[3];
    if(!s_ready || s_screen == 0) return;
    if(s_active_frame)
    {
        cmd[0] = (uint8_t)(0xB0u | s_page);
        cmd[1] = 0x00u;
        cmd[2] = 0x10u;
        if(!spi_send(cmd, 3u, 0u)) return;
        if(!spi_send(&s_active[(uint16_t)s_page * SSD1315_WIDTH],
                     SSD1315_WIDTH, 1u)) return;
        s_page++;
        if(s_page >= 8u)
        {
            s_active_frame = 0u;
            s_page = 0u;
            s_frames++;
        }
        return;
    }

    if(s_power_dirty)
    {
        cmd[0] = s_power_requested ? 0xAEu : 0xAFu;
        if(!spi_send(cmd, 1u, 0u)) return;
        s_power_save = s_power_requested;
        s_power_dirty = 0u;
        return;
    }

    if(s_contrast_dirty)
    {
        cmd[0] = 0x81u;
        cmd[1] = s_contrast;
        if(!spi_send(cmd, 2u, 0u)) return;
        s_contrast_dirty = 0u;
        return;
    }

    if(s_power_save || !s_has_pending) return;
    memcpy(s_active, s_pending, SSD1315_FB_SIZE);
    s_has_pending = 0u;
    s_active_frame = 1u;
    s_page = 0u;
}
uint8_t SSD1315_Display_IsReady(void) { return s_ready; }
uint32_t SSD1315_Display_GetFrameCount(void) { return s_frames; }
