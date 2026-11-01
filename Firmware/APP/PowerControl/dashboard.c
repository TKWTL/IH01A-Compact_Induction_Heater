/* Statistics -> System: static PAGE_TEXT ADC readings, no heap.
 * Only reports ADC pin voltage; sensor scaling is not guessed.
 * The STM32G071 internal temperature uses two-point factory calibration.
 */
#include "dashboard.h"
#include "conv_adc.h"
#include "conv_controller.h"
#include "core/ui.h"
#include "main.h"
#include <stdio.h>
#include <stdint.h>

#define POWER_DASHBOARD_ROW_BYTES 28u
static ui_item_t s_rows[CONV_ADC_COUNT];
static char s_labels[CONV_ADC_COUNT][POWER_DASHBOARD_ROW_BYTES];
static const char *const s_names[CONV_ADC_COUNT] = {
    "CT_RMS", "IBUS", "NTC_POT", "VBUS",
    "NTC_SYS", "VBIAS", "ISR_LOAD", "MCU"
};
static uint32_t s_last_update;
static uint32_t s_last_frames=0xFFFFFFFFu;
static uint8_t s_initialized;
static uint8_t s_last_state=255u;

static void PowerDashboard_ReadOnly(ui_t *ui)
{
    if(ui!=0 && ui->action==UI_ACTION_NONE)
        ui->action=UI_ACTION_BACK;
}

void PowerDashboard_Poll(void)
{
    ConvADC_Snapshot snapshot;
    uint32_t now=HAL_GetTick();
    ConvState state=ConvController_GetState();
    unsigned i;
    if(!s_initialized || (uint32_t)(now-s_last_update)<100u) return;
    s_last_update=now;
    ConvADC_CopySnapshot(&snapshot);
    if(s_last_frames==snapshot.frames && s_last_state==(uint8_t)state)
        return;
    s_last_frames=snapshot.frames;
    s_last_state=(uint8_t)state;

    if(state!=CONV_STATE_ADC_MONITOR || snapshot.frames==0u)
    {
        (void)snprintf(s_labels[0],sizeof(s_labels[0]),"%s",
           state==CONV_STATE_FAULT ? "ADC FAULT" : "Waiting for ADC");
        for(i=1u;i<CONV_ADC_COUNT;++i) s_labels[i][0]='\0';
        return;
    }

    for(i=0u;i<CONV_ADC_COUNT;++i)
    {
        if(i==CONV_ADC_MCU_TEMP)
        {
            int32_t temp;
            if(!ConvADC_McuTempDeciC(snapshot.raw[i], &temp))
                (void)snprintf(s_labels[i],sizeof(s_labels[i]),
                               "MCU: invalid cal");
            else
            {
                uint32_t magnitude=(uint32_t)(temp<0 ? -temp : temp);
                (void)snprintf(s_labels[i],sizeof(s_labels[i]),
                       "MCU: %s%u.%uC",temp<0?"-":"",
                       (unsigned)(magnitude/10u),
                       (unsigned)(magnitude%10u));
            }
        }
        else
        {
            uint32_t mv=(uint32_t)ConvADC_PinMillivolts(snapshot.raw[i]);
            if(i==CONV_ADC_VBUS) mv*=16u; /* actual BUS, not ADC pin */
            (void)snprintf(s_labels[i],sizeof(s_labels[i]),
                 "%s: %u.%03uV",s_names[i],
                 (unsigned)(mv/1000u),(unsigned)(mv%1000u));
        }
    }
}

void PowerDashboard_AddItems(ui_page_t *page)
{
    unsigned i;
    for(i=0u;i<CONV_ADC_COUNT;++i)
    {
        s_labels[i][0]='\0';
        AddItem(s_labels[i],UI_ITEM_WORD,0,&s_rows[i],
                page,0,PowerDashboard_ReadOnly);
    }
    s_last_frames=0xFFFFFFFFu;
    s_last_state=255u;
    s_last_update=HAL_GetTick()-100u;
    s_initialized=1u;
    PowerDashboard_Poll();
}
