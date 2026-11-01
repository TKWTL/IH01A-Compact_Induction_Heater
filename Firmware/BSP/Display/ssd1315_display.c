/* SSD1315 application driver: 128x64 external-VCC OLED.
 * Protocol/D-C/page sequencing here; SPI HAL/DMA lives in Core/Src/spi.c.
 * Initializer and runtime pages are both cooperative and nonblocking.
 * Panel reset is tied to MCU NRST (no firmware reset pulses).
 */
#include "ssd1315_display.h"
#include "spi.h"
#include "main.h"
#include <string.h>
enum {OLED_PHASE_IDLE=0,OLED_PHASE_INIT,OLED_PHASE_PAGE_CMD,
      OLED_PHASE_PAGE_DATA,OLED_PHASE_POWER,OLED_PHASE_CONTRAST};
#define OLED_TX_TIMEOUT_MS 50u
#define OLED_INIT_MAX_ATTEMPTS 3u
static u8g2_t *s_screen;
static uint8_t s_active[SSD1315_FB_SIZE],s_pending[SSD1315_FB_SIZE];
static uint8_t s_cmd[3];
/* The exact SSD1315 u8x8 initialization commands, including 8D 10
 * (charge pump OFF), 20 02 (page addressing), D9 22 (external VCC).
 * DMA source stays in static SRAM until completion. */
static uint8_t s_init_cmd[]={
    0xAE,0x8D,0x10,0x20,0x02,0x81,0x7F,0xA1,0xC8,0xA8,0x3F,
    0xD3,0x00,0x40,0xDA,0x12,0xD5,0x80,0xD9,0x22,0xDB,0x30,
    0xA4,0xA6,0xAF
};
static uint8_t s_has_pending,s_active_frame,s_page;
static uint8_t s_power_save,s_power_requested,s_power_dirty;
static uint8_t s_contrast_dirty,s_contrast,s_transmitted_value;
static uint8_t s_ready,s_init_pending,s_init_failed,s_init_attempts;
static uint8_t s_dma_phase;
static uint32_t s_frames,s_dma_started_ms;
static SSD1315_DisplayStats s_stats;

/* u8g2 is retained only for framebuffer/geometry/font rendering.
 * Direct u8g2 HW byte transfers are deliberately disabled: all transfers
 * must pass through the asynchronous OLED service + SPI1_DmaTxStart().
 */
