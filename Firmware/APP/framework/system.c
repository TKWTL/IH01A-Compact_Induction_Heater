/* IH01A minimal system framework, based on Collar-Radio
 * system_operation / system_statistic: reset and system uptime only.
 * PM, analog telemetry and heater safety remain in their own modules.
 */
#include "framework/system.h"
#include "framework/time_service.h"
#include "main.h"
#include "core/ui.h"
#include <stdio.h>

static ui_item_t s_reset_item;
static ui_item_t s_uptime_item;
static char s_uptime_text[32];
static uint32_t s_last_seconds = 0xFFFFFFFFu;
static uint32_t s_last_poll_ms;
static uint8_t s_stat_ready;

/* This is deliberately a one-shot MiaoUI item, NOT a writable switch.
 * NVIC_SystemReset never returns; no OLED DMA completion is assumed. */
void System_Reset(void)
{
    NVIC_SystemReset();
    for(;;) { }
}

static void System_ResetSelected(ui_t *ui)
{
    (void)ui;
    System_Reset();
}

void System_AddSettingsItems(ui_page_t *page)
{
    if(page == 0) return;
    /* The caller MUST insert this after all other Settings/System items. */
    AddItem(" System Reset", UI_ITEM_ONCE_FUNCTION, 0,
            &s_reset_item, page, 0, System_ResetSelected);
}

static void System_UptimeReadOnly(ui_t *ui)
{
    if(ui != 0 && ui->action == UI_ACTION_NONE)
        ui->action = UI_ACTION_BACK;
}

void System_AddStatisticsItems(ui_page_t *page)
{
    if(page == 0) return;
    (void)snprintf(s_uptime_text, sizeof(s_uptime_text),
                   "Uptime: --d --:--:--");
    AddItem(s_uptime_text, UI_ITEM_WORD, 0, &s_uptime_item,
            page, 0, System_UptimeReadOnly);
    s_last_seconds = 0xFFFFFFFFu;
    s_last_poll_ms = HAL_GetTick()-100u;
    s_stat_ready = 1u;
    System_Poll();
}

void System_Poll(void)
{
    uint32_t seconds;
    uint32_t now;
    char duration[25];
    if(!s_stat_ready) return;
    now = HAL_GetTick();
    if((uint32_t)(now-s_last_poll_ms)<100u) return;
    s_last_poll_ms = now;

    if(!Time_GetUptimeSeconds(&seconds))
    {
        s_last_seconds = 0xFFFFFFFFu;
        (void)snprintf(s_uptime_text, sizeof(s_uptime_text),
                       "Uptime: RTC error");
        return;
    }
    if(seconds == s_last_seconds) return;
    s_last_seconds = seconds;
    Time_FormatDuration(seconds, duration, sizeof(duration));
    (void)snprintf(s_uptime_text, sizeof(s_uptime_text),
                   "Uptime: %s", duration);
}
