/* IH01A protection bootstrap. Legacy G474 Protection_Init2 was empty;
 * its HRTIM FLT1 and ADC watchdog code are not binary-compatible with G071.
 *
 * G071 CubeMX: COMP1 is connected to TIM1 BRK; COMP2 is not. DAC1 CH1/CH2
 * are selected as the COMP negative inputs, but the actual + input routing,
 * fault polarity, analog gain and bias are NOT hardware-validated yet.
 * Only compute DAC proposal codes; NEVER enable COMPs, DACs, or TIM1 here.
 */
#include "conv_protection.h"
#ifndef CONV_PROTECTION_HOST_TEST
#include "conv_controller.h"
#include "heater_control.h"
#include "main.h"
#include "tim.h"
#include "framework/beep.h"
#include "framework/pm_api.h"
#include "display/dispDriver.h"
#include <stdio.h>
#define PROT_RAMFUNC __attribute__((section(".ih_ramfunc"),used,noinline))
#else
#define PROT_RAMFUNC
#endif
static volatile uint32_t s_fast_faults;
static volatile uint8_t s_zero_status;
static volatile uint16_t s_zero_samples,s_ct_zero,s_ibus_zero;
static uint32_t s_ct_sum,s_ibus_sum;
static uint16_t s_settle,s_ct_min,s_ct_max,s_ib_min,s_ib_max;
static uint16_t s_dac1,s_dac2,s_low,s_high;
static uint8_t s_profile_ready,s_notified;
static uint32_t s_faults,s_fault_at;