static uint8_t OLED_U8X8Byte(u8x8_t *u8x8,uint8_t msg,uint8_t arg_int,void *arg_ptr)
{
    (void)u8x8;(void)arg_int;(void)arg_ptr;
    if(msg==U8X8_MSG_BYTE_SEND)return 0u;
    return 1u;
}
static uint8_t OLED_U8X8Gpio(u8x8_t *u8x8,uint8_t msg,uint8_t arg_int,void *arg_ptr)
{
    (void)u8x8;(void)arg_int;(void)arg_ptr;
    /* u8g2_Setup does not request delays; hardware init is explicit below. */
    if(msg==U8X8_MSG_DELAY_MILLI)return 0u;
    return 1u;
}
static uint8_t oled_start(const uint8_t *data,uint16_t length,
                          uint8_t data_mode,uint8_t phase)
{
    if(s_dma_phase!=OLED_PHASE_IDLE || SPI1_DmaTxState()!=SPI1_DMA_IDLE)
    {
        ++s_stats.dma_busy;
        return 0u;
    }
    if(data_mode)LL_GPIO_SetOutputPin(D_C_GPIO_Port,D_C_Pin);
    else LL_GPIO_ResetOutputPin(D_C_GPIO_Port,D_C_Pin);
    s_dma_started_ms=HAL_GetTick();
    if(!SPI1_DmaTxStart(data,length))
    {
        ++s_stats.dma_start_failed;
        /* HAL may have rejected DMA after changing its internal state.
         * Request recovery asynchronously, without blocking this pass. */
        if(SPI1_DmaTxState()==SPI1_DMA_ERROR)
            (void)SPI1_DmaTxAbortAsync();
        return 0u;
    }
    s_dma_phase=phase;
    return 1u;
}
static void oled_failed(uint8_t phase)
{
    s_dma_phase=OLED_PHASE_IDLE;
    if(phase==OLED_PHASE_INIT)
    {
        ++s_stats.init_timeouts;
        if(++s_init_attempts>=OLED_INIT_MAX_ATTEMPTS)
        {
            s_init_pending=0u;
            s_init_failed=1u;
        }
    }
    /* Runtime transmission preserves current page and retries next pass. */
}
uint8_t SSD1315_Display_Begin(u8g2_t *screen)
{
    if(screen==0)return 0u;
    s_screen=screen;
    s_has_pending=0u;s_active_frame=0u;s_page=0u;
    s_power_save=0u;s_power_requested=0u;s_power_dirty=0u;
    s_contrast_dirty=0u;s_contrast=0u;s_transmitted_value=0u;
    s_ready=0u;s_init_pending=1u;s_init_failed=0u;s_init_attempts=0u;
    s_dma_phase=OLED_PHASE_IDLE;s_frames=0u;
    memset(&s_stats,0,sizeof(s_stats));
    /* R2 rotation preserved. No u8g2_InitDisplay / u8g2_SetPowerSave:
     * those call the synchronous u8x8 byte+delay callbacks. */
    u8g2_Setup_ssd1315_128x64_f(screen,U8G2_R2,OLED_U8X8Byte,OLED_U8X8Gpio);
    u8g2_ClearBuffer(screen);
    return 1u;
}
SSD1315_InitState SSD1315_Display_InitState(void)
{
    if(s_ready)return SSD1315_INIT_READY;
    if(s_init_failed)return SSD1315_INIT_FAILED;
    return SSD1315_INIT_PENDING;
}
void SSD1315_Display_RequestFlush(const uint8_t *framebuffer)
{
    if(!s_ready || !framebuffer)return;
    memcpy(s_pending,framebuffer,SSD1315_FB_SIZE);
    s_has_pending=1u;
}
void SSD1315_Display_RequestContrast(uint8_t contrast)
{
    s_contrast=contrast;s_contrast_dirty=1u;
}
void SSD1315_Display_RequestPowerSave(uint8_t enable)
{
    s_power_requested=enable?1u:0u;
    s_power_dirty=1u;
}
void SSD1315_Display_Service(void)
{
    SPI1_DmaState status;
    uint8_t phase;
    if(!s_screen || s_init_failed)return;
    if(s_dma_phase!=OLED_PHASE_IDLE)
    {
        phase=s_dma_phase;
        status=SPI1_DmaTxState();
        if(status==SPI1_DMA_BUSY)
        {
            if((uint32_t)(HAL_GetTick()-s_dma_started_ms)<=OLED_TX_TIMEOUT_MS)
                return;
            ++s_stats.dma_runtime_timeouts;
            if(!SPI1_DmaTxAbortAsync())
                oled_failed(phase);
            return;
        }
        if(status==SPI1_DMA_ABORTING)return;
        if(status==SPI1_DMA_ERROR)
        {
            ++s_stats.dma_irq_errors;
            if(!SPI1_DmaTxAbortAsync())oled_failed(phase);
            return;
        }
        if(status==SPI1_DMA_IDLE)
        {
            /* An asynchronous abort completed; retry this transaction. */
            oled_failed(phase);
            return;
        }
        if(status!=SPI1_DMA_DONE)return;
        (void)SPI1_DmaTxRelease();
        s_dma_phase=OLED_PHASE_IDLE;
        switch(phase)
        {
        case OLED_PHASE_INIT:
            s_init_pending=0u;
            s_ready=1u;
            return;
        case OLED_PHASE_PAGE_CMD:
            (void)oled_start(&s_active[(uint16_t)s_page*SSD1315_WIDTH],
                             SSD1315_WIDTH,1u,OLED_PHASE_PAGE_DATA);
            return;
        case OLED_PHASE_PAGE_DATA:
            if(++s_page>=8u)
            {
                s_active_frame=0u;s_page=0u;++s_frames;
            }
            return;
        case OLED_PHASE_POWER:
            s_power_save=s_transmitted_value;
            s_power_dirty=(s_power_requested!=s_transmitted_value);
            return;
        case OLED_PHASE_CONTRAST:
            s_contrast_dirty=(s_contrast!=s_transmitted_value);
            return;
        default:return;
        }
    }
    if(s_init_pending)
    {
        /* Do not burn through retries while the preceding DMA abort runs. */
        status=SPI1_DmaTxState();
        if(status==SPI1_DMA_ABORTING || status==SPI1_DMA_BUSY)return;
        if(status==SPI1_DMA_ERROR)
        {
            (void)SPI1_DmaTxAbortAsync();
            return;
        }
        /* At most one start attempt per cooperative pass. */
        if(!oled_start(s_init_cmd,sizeof(s_init_cmd),0u,OLED_PHASE_INIT))
        {
            if(++s_init_attempts>=OLED_INIT_MAX_ATTEMPTS)
            {s_init_pending=0u;s_init_failed=1u;}
        }
        return;
    }
    if(!s_ready)return;
    if(s_active_frame)
    {
        s_cmd[0]=(uint8_t)(0xB0u|s_page);
        s_cmd[1]=0x00u;s_cmd[2]=0x10u;
        (void)oled_start(s_cmd,3u,0u,OLED_PHASE_PAGE_CMD);
        return;
    }
    if(s_power_dirty)
    {
        s_transmitted_value=s_power_requested;
        s_cmd[0]=s_transmitted_value?0xAEu:0xAFu;
        (void)oled_start(s_cmd,1u,0u,OLED_PHASE_POWER);
        return;
    }
    if(s_contrast_dirty)
    {
        s_transmitted_value=s_contrast;
        s_cmd[0]=0x81u;s_cmd[1]=s_transmitted_value;
        (void)oled_start(s_cmd,2u,0u,OLED_PHASE_CONTRAST);
        return;
    }
    if(s_power_save || !s_has_pending)return;
    memcpy(s_active,s_pending,SSD1315_FB_SIZE);
    s_has_pending=0u;s_active_frame=1u;s_page=0u;
}
uint8_t SSD1315_Display_IsReady(void){return s_ready;}
uint32_t SSD1315_Display_GetFrameCount(void){return s_frames;}
void SSD1315_Display_GetStats(SSD1315_DisplayStats *out)
{
    if(out)*out=s_stats;
}
