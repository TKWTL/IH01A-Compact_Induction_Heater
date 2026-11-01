/* SSD1315 128x64 on SPI1: nonblocking DMA for runtime page transfers.
 *
 * CubeMX SPI1 TX: DMA1_Channel1, DMA_REQUEST_SPI1_TX, 8-bit, normal.
 * u8g2 startup uses the SAME TX DMA primitive, waiting for IRQ completion.
 * Runtime refresh keeps command/data D/C changes between completed DMA
 * transactions and holds DMA source storage stable until completion.
 *
 * PA15 hardware NSS, PB3 SCK, PB5 MOSI, PB4 D/C.
 * Display RES# follows STM32 PF2/NRST; OLED VCC is externally supplied.
 * No charge pump: the SSD1315 init sequence must keep 8D 10.
 */
#include "ssd1315_display.h"
#include "spi.h"
#include "main.h"
#include <string.h>

enum {
    OLED_DMA_IDLE = 0,
    OLED_DMA_INIT,
    OLED_DMA_PAGE_COMMAND,
    OLED_DMA_PAGE_DATA,
    OLED_DMA_POWER,
    OLED_DMA_CONTRAST
};

static u8g2_t *s_screen;
static uint8_t s_active[SSD1315_FB_SIZE];
static uint8_t s_pending[SSD1315_FB_SIZE];
static uint8_t s_cmd[3];             /* DMA must never reference a stack buffer */
static uint8_t s_has_pending;
static uint8_t s_active_frame;
static uint8_t s_page;
static uint8_t s_power_save;
static uint8_t s_power_requested;
static uint8_t s_power_dirty;
static uint8_t s_contrast_dirty;
static uint8_t s_contrast;
static uint8_t s_transmitted_value;
static uint8_t s_ready;
static uint32_t s_frames;
static volatile uint8_t s_dma_phase;
static volatile uint8_t s_dma_complete;
static volatile uint8_t s_dma_failed;
static uint32_t s_dma_started_ms;

/* Shared DMA primitive for both startup commands and cooperative runtime.
 * The startup path may wait; ordinary page refresh never waits.
 * DMA1_Channel1_IRQn and SPI1_IRQn must remain enabled in CubeMX. */
static uint8_t oled_start_dma(uint8_t *data, uint16_t length,
                              uint8_t data_mode, uint8_t phase);
static uint8_t s_u8x8_dc;

/* Called only during u8g2 initialization, before s_ready. */
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
        s_u8x8_dc = (arg_int != 0u);
        if(s_u8x8_dc) LL_GPIO_SetOutputPin(D_C_GPIO_Port, D_C_Pin);
        else LL_GPIO_ResetOutputPin(D_C_GPIO_Port, D_C_Pin);
        return 1u;
    case U8X8_MSG_BYTE_SEND:
    {
        uint32_t start;
        uint8_t success;
        if(arg_int == 0u) return 1u;
        if(!oled_start_dma((uint8_t *)arg_ptr, (uint16_t)arg_int,
                           s_u8x8_dc, OLED_DMA_INIT))
            return 0u;
        start = HAL_GetTick();
        /* Init only. No UI/PD scheduler is running yet; DMA IRQ completes
         * this transfer. Never use this wait in the runtime service. */
        while(!s_dma_complete && !s_dma_failed &&
              ((uint32_t)(HAL_GetTick() - start) < 20u))
        {
        }
        success = (s_dma_complete && !s_dma_failed) ? 1u : 0u;
        if(!success)
            (void)HAL_SPI_Abort(&hspi1);
        s_dma_phase = OLED_DMA_IDLE;
        s_dma_complete = 0u;
        s_dma_failed = 0u;
        return success;
    }
    default:
        return 0u;
    }
}

static uint8_t U8X8_GPIO(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8; (void)arg_ptr;
    switch(msg)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
    case U8X8_MSG_GPIO_RESET:
        /* Shared reset: do not pulse NRST from firmware. */
        return 1u;
    case U8X8_MSG_DELAY_MILLI:
        HAL_Delay(arg_int);
        return 1u;
    default:
        return 1u;
    }
}

