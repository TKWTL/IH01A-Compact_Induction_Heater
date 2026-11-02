/* IH01A minimal system framework, based on Collar-Radio
 * system_operation / system_statistic: reset and system uptime only.
 * PM, analog telemetry and heater safety remain in their own modules.
 */
#include "framework/system.h"
#include "framework/time_service.h"
#include "framework/beep.h"
#include "main.h"
#include "core/ui.h"
#include <stdio.h>

static ui_item_t s_reset_item;
static ui_item_t s_click_item,s_mute_item,s_music_item;
static ui_data_t s_click_data,s_mute_data;
static ui_element_t s_click_element,s_mute_element;
static uint8_t s_click_on,s_mute_on;

/* Johann Pachelbel: Canon in D, public-domain first violin entry, bars 3-4.
 * Melodic opening: F#5 E5 D5 C#5 | B4 A4 B4 C#5 (short ringtone tempo).
 * All pitch calculations are compile-time ARR constants. */
static const beep_note_t s_music[]={
    {BEEP_FS5,320u},{BEEP_E5,320u},{BEEP_D5,320u},{BEEP_CS5,320u},
    {BEEP_B4,320u},{BEEP_A4,320u},{BEEP_B4,320u},{BEEP_CS5,320u},
    BEEP_END
};

static void system_click_changed(ui_t *ui)
{
    (void)ui;
    beep_set_key_enabled(s_click_on);
}

static void system_mute_changed(ui_t *ui)
{
    (void)ui;
    beep_set_muted(s_mute_on);
}

static void system_play_music(ui_t *ui)
{
    (void)ui;
    /* This user-triggered easter egg overrides mute, not Click Beep.
     * Update the switch's storage too, so the UI reflects the new state. */
    s_mute_on=0u;
    beep_set_muted(0u);
    (void)beep_enqueue(s_music);
}

static void system_add_beep_switch(ui_page_t *page,
                                   ui_item_t *item,ui_data_t *data,
                                   ui_element_t *element,uint8_t *flag,
                                   const char *name,ui_item_function callback)
{
    data->name=name;
    data->ptr=flag;
    data->function=callback;
    data->functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
    data->dataType=UI_DATA_SWITCH;
    data->actionType=UI_DATA_ACTION_RW;
    data->min=0;  /* independent toggle, not a radio group */
    data->max=0;
    data->step=0;
    data->decimals=0;
    element->data=data;
    Create_element(item,element);
    AddItem(name,UI_ITEM_DATA,0,item,page,0,0);
}
static ui_item_t s_uptime_item;
static char s_uptime_text[32];
static uint32_t s_last_seconds = 0xFFFFFFFFu;
static uint32_t s_last_poll_ms;
static uint8_t s_stat_ready;

/* This is deliberately a one-shot MiaoUI item, NOT a writable switch.
 * NVIC_SystemReset never returns; no OLED DMA completion is assumed. */
void System_Reset(void)
{
    /* The user-initiated reset follows the same safe PC6-off sequence. */
    IH_EmergencyReset();
}

static void System_ResetSelected(ui_t *ui)
{
    (void)ui;
    System_Reset();
}

void System_AddSettingsItems(ui_page_t *page)
{
    if(page == 0) return;
    s_click_on=beep_get_key_enabled();
    s_mute_on=beep_is_muted();
    system_add_beep_switch(page,&s_click_item,&s_click_data,
                           &s_click_element,&s_click_on,
                           " Click Beep",system_click_changed);
    system_add_beep_switch(page,&s_mute_item,&s_mute_data,
                           &s_mute_element,&s_mute_on,
                           " Mute Beep",system_mute_changed);
    AddItem(" Play Music",UI_ITEM_ONCE_FUNCTION,0,
            &s_music_item,page,0,system_play_music);
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
