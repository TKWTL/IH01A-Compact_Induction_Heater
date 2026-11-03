/* IH01A Settings -> Heater. Native MiaoUI switch/data items let the user
 * toggle the validated PC6 bench gate and edit phase/frequency directly.
 * All controls are in the cooperative foreground, never an ADC ISR.
 */
#include "heater_settings.h"
#include "PowerControl/heater_control.h"
#include "PowerControl/conv_controller.h"
#include "PowerControl/conv_phase.h"
#include "PowerControl/conv_current.h"
#include "PowerControl/conv_adc.h"
#include "PowerControl/conv_protection.h"
#include "PowerControl/conv_zcd.h"
#include "framework/pm_api.h"
#include "main.h"
#include <stdio.h>

static ui_page_t s_page;
static ui_item_t s_entry,s_back,s_pc6_item,s_phase_item,s_freq_item;
static ui_item_t s_ic2_item,s_lag_item,s_ct_item,s_zero_item,s_status_item;
static ui_data_t s_pc6_data,s_phase_data,s_freq_data;
static ui_element_t s_pc6_elem,s_phase_elem,s_freq_elem;
static uint8_t s_pc6;
static int s_phase,s_freq;
static int s_previous_phase,s_previous_freq;
static char s_ic2_text[26],s_lag_text[26],s_ct_text[26];
static char s_zero_text[26],s_status_text[26];
static uint32_t s_last_poll;
static uint8_t s_created;