/* Called from the USBPD user hook (thread context), never ISR. */
static uint8_t oled_start_dma(uint8_t *data, uint16_t length,
                              uint8_t data_mode, uint8_t phase)
{
    if(s_dma_phase != OLED_DMA_IDLE ||
       HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY)
        return 0u;

    if(data_mode)
        LL_GPIO_SetOutputPin(D_C_GPIO_Port, D_C_Pin);
    else
        LL_GPIO_ResetOutputPin(D_C_GPIO_Port, D_C_Pin);

    s_dma_complete = 0u;
    s_dma_failed = 0u;
    s_dma_phase = phase;
    s_dma_started_ms = HAL_GetTick();
    if(HAL_SPI_Transmit_DMA(&hspi1, data, length) != HAL_OK)
    {
        s_dma_phase = OLED_DMA_IDLE;
        return 0u;
    }
    return 1u;
}

/* The DMA IRQ only records completion: no SPI re-entry, memcpy or UI work. */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance == SPI1 && s_dma_phase != OLED_DMA_IDLE)
        s_dma_complete = 1u;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance == SPI1 && s_dma_phase != OLED_DMA_IDLE)
        s_dma_failed = 1u;
}

void SSD1315_Display_Init(u8g2_t *screen)
{
    if(screen == 0) return;
    s_ready = 0u;
    s_screen = screen;
    s_page = 0u;
    s_has_pending = 0u;
    s_active_frame = 0u;
    s_power_save = 0u;
    s_power_dirty = 0u;
    s_contrast_dirty = 0u;
    s_dma_phase = OLED_DMA_IDLE;
    s_dma_complete = 0u;
    s_dma_failed = 0u;
    s_u8x8_dc = 0u;
    s_frames = 0u;
    /* Keep user-corrected initial display rotation. */
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
    if(!s_ready || s_screen == 0) return;

    if(s_dma_phase != OLED_DMA_IDLE)
    {
        uint8_t phase = s_dma_phase;

        if(!s_dma_complete && !s_dma_failed)
        {
            /* Missing IRQ must not leave the display state machine stuck. */
            if((uint32_t)(HAL_GetTick() - s_dma_started_ms) <= 50u)
                return;
            s_dma_failed = 1u;
        }

        if(s_dma_failed)
        {
            /* Rare error/timeout recovery; keep page for retry.
             * Only the error path calls the blocking HAL abort routine. */
            (void)HAL_SPI_Abort(&hspi1);
            s_dma_phase = OLED_DMA_IDLE;
            s_dma_complete = 0u;
            s_dma_failed = 0u;
            return;
        }

        s_dma_phase = OLED_DMA_IDLE;
        s_dma_complete = 0u;

        switch(phase)
        {
        case OLED_DMA_PAGE_COMMAND:
            /* Next transaction starts only after the page command finishes,
             * otherwise D/C could switch in the middle of a DMA transfer. */
            (void)oled_start_dma(&s_active[(uint16_t)s_page * SSD1315_WIDTH],
                                 SSD1315_WIDTH, 1u, OLED_DMA_PAGE_DATA);
            return;
        case OLED_DMA_PAGE_DATA:
            if(++s_page >= 8u)
            {
                s_active_frame = 0u;
                s_page = 0u;
                s_frames++;
            }
            return;
        case OLED_DMA_POWER:
            s_power_save = s_transmitted_value;
            s_power_dirty = (s_power_requested != s_transmitted_value);
            return;
        case OLED_DMA_CONTRAST:
            s_contrast_dirty = (s_contrast != s_transmitted_value);
            return;
        default:
            return;
        }
    }

    if(s_active_frame)
    {
        s_cmd[0] = (uint8_t)(0xB0u | s_page);
        s_cmd[1] = 0x00u;
        s_cmd[2] = 0x10u;
        (void)oled_start_dma(s_cmd, 3u, 0u, OLED_DMA_PAGE_COMMAND);
        return;
    }

    if(s_power_dirty)
    {
        s_transmitted_value = s_power_requested;
        s_cmd[0] = s_transmitted_value ? 0xAEu : 0xAFu;
        (void)oled_start_dma(s_cmd, 1u, 0u, OLED_DMA_POWER);
        return;
    }

    if(s_contrast_dirty)
    {
        s_transmitted_value = s_contrast;
        s_cmd[0] = 0x81u;
        s_cmd[1] = s_transmitted_value;
        (void)oled_start_dma(s_cmd, 2u, 0u, OLED_DMA_CONTRAST);
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