void ConvProtection_Init(void)
{
    s_fast_faults=0u;
    s_zero_status=CONV_ZERO_WAIT;
    s_zero_samples=s_ct_zero=s_ibus_zero=0u;
    s_settle=0u;s_ct_sum=s_ibus_sum=0u;
    s_ct_min=s_ib_min=4095u;
    s_ct_max=s_ib_max=0u;
    s_dac1=s_dac2=s_low=s_high=0u;
    s_profile_ready=s_notified=0u;
    s_faults=s_fault_at=0u;
}
static uint16_t clamp12(int32_t x)
{
    if(x<0)return 0u;
    if(x>4095)return 4095u;
    return (uint16_t)x;
}
void ConvProtection_CalcDacWindow(uint16_t vbias_raw,uint16_t arr,
                                  uint16_t low_delta,uint16_t high_delta,
                                  uint16_t *dac1,uint16_t *dac2)
{
    /* The ARR dependency is explicitly PARAMETRIC: these deltas are
     * externally characterized DAC counts at TIM1 ARR=853. This is not a
     * derivation of the current threshold without a hardware transfer
     * function. Never write results to DAC without hardware validation. */
    uint32_t span=(uint32_t)arr+1u;
    uint32_t lo=(span*low_delta+427u)/854u;
    uint32_t hi=(span*high_delta+427u)/854u;
    if(dac1)*dac1=clamp12((int32_t)vbias_raw-(int32_t)lo);
    if(dac2)*dac2=clamp12((int32_t)vbias_raw+(int32_t)hi);
}
void ConvProtection_SetDacProfile(uint16_t lo,uint16_t hi,uint8_t calibrated)
{
    s_low=lo;s_high=hi;
    s_profile_ready=(uint8_t)(calibrated && lo && hi);
}
PROT_RAMFUNC void ConvProtection_FrameISR(const volatile uint16_t *frame)
{
    uint16_t ct,ibus;
    if(!frame)return;
    /* Raw watchdog equivalents run in the existing DMA IRQ; these detect
     * ADC rail/saturation, not accurately calibrated physical current. */
    if(frame[CONV_ADC_VBUS]>=CONV_PROT_VBUS_RAIL_RAW)
    {
        s_fast_faults|=CONV_PROT_VBUS_RAIL;
#ifndef CONV_PROTECTION_HOST_TEST
        ConvController_TripISR();
#endif
    }
    if(frame[CONV_ADC_IBUS]>=CONV_PROT_IBUS_RAIL_RAW)
    {
        s_fast_faults|=CONV_PROT_IBUS_RAIL;
#ifndef CONV_PROTECTION_HOST_TEST
        ConvController_TripISR();
#endif
    }
    if(s_zero_status==CONV_ZERO_READY ||
       s_zero_status==CONV_ZERO_FAILED)return;
#ifndef CONV_PROTECTION_HOST_TEST
    /* Hard interlock: a zero window is invalid if the bridge was ever
     * enabled during settling or averaging. This check is independent
     * of the MiaoUI RUN request and scheduled start state. */
    if((TIM1->BDTR & TIM_BDTR_MOE)!=0u ||
       (PWM_EN_GPIO_Port->ODR & PWM_EN_Pin)!=0u)
    {
        s_zero_status=CONV_ZERO_FAILED;
        s_fast_faults|=CONV_PROT_ZERO_SWITCHING;
        ConvController_TripISR();
        return;
    }
#endif
    if(s_settle<CONV_ZERO_SETTLE_FRAMES){++s_settle;return;}
    s_zero_status=CONV_ZERO_COLLECT;
    ct=frame[CONV_ADC_CT_RMS];
    ibus=frame[CONV_ADC_IBUS];
    if(ct<s_ct_min)s_ct_min=ct;
    if(ct>s_ct_max)s_ct_max=ct;
    if(ibus<s_ib_min)s_ib_min=ibus;
    if(ibus>s_ib_max)s_ib_max=ibus;
    s_ct_sum+=ct;s_ibus_sum+=ibus;
    if(++s_zero_samples<CONV_ZERO_AVG_FRAMES)return;
    if((uint16_t)(s_ct_max-s_ct_min)>CONV_ZERO_MAX_SPAN_RAW ||
       (uint16_t)(s_ib_max-s_ib_min)>CONV_ZERO_MAX_SPAN_RAW ||
       s_ct_max>=4090u)
    {
        s_zero_status=CONV_ZERO_FAILED;
        s_fast_faults|=CONV_PROT_ZERO_UNSTABLE;
        return;
    }
    s_ct_zero=(uint16_t)((s_ct_sum+CONV_ZERO_AVG_FRAMES/2u)/
                          CONV_ZERO_AVG_FRAMES);
    s_ibus_zero=(uint16_t)((s_ibus_sum+CONV_ZERO_AVG_FRAMES/2u)/
                            CONV_ZERO_AVG_FRAMES);
    s_zero_status=CONV_ZERO_READY;
}
uint8_t ConvProtection_ZeroReady(void)
{
    return s_zero_status==CONV_ZERO_READY;
}
uint32_t ConvProtection_FaultMask(void)
{
    return s_faults|s_fast_faults;
}
void ConvProtection_GetSnapshot(ConvProtectionSnapshot *out)
{
    if(!out)return;
    out->zero_status=(ConvZeroStatus)s_zero_status;
    out->ct_zero_raw=s_ct_zero;
    out->ibus_zero_raw=s_ibus_zero;
    out->zero_samples=s_zero_samples;
    out->dac1_code=s_dac1;out->dac2_code=s_dac2;
    out->dac_profile_ready=s_profile_ready;
    out->fault_mask=ConvProtection_FaultMask();
}
void ConvProtection_Service(uint32_t now,const ConvADC_Snapshot *adc,
                            const TempService_Snapshot *temp)
{
    uint32_t fresh=s_fast_faults;
    uint8_t heating=0u;
#ifndef CONV_PROTECTION_HOST_TEST
    HeaterSnapshot heater;
    Heater_GetSnapshot(&heater);
    heating=(uint8_t)(heater.state==HEATER_RUN_REQUESTED);
#endif
    if(adc && adc->frames)
    {
        if(s_profile_ready &&
           adc->filtered[CONV_ADC_VBIAS]>8u &&
           adc->filtered[CONV_ADC_VBIAS]<4080u)
        {
            uint16_t arr=853u;
#ifndef CONV_PROTECTION_HOST_TEST
            arr=(uint16_t)TIM1->ARR;
#endif
            ConvProtection_CalcDacWindow(adc->filtered[CONV_ADC_VBIAS],
                                          arr,s_low,s_high,&s_dac1,&s_dac2);
            /* Deliberately no HAL_DAC_Start/SetValue or HAL_COMP_Start. */
        }
        /* VBUS ~8V lower bound: ADC 2.5V ref and external x16 divider. */
        if(heating && adc->filtered[CONV_ADC_VBUS]<819u)
            fresh|=CONV_PROT_VBUS_LOW;
    }
    if(heating && temp)
    {
        if(!(temp->valid&TEMP_VALID_POT) || temp->pot_deci_c>=1300)
            fresh|=CONV_PROT_NTC_POT;
        if(!(temp->valid&TEMP_VALID_SYS) || temp->sys_deci_c>=850)
            fresh|=CONV_PROT_NTC_SYS;
        if((uint32_t)(now-temp->updated_ms)>500u)
            fresh|=CONV_PROT_ADC_STALE;
    }
    fresh &= ~s_faults;
    if(!fresh)return;
    s_faults|=fresh;
    s_fault_at=now;
#ifndef CONV_PROTECTION_HOST_TEST
    Heater_LatchFault(s_faults);
    pm_api_refresh_idle();
    if(!s_notified)
    {
        static const beep_note_t alarm[]={
            {BEEP_A5,100u},{BEEP_REST,80u},{BEEP_A5,150u},BEEP_END
        };
        (void)beep_popup(alarm);
        s_notified=1u;
    }
#endif
}
#ifndef CONV_PROTECTION_HOST_TEST
void ConvProtection_DrawFaultOverlay(void)
{
    char line[25];
    uint32_t now=HAL_GetTick();
    if(!s_faults || (uint32_t)(now-s_fault_at)>=4000u)return;
    Disp_ClearBuffer();
    Disp_SetFont(UI_FONT);
    Disp_DrawFrame(0u,0u,128u,64u);
    Disp_DrawStr(8u,15u,"HEATER FAULT");
    (void)snprintf(line,sizeof(line),"CODE %08lX",(unsigned long)s_faults);
    Disp_DrawStr(8u,33u,line);
    Disp_DrawStr(8u,51u,"OUTPUT DISABLED");
    Disp_SendBuffer();
}
#endif
