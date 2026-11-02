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
#include <stdio.h>
#include "console_cli.h"
#include "algorithm/pd_diag.h"
#include "algorithm/pd_input.h"
#include "framework/beep.h"
#include "temp_service.h"
#include "PowerControl/heater_control.h"
#include "PowerControl/heater_budget.h"

static coro_scheduler_t s_scheduler;
static ui_t s_ui;

THRD_DECLARE(thread_ui)
{
    THRD_BEGIN;
    /* OLED RES# follows shared MCU NRST. External VCC may require its own
     * power sequencing; wait cooperatively without starving USB-PD. */
    THRD_DELAY(250u);
    if(!diapInit())
    {
        printf("[OLED] begin rejected\r\n");
        while(1) { THRD_DELAY(1000u); }
    }
    /* Cooperative polling, not HAL_Delay() or DMA busy-wait.
     * Display_Service runs after each scheduler pass. */
    THRD_UNTIL(SSD1315_Display_InitState()!=SSD1315_INIT_PENDING);
    if(SSD1315_Display_InitState()!=SSD1315_INIT_READY)
    {
        printf("[OLED] async init failed\r\n");
        while(1) { THRD_DELAY(1000u); }
    }
    MiaoUi_Setup(&s_ui);
    while(1)
    {
        THRD_DELAY(20u);
        if(!pm_api_ui_should_block() && !pm_api_is_unstable_wake())
        {
            IH_Statistics_Poll();
            ui_loop(&s_ui);
        }
    }
    THRD_END;
}

THRD_DECLARE(thread_input_pm)
{
    static uint8_t was_off, wake;
    static uint32_t off_started, now;
    THRD_BEGIN;
    was_off=0u;
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
            /* One debounced physical press refreshes the PM countdown and
             * optionally triggers a single click. Auto-repeat generates no
             * extra sound; the screen-off wake press is deliberately silent. */
            if(Buttons_TakePress())
            {
                pm_api_refresh_idle();
                beep_key_click();
            }
            (void)Buttons_TakeWake();
        }
        THRD_DELAY(10u);
    }
    THRD_END;
}

THRD_DECLARE(thread_beep)
{
    THRD_BEGIN;
    while(1)
    {
        beep_poll(TIME_Millis());
        THRD_YIELD;
    }
    THRD_END;
}
/* Always runs regardless of main page visibility, PM UI_OFF, or OLED
 * initialization. The timer/PID state is owned by this foreground thread,
 * not by the 20kHz DMA ISR. */
THRD_DECLARE(thread_heater)
{
    static PD_InputSnapshot pd;
    static PD_InputContract contract;
    static HeaterObservation observation;
    static TempService_Snapshot temperature;
    THRD_BEGIN;
    while(1)
    {
        PD_Input_GetSnapshot(&pd);
        observation=Heater_BudgetClassify(pd.bus_mv,pd.mode,
            PD_Input_GetActiveContract(&contract),
            contract.voltage_mv,contract.current_ma);
        TempService_GetSnapshot(&temperature);
        observation.pot_valid=(temperature.valid&TEMP_VALID_POT)!=0u;
        observation.sys_valid=(temperature.valid&TEMP_VALID_SYS)!=0u;
        observation.pot_deci_c=temperature.pot_deci_c;
        observation.sys_deci_c=temperature.sys_deci_c;
        observation.temperature_updated_ms=temperature.updated_ms;
        Heater_Poll(TIME_Millis(),&observation);
        THRD_DELAY(100u);
    }
    THRD_END;
}

/* Last task, after all useful foreground work. SysTick interrupts at 1ms.
 * Keep PM in this idle thread, even while MiaoUI is not running. A WFI
 * returns on any enabled interrupt and THRD_YIELD resumes the scheduler. */
THRD_DECLARE(thread_idle)
{
    static uint32_t last_pm_ms,now;
    THRD_BEGIN;
    last_pm_ms=TIME_Millis()-50u;
    while(1)
    {
        now=TIME_Millis();
        if((uint32_t)(now-last_pm_ms)>=50u)
        {
            last_pm_ms=now;
            pm_api_poll();
        }
        if(__get_PRIMASK()==0u &&
           (SysTick->CTRL&SysTick_CTRL_TICKINT_Msk)!=0u)
        {
            __DSB();
            __WFI();
            __ISB();
        }
        THRD_YIELD;
    }
    THRD_END;
}
static const coro_thread_fn_t s_threads[]={
    thread_input_pm,thread_ui,thread_beep,thread_heater,thread_idle
};
static coro_pt_t s_states[sizeof(s_threads)/sizeof(s_threads[0])];

void APP_Tasks_Init(void)
{
    Buttons_Init();
    pm_api_init();
    beep_init();
    Heater_Init();
    CoroOS_Init(&s_scheduler,s_threads,s_states,
                (uint8_t)(sizeof(s_threads)/sizeof(s_threads[0])));
}
void APP_Tasks_RunOnce(void)
{
    /* Deferred PD diagnostic text; no UART transmission in PD callbacks. */
    g_uart_boot_stage=5u;
    PD_Input_EPR_Poll(); /* foreground-only one-shot EPR PE requests */
    PD_Diag_Service();
    ConsoleCLI_Poll();
    DebugUART_Service();
    TempService_Poll(); /* 10Hz; never in the 20kHz ADC ISR */
    CoroOS_RunOnce(&s_scheduler);
    SSD1315_Display_Service();
}
