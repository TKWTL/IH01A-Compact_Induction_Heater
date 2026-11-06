/* Statistics -> System: static PAGE_TEXT ADC readings, no heap.
 * IBUS current is shown on Heater pages, not duplicated as pin voltage.
 * Other voltage channels report ADC pin voltage (VBUS scaled x16).
 * ISR_LOAD reports filtered duty percentage from 0..3.3V PWM average;
 * its 2.5V ADC reference inherently limits measurable duty to ~75.8%.
 * The STM32G071 internal temperature uses two-point factory calibration.
 */
#include "dashboard.h"
#include "conv_adc.h"
#include "conv_controller.h"
#include "conv_protection.h"
#include "temp_service.h"
#include "core/ui.h"
#include "main.h"
#include <stdio.h>
#include <stdint.h>

#define POWER_DASHBOARD_ROW_BYTES 28u
static ui_item_t s_rows[CONV_ADC_COUNT];
static char s_labels[CONV_ADC_COUNT][POWER_DASHBOARD_ROW_BYTES];
#define PROTECTION_ROWS 5u
static ui_item_t s_prot_items[PROTECTION_ROWS];
static char s_prot_text[PROTECTION_ROWS][POWER_DASHBOARD_ROW_BYTES];
static const char *const s_names[CONV_ADC_COUNT] = {
    "CT_RMS", "", "NTC_POT", "VBUS",
    "NTC_SYS", "VBIAS", "ISR_LOAD", "MCU"
};
static uint32_t s_last_update;
static uint32_t s_last_frames=0xFFFFFFFFu;
static uint8_t s_initialized;
static uint8_t s_last_state=255u;

static void PowerDashboard_Temp(char *dst, unsigned capacity,
                                const char *name, int32_t deci,
                                uint8_t valid)
{
    uint32_t magnitude;
    if(!valid)
    {
        (void)snprintf(dst,capacity,"%s: FAULT",name);
        return;
    }
    magnitude=(uint32_t)(deci<0?-deci:deci);
    (void)snprintf(dst,capacity,"%s: %s%lu.%luC",
                   name,deci<0?"-":"",
                   (unsigned long)(magnitude/10u),
                   (unsigned long)(magnitude%10u));
}

static void PowerDashboard_ReadOnly(ui_t *ui)
{
    if(ui!=0 && ui->action==UI_ACTION_NONE)
        ui->action=UI_ACTION_BACK;
}

void PowerDashboard_Poll(void)
{
    ConvADC_Snapshot snapshot;
    TempService_Snapshot temp;
    ConvProtectionSnapshot protection;
    uint32_t now=HAL_GetTick();
    ConvState state=ConvController_GetState();
    unsigned i;
    if(!s_initialized || (uint32_t)(now-s_last_update)<100u) return;
    s_last_update=now;
    ConvADC_CopySnapshot(&snapshot);
    TempService_GetSnapshot(&temp);
    ConvProtection_GetSnapshot(&protection);
    if(s_last_frames==snapshot.frames && s_last_state==(uint8_t)state)
        return;
    s_last_frames=snapshot.frames;
    s_last_state=(uint8_t)state;
    (void)snprintf(s_prot_text[0],sizeof(s_prot_text[0]),
                   "ZERO: %s %u/%u",
                   protection.zero_status==CONV_ZERO_READY?"OK":
                   protection.zero_status==CONV_ZERO_FAILED?"FAIL":
                   protection.zero_status==CONV_ZERO_COLLECT?"AVG":"WAIT",
                   (unsigned)protection.zero_samples,
                   (unsigned)CONV_ZERO_AVG_FRAMES);
    (void)snprintf(s_prot_text[1],sizeof(s_prot_text[1]),
                   "IB0:%u",
                   (unsigned)protection.ibus_zero_raw);
    (void)snprintf(s_prot_text[2],sizeof(s_prot_text[2]),
                   "DAC: %u/%u %c",
                   (unsigned)protection.dac1_code,
                   (unsigned)protection.dac2_code,
                   protection.comp_armed?'Y':'-');
    (void)snprintf(s_prot_text[3],sizeof(s_prot_text[3]),
                   "AWD2: %u-%u %c",
                   (unsigned)protection.awd2_low_code,
                   (unsigned)protection.awd2_high_code,
                   protection.awd2_enabled?'Y':'N');
    (void)snprintf(s_prot_text[4],sizeof(s_prot_text[4]),
                   "PROT: 0x%08lX",(unsigned long)protection.fault_mask);

    if(state!=CONV_STATE_ADC_MONITOR || snapshot.frames==0u)
    {
        (void)snprintf(s_labels[0],sizeof(s_labels[0]),"%s",
           state==CONV_STATE_FAULT ? "ADC FAULT" : "Waiting for ADC");
        for(i=1u;i<CONV_ADC_COUNT;++i) s_labels[i][0]='\0';
        return;
    }

    for(i=0u;i<CONV_ADC_COUNT;++i)
    {
        if(i==CONV_ADC_IBUS)continue; /* IBUS moved to Heater page. */
        if(i==CONV_ADC_MCU_TEMP)
        {
            PowerDashboard_Temp(s_labels[i],sizeof(s_labels[i]),"MCU",
                                temp.mcu_deci_c,
                                (temp.valid&TEMP_VALID_MCU)!=0u);
        }
        else if(i==CONV_ADC_NTC_SYS)
        {
            PowerDashboard_Temp(s_labels[i],sizeof(s_labels[i]),"NTC_SYS",
                                temp.sys_deci_c,
                                (temp.valid&TEMP_VALID_SYS)!=0u);
        }
        else if(i==CONV_ADC_NTC_POT)
        {
            PowerDashboard_Temp(s_labels[i],sizeof(s_labels[i]),"NTC_POT",
                                temp.pot_deci_c,
                                (temp.valid&TEMP_VALID_POT)!=0u);
        }
        else if(i==CONV_ADC_ISR_LOAD)
        {
            /* Duty 0..100% maps to 0..3300mV, ADC full scale is 2500mV.
             * Compute tenths of a percent directly from the 12-bit code. */
            uint32_t duty_deci=(uint32_t)(((uint32_t)snapshot.raw[i]*25000u+
                                          (4095u*33u)/2u)/(4095u*33u));
            (void)snprintf(s_labels[i],sizeof(s_labels[i]),
                 "ISR_LOAD: %u.%u%%",
                 (unsigned)(duty_deci/10u),(unsigned)(duty_deci%10u));
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
        if(i==CONV_ADC_IBUS)continue;
        AddItem(s_labels[i],UI_ITEM_WORD,0,&s_rows[i],
                page,0,PowerDashboard_ReadOnly);
    }
    for(i=0u;i<PROTECTION_ROWS;++i)
    {
        s_prot_text[i][0]='\0';
        AddItem(s_prot_text[i],UI_ITEM_WORD,0,&s_prot_items[i],
                page,0,PowerDashboard_ReadOnly);
    }
    s_last_frames=0xFFFFFFFFu;
    s_last_state=255u;
    s_last_update=HAL_GetTick()-100u;
    s_initialized=1u;
    PowerDashboard_Poll();
}
