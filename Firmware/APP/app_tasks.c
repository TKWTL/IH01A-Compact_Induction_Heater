/* STM32G071 cooperative application shell.
 * The ST non-RTOS USBPD_DPM_Run() loop does not return. Each pass calls
 * APP_Tasks_RunOnce() through USBPD_DPM_UserExecute() after CAD/PE.
 * One OLED page per pass maximum; heater PWM remains OFF.
 */
#include "app_tasks.h"
#include "coroOS.h"
#include "time_api.h"
#include "buttons.h"
#include "ssd1315_display.h"
#include "framework/pm_api.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "ui_conf.h"
#include "usart.h"
#include "console_cli.h"
#include "algorithm/pd_diag.h"
#include "temp_service.h"
#include "ih01a_recovery_profile.h"
#include <stdio.h>

volatile uint32_t g_recovery_ui_stage;
volatile uint32_t g_recovery_ui_updates;
static coro_scheduler_t s_scheduler;
static ui_t s_ui;
#if IH01A_RECOVERY_5V_ONLY
static uint32_t s_recovery_last_health;
#endif

THRD_DECLARE(thread_ui)
{
    THRD_BEGIN;
    g_recovery_ui_stage=1u;
    /* OLED RES# follows shared MCU NRST. External VCC may require its own
     * power sequencing; wait cooperatively without starving USB-PD. */
    THRD_DELAY(250u);
    g_recovery_ui_stage=2u; /* entering OLED u8g2 init */
    if(!diapInit())
    {
        g_recovery_ui_stage=6u; /* invalid begin */
        printf("[OLED] begin rejected\r\n");
        while(1) { THRD_DELAY(1000u); }
    }
    /* Hardware initialization proceeds in one DMA state per scheduler pass. */
    THRD_UNTIL(SSD1315_Display_InitState()!=SSD1315_INIT_PENDING);
    if(SSD1315_Display_InitState()!=SSD1315_INIT_READY)
    {
        g_recovery_ui_stage=6u; /* retries exhausted */
        printf("[OLED] async init failed\r\n");
        while(1) { THRD_DELAY(1000u); }
    }
    g_recovery_ui_stage=3u; /* OLED init completed */
    MiaoUi_Setup(&s_ui);
    g_recovery_ui_stage=4u; /* UI tree ready */
    while(1)
    {
        THRD_DELAY(20u);
        if(!pm_api_ui_should_block() && !pm_api_is_unstable_wake())
        {
            IH_Statistics_Poll();
            ui_loop(&s_ui);
            ++g_recovery_ui_updates;
        }
    }
    THRD_END;
}

THRD_DECLARE(thread_input_pm)
{
    static uint8_t was_off, wake;
    static uint32_t off_started, last_poll, now;
    THRD_BEGIN;
    was_off=0u;
    last_poll=TIME_Millis();
    while(1)
    {
        Buttons_Poll10ms();
        now=TIME_Millis();
        if(pm_api_ui_should_block())
        {
            if(!was_off)
            {
                was_off=1u;
                off_started=now;
                Buttons_ClearActions();
            }
            wake=Buttons_TakeWake();
            (void)Buttons_TakePress();
            if(wake && ((uint32_t)(now-off_started)>=300u))
            {
                /* Do not forward the wake key or its release to MiaoUI. */
                Buttons_SuppressHeld();
                pm_api_refresh_idle();
            }
            else
                Buttons_ClearActions();
        }
        else
        {
            was_off=0u;
            if(Buttons_TakePress()) pm_api_refresh_idle();
            (void)Buttons_TakeWake();
        }
        if((uint32_t)(now-last_poll)>=50u)
        {
            last_poll=now;
            pm_api_poll();
        }
        THRD_DELAY(10u);
    }
    THRD_END;
}

static const coro_thread_fn_t s_threads[] = {thread_input_pm,thread_ui};
static coro_pt_t s_states[2];

void APP_Tasks_Init(void)
{
    Buttons_Init();
    pm_api_init();
    CoroOS_Init(&s_scheduler,s_threads,s_states,2u);
}
void APP_Tasks_RunOnce(void)
{
    /* Deferred PD diagnostic text; no UART transmission in PD callbacks. */
    g_uart_boot_stage=5u;
    PD_Diag_Service();
    ConsoleCLI_Poll();
    DebugUART_Service();
    TempService_Poll(); /* 10Hz; never in the 20kHz ADC ISR */
    CoroOS_RunOnce(&s_scheduler);
    SSD1315_Display_Service();
#if IH01A_RECOVERY_5V_ONLY
    /* 3-second low-rate heartbeat: observe continuous execution and
     * compare UI iterations with frames actually sent to the panel. */
    {
        uint32_t now=TIME_Millis();
        if((uint32_t)(now-s_recovery_last_health)>=3000u)
        {
            s_recovery_last_health=now;
            printf("[RECOVERY %lu] ui_stage=%lu updates=%lu frames=%lu flush=%lu\r\n",
                   (unsigned long)now,
                   (unsigned long)g_recovery_ui_stage,
                   (unsigned long)g_recovery_ui_updates,
                   (unsigned long)SSD1315_Display_GetFrameCount(),
                   (unsigned long)Disp_GetFlushCount());
        }
    }
#endif
}
