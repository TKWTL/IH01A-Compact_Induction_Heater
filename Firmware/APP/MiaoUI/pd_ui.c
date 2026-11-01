#include "pd_ui.h"
#include "algorithm/pd_input.h"
#include "core/ui.h"
#include "main.h"
#include <stdio.h>

static ui_page_t s_page;
static ui_item_t s_parent,s_back,s_retry,s_mode,s_voltage,s_target;
static char s_mode_label[25],s_voltage_label[25],s_target_label[25];
static uint32_t s_poll_ms;
static uint8_t s_menu;

static void pd_readonly(ui_t *ui)
{
    if(ui&&ui->action==UI_ACTION_NONE)ui->action=UI_ACTION_BACK;
}
static void pd_retry(ui_t *ui)
{
    (void)ui;
    if(!PD_Input_Manual())
        (void)snprintf(s_mode_label,sizeof(s_mode_label),"PD: not ready");
}
void PD_UI_AddSettings(ui_page_t *settings)
{
    if(!settings)return;
    AddItem("-PD",UI_ITEM_PARENTS,0,&s_parent,settings,&s_page,0);
    AddPage("[PD]",&s_page,UI_PAGE_TEXT,settings);
    s_page.wideText=1u;
    AddItem("[Return]",UI_ITEM_RETURN,0,&s_back,&s_page,settings,0);
    AddItem(" Retry PD",UI_ITEM_ONCE_FUNCTION,0,&s_retry,&s_page,0,pd_retry);
    AddItem(s_mode_label,UI_ITEM_WORD,0,&s_mode,&s_page,0,pd_readonly);
    AddItem(s_voltage_label,UI_ITEM_WORD,0,&s_voltage,&s_page,0,pd_readonly);
    AddItem(s_target_label,UI_ITEM_WORD,0,&s_target,&s_page,0,pd_readonly);
    s_menu=1u;
    PD_UI_Poll();
}
void PD_UI_Poll(void)
{
    PD_InputSnapshot snap;
    uint32_t ms=HAL_GetTick();
    const char *mode;
    if(!s_menu||(uint32_t)(ms-s_poll_ms)<100u)return;
    s_poll_ms=ms;
    PD_Input_GetSnapshot(&snap);
    switch(snap.mode)
    {
    case PD_INPUT_PD:mode="PD";break;
    case PD_INPUT_DCIN:mode="DCin";break;
    case PD_INPUT_WAIT:mode="PD Wait";break;
    default:mode="No Input";break;
    }
    (void)snprintf(s_mode_label,sizeof(s_mode_label),"Input: %s",mode);
    (void)snprintf(s_voltage_label,sizeof(s_voltage_label),
          "BUS: %lu.%02luV",
          (unsigned long)(snap.bus_mv/1000u),
          (unsigned long)((snap.bus_mv%1000u)/10u));
    (void)snprintf(s_target_label,sizeof(s_target_label),
          "Select: %lu.%02luV",
          (unsigned long)(snap.selected_mv/1000u),
          (unsigned long)((snap.selected_mv%1000u)/10u));
}