static void readonly_item(ui_t *ui)
{
    if(ui && ui->action==UI_ACTION_NONE)ui->action=UI_ACTION_BACK;
}
static void pc6_clicked(ui_t *ui)
{
    HeaterSnapshot hs;
    (void)ui;
    if(!s_pc6)
    {
        ConvController_BenchPowerOff(); /* immediate, no scheduler latency */
        Heater_Stop();
    }
    else
    {
        Heater_GetSnapshot(&hs);
        /* The switch is an explicit RUN request, NOT an unsafe GPIO write.
         * All 12V PD / NTC / COMP / Break checks remain in BenchRunPoll. */
        if(hs.state==HEATER_FAULT)
        {
            s_pc6=0u;
            return;
        }
        Heater_SetMode(HEATER_MODE_POWER);
        Heater_StartConfirmed(HAL_GetTick());
    }
    pm_api_refresh_idle();
}
static void phase_changed(ui_t *ui)
{
    (void)ui;
    /* MiaoUI numeric widget wraps endpoints. Prevent a dangerous implicit
     * transition 180->0 or 0->180 while tuning live power. */
    if(s_previous_phase==180 && s_phase==0)s_phase=180;
    else if(s_previous_phase==0 && s_phase==180)s_phase=0;
    if(s_phase<0)s_phase=0;
    if(s_phase>180)s_phase=180;
    Heater_SetPhaseDegrees((uint16_t)s_phase);
    ConvController_SetPhaseDegrees((uint16_t)s_phase);
    s_previous_phase=s_phase;
    pm_api_refresh_idle();
}
static void frequency_changed(ui_t *ui)
{
    (void)ui;
    /* Live frequency edit: TIM1 ARR and CCR1/3/4 preload together at
     * one natural UEV, without dropping PC6 while timer is healthy.
     * Hardware Break/ADC protection remains active. */
    if(s_previous_freq==(int)CONV_FREQ_MAX_KHZ &&
       s_freq==(int)CONV_FREQ_MIN_KHZ)
        s_freq=s_previous_freq;
    else if(s_previous_freq==(int)CONV_FREQ_MIN_KHZ &&
            s_freq==(int)CONV_FREQ_MAX_KHZ)
        s_freq=s_previous_freq;
    if(!ConvController_SetFrequencyKhz((uint16_t)s_freq))
        s_freq=(int)ConvController_GetFrequencyKhz();
    s_previous_freq=s_freq;
    pm_api_refresh_idle();
}
void HeaterSettings_AddSettings(ui_page_t *settings)
{
    if(!settings)return;
    AddItem("-Heater",UI_ITEM_PARENTS,0,&s_entry,settings,&s_page,0);
    AddPage("[Heater]",&s_page,UI_PAGE_TEXT,settings);
    s_page.wideText=1u;
    AddItem("[Return]",UI_ITEM_RETURN,0,&s_back,&s_page,settings,0);

    s_pc6=0u;
    s_pc6_data.name="PC6 EN";
    s_pc6_data.ptr=&s_pc6;
    s_pc6_data.function=pc6_clicked;
    s_pc6_data.functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
    s_pc6_data.dataType=UI_DATA_SWITCH;
    s_pc6_data.actionType=UI_DATA_ACTION_RW;
    s_pc6_data.min=0;s_pc6_data.max=0;s_pc6_data.step=0;
    s_pc6_elem.data=&s_pc6_data;
    Create_element(&s_pc6_item,&s_pc6_elem);
    AddItem(" PC6 Enable",UI_ITEM_DATA,0,&s_pc6_item,&s_page,0,0);

    s_phase=0;s_previous_phase=0;
    s_phase_data.name="Phase deg";
    s_phase_data.ptr=&s_phase;
    s_phase_data.function=phase_changed;
    s_phase_data.functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
    s_phase_data.dataType=UI_DATA_INT;
    s_phase_data.actionType=UI_DATA_ACTION_RW;
    s_phase_data.min=0;s_phase_data.max=180;s_phase_data.step=10;
    s_phase_elem.data=&s_phase_data;
    Create_element(&s_phase_item,&s_phase_elem);
    AddItem(" Phase deg",UI_ITEM_DATA,0,&s_phase_item,&s_page,0,0);

    s_freq=200;s_previous_freq=200;
    s_freq_data.name="Freq kHz";
    s_freq_data.ptr=&s_freq;
    s_freq_data.function=frequency_changed;
    s_freq_data.functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
    s_freq_data.dataType=UI_DATA_INT;
    s_freq_data.actionType=UI_DATA_ACTION_RW;
    s_freq_data.min=(int)CONV_FREQ_MIN_KHZ;
    s_freq_data.max=(int)CONV_FREQ_MAX_KHZ;
    s_freq_data.step=(int)CONV_FREQ_STEP_KHZ;
    s_freq_elem.data=&s_freq_data;
    Create_element(&s_freq_item,&s_freq_elem);
    AddItem(" Freq kHz",UI_ITEM_DATA,0,&s_freq_item,&s_page,0,0);

    AddItem(s_ic2_text,UI_ITEM_WORD,0,&s_ic2_item,&s_page,0,readonly_item);
    AddItem(s_lag_text,UI_ITEM_WORD,0,&s_lag_item,&s_page,0,readonly_item);
    AddItem(s_ct_text,UI_ITEM_WORD,0,&s_ct_item,&s_page,0,readonly_item);
    AddItem(s_zero_text,UI_ITEM_WORD,0,&s_zero_item,&s_page,0,readonly_item);
    AddItem(s_status_text,UI_ITEM_WORD,0,&s_status_item,&s_page,0,readonly_item);
    s_created=1u;
    s_last_poll=HAL_GetTick()-100u;
    HeaterSettings_Poll();
}
void HeaterSettings_Poll(void)
{
    HeaterSnapshot heater;
    ConvProtectionSnapshot protection;
    ConvADC_Snapshot adc;
    ConvZcdSnapshot capture;
    uint32_t now=HAL_GetTick();
    uint32_t current_ma;
    if(!s_created || (uint32_t)(now-s_last_poll)<50u)return;
    s_last_poll=now;
    Heater_GetSnapshot(&heater);
    ConvProtection_GetSnapshot(&protection);
    ConvADC_CopySnapshot(&adc);
    ConvZcd_GetSnapshot(&capture);

    s_pc6=(uint8_t)((heater.state==HEATER_RUN_REQUESTED) ||
                     ConvController_OutputEnabled());
    s_phase=(int)heater.target_phase_deg;
    s_previous_phase=s_phase;
    s_freq=(int)ConvController_GetFrequencyKhz();
    s_previous_freq=s_freq;

    if(capture.valid && (uint32_t)(now-capture.last_ms)<=250u)
    {
        int lag=(int)capture.lag_deci_deg;
        unsigned mag=(unsigned)((lag<0)?-lag:lag);
        (void)snprintf(s_ic2_text,sizeof(s_ic2_text),
                       "IC2: %u raw",(unsigned)capture.capture_raw);
        (void)snprintf(s_lag_text,sizeof(s_lag_text),
                       "Lag: %c%u.%u deg",lag<0?'-':'+',
                       mag/10u,mag%10u);
    }
    else
    {
        (void)snprintf(s_ic2_text,sizeof(s_ic2_text),"IC2: --");
        (void)snprintf(s_lag_text,sizeof(s_lag_text),"Lag: --");
    }

    if(protection.zero_status==CONV_ZERO_READY && adc.frames &&
       ConvController_GetState()==CONV_STATE_ADC_MONITOR)
    {
        /* ADC low-pass value minus the original no-switching CT_RMS zero.
         * 0.25253 V/A is a channel gain; current is a display estimate.
         * Near ADC saturation the ampere estimate would under-report. */
        current_ma=ConvCurrent_CtRmsMilliAmp(
                         adc.filtered[CONV_ADC_CT_RMS],
                         protection.ct_zero_raw);
        if(adc.raw[CONV_ADC_CT_RMS]>=4090u)
            (void)snprintf(s_ct_text,sizeof(s_ct_text),"CT_RMS: SAT");
        else
            (void)snprintf(s_ct_text,sizeof(s_ct_text),
                           "CT_RMS:%lu.%03luA",
                           (unsigned long)(current_ma/1000u),
                           (unsigned long)(current_ma%1000u));
        (void)snprintf(s_zero_text,sizeof(s_zero_text),
                       "CT0:%u ADC:%u",
                       (unsigned)protection.ct_zero_raw,
                       (unsigned)adc.filtered[CONV_ADC_CT_RMS]);
    }
    else
    {
        (void)snprintf(s_ct_text,sizeof(s_ct_text),"CT_RMS: calibrating");
        (void)snprintf(s_zero_text,sizeof(s_zero_text),"CT0: --");
    }
    if(heater.state==HEATER_FAULT || protection.fault_mask)
        (void)snprintf(s_status_text,sizeof(s_status_text),
                       "Fault: %08lX",(unsigned long)protection.fault_mask);
    else if(ConvController_OutputEnabled())
        (void)snprintf(s_status_text,sizeof(s_status_text),
                       "PC6: Y/%c RUN",
                       ConvController_OutputPinLevel()?'Y':'n');
    else if(heater.state==HEATER_RUN_REQUESTED)
        (void)snprintf(s_status_text,sizeof(s_status_text),
                       "PC6: N STARTING");
    else if(ConvController_BenchBlockReason()!=CONV_BENCH_OK)
        (void)snprintf(s_status_text,sizeof(s_status_text),
                       "Stopped R:%u",(unsigned)ConvController_BenchBlockReason());
    else
        (void)snprintf(s_status_text,sizeof(s_status_text),"State: OFF");
}
