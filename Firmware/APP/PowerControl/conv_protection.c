/* IH01A protection bootstrap. Legacy G474 Protection_Init2 was empty;
 * its HRTIM FLT1 and ADC watchdog code are not binary-compatible with G071.
 *
 * CubeMX config: window comparator pair, COMP1 synthesized output to TIM1
 * BRK (active low), DAC1 CH1/CH2 as low/high thresholds.
 * The analog window is initialized AFTER zero capture and with PWM disabled.
 * TIM1 output enable is never granted by this module; real break response
 * and analog gain must still be validated with an oscilloscope.
 */
#include "conv_protection.h"
#ifndef CONV_PROTECTION_HOST_TEST
#include "conv_controller.h"
#include "heater_control.h"
#include "main.h"
#include "tim.h"
#include "adc.h"
#include "dac.h"
#include "comp.h"
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
static volatile uint16_t s_ibus_trip_delta_raw;
static uint32_t s_ibus_limit_ma;
static uint16_t s_awd2_low,s_awd2_high;
static uint8_t s_comp_armed;
static uint8_t s_awd2_enabled;
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
    s_ibus_trip_delta_raw=0u;
    s_ibus_limit_ma=0u;
    s_awd2_low=0u;
    s_awd2_high=4095u;
    s_comp_armed=0u;
    s_awd2_enabled=0u;
    s_settle=0u;s_ct_sum=s_ibus_sum=0u;
    s_ct_min=s_ib_min=4095u;
    s_ct_max=s_ib_max=0u;
    s_dac1=s_dac2=0u;
    /* TEST-ONLY current window: ±12A at 0.1V/A => 1966 DAC counts.
     *
     * PROPOSED FUTURE PROTECTION: limit resonant-capacitor voltage to
     * |V_C,peak| <= 1000V by adapting the allowed sinusoidal CT_OCP
     * peak current to TIM1 period (ARR), not by holding 12A forever.
     * For i_C(t)=Ipk*sin(2*pi*f*t), with steady symmetric AC and C fixed:
     * Q_half = integral[0 .. T/2] i_C dt = 2*Ipk/(2*pi*f)
     * and delta_V_C(half-cycle)=Q_half/C = Ipk/(pi*f*C).
     * IMPORTANT: that delta is the PEAK-TO-PEAK voltage swing, so
     * |V_C,peak| = Ipk/(2*pi*f*C) (not delta_V_C itself).
     * Consequently the approximate bound is Ipk <= 2*pi*f*C*1000V,
     * f_TIM1 = 128MHz/(ARR+1) for PSC=0 (if PWM and current have this
     * fundamental frequency). For 37.6nF C, 300kHz permits ~70.9A
     * from this ideal model, 150kHz ~35.4A. Non-sinusoidal current,
     * frequency transitions, capacitor tolerances, voltage DC offset,
     * sensor delay and resonant overvoltage invalidate that simple
     * estimate; retain independent fast HW trip and margin.
     * This revision intentionally DOES NOT implement the dynamic rule. */
    s_low=s_high=1966u;
    s_profile_ready=1u;
    s_notified=0u;
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
    /* Physical CT_OCP amplifier is 0.1 V/A, independent of switching
     * frequency. ARR is retained only for source/API compatibility. */
    (void)arr;
    if(dac1)*dac1=clamp12((int32_t)vbias_raw-(int32_t)low_delta);
    if(dac2)*dac2=clamp12((int32_t)vbias_raw+(int32_t)high_delta);
}
void ConvProtection_SetDacProfile(uint16_t lo,uint16_t hi,uint8_t calibrated)
{
    s_low=lo;s_high=hi;
    s_profile_ready=(uint8_t)(calibrated && lo && hi);
}
/* Called from ADC1_COMP_IRQHandler before HAL_ADC_IRQHandler.
 * Latch the exact AWD source and make the interrupt one-shot; otherwise a
 * static overtemperature value would retrigger at the ADC scan rate. */
void ConvProtection_AnalogWatchdogISR(void)
{
#ifndef CONV_PROTECTION_HOST_TEST
    uint32_t pending=ADC1->ISR & ADC1->IER &
        (ADC_ISR_AWD1 | ADC_ISR_AWD2 | ADC_ISR_AWD3);
    if(!pending)return;
    if(pending&ADC_ISR_AWD1)s_fast_faults|=CONV_PROT_AWD_VBUS;
    if(pending&ADC_ISR_AWD2)s_fast_faults|=CONV_PROT_AWD_IBUS;
    if(pending&ADC_ISR_AWD3)s_fast_faults|=CONV_PROT_AWD_SYS;
    /* Disarm the asserted IRQ sources and clear their write-one flags;
     * the ADC scan and DMA remain active for diagnosis. */
    ADC1->IER &= ~pending;
    ADC1->ISR = pending;
    ConvController_TripISR();
#endif
}
PROT_RAMFUNC void ConvProtection_BreakISR(void)
{
#ifndef CONV_PROTECTION_HOST_TEST
    /* Do not clear BIF: it remains evidence of the first hardware trip.
     * Disable only the break interrupt to avoid a latched IRQ storm. */
    ConvController_TripISR();
    s_fast_faults|=CONV_PROT_TIM1_BREAK;
    TIM1->DIER &= ~TIM_DIER_BIE;
#endif
}
static void ibus_window_codes(uint16_t zero,uint32_t ma,
                              uint16_t *lower,uint16_t *upper)
{
    /* 0.2134 V/A; compute absolute current envelope around measured zero.
     * AWD2 checks only 8 MSBs: low rounds UP and high DOWN to avoid gaps
     * near nominal threshold; always retain 0..4095 legal bounds. */
    uint64_t raw=(uint64_t)ma*2134u*4095u+12500000u;
    uint32_t delta=(uint32_t)(raw/25000000u);
    uint32_t hi=(uint32_t)zero+delta;
    uint32_t lo=(delta>zero)?0u:(uint32_t)zero-delta;
    if(hi>4095u)hi=4095u;
    if(lower)*lower=(uint16_t)(((lo+15u)/16u)*16u);
    if(upper)*upper=(uint16_t)((hi/16u)*16u);
}
void ConvProtection_SetIbusLimitMa(uint32_t current_ma)
{
#ifndef CONV_PROTECTION_HOST_TEST
    uint16_t lower=0u,upper=4080u;
    uint8_t armed=(uint8_t)(ConvProtection_ZeroReady() && current_ma &&
                           !s_fast_faults && !s_faults);
    if(armed)ibus_window_codes(s_ibus_zero,current_ma,&lower,&upper);
    if(current_ma==s_ibus_limit_ma && lower==s_awd2_low &&
       upper==s_awd2_high && s_awd2_enabled==armed)return;
    s_awd2_enabled=armed;
    s_ibus_limit_ma=current_ma;
    s_awd2_low=lower;
    s_awd2_high=upper;
    ADC1->IER &= ~ADC_IER_AWD2IE;
    MODIFY_REG(ADC1->AWD2TR,ADC_AWD2TR_HT2|ADC_AWD2TR_LT2,
               ((uint32_t)upper<<ADC_AWD2TR_HT2_Pos) |
               ((uint32_t)lower<<ADC_AWD2TR_LT2_Pos));
    ADC1->ISR=ADC_ISR_AWD2;
    if(s_awd2_enabled)ADC1->IER |= ADC_IER_AWD2IE;
#else
    s_ibus_limit_ma=current_ma;
    s_awd2_low=0u;s_awd2_high=4080u;
    s_awd2_enabled=(uint8_t)(ConvProtection_ZeroReady() && current_ma &&
                            !s_fast_faults && !s_faults);
    if(s_awd2_enabled)
        ibus_window_codes(s_ibus_zero,current_ma,&s_awd2_low,&s_awd2_high);
#endif
}

PROT_RAMFUNC void ConvProtection_FrameISR(const volatile uint16_t *frame)
{
    uint16_t ct,ibus;
    if(!frame)return;
    /* Raw watchdog equivalents run in the existing DMA IRQ; these detect
     * ADC rail/saturation, not accurately calibrated physical current. */
#ifndef CONV_PROTECTION_HOST_TEST
    if(ConvController_BenchAdcFrameWatchdogISR())
        s_fast_faults|=CONV_PROT_BENCH_TIMEOUT;
#endif
    if(frame[CONV_ADC_VBUS]>=CONV_PROT_VBUS_RAIL_RAW)
    {
        s_fast_faults|=CONV_PROT_VBUS_RAIL;
#ifndef CONV_PROTECTION_HOST_TEST
        ConvController_TripISR();
#endif
    }
    /* Input undervoltage/overvoltage during *energized 12V bench*:
     * 12-bit reference = 40V bus full-scale after x16 divider.
     * ~10.25..13.67V hysteresis-free emergency guard independent of
     * the 100ms PD policy check. These are conservative raw limits. */
#ifndef CONV_PROTECTION_HOST_TEST
    if((PWM_EN_GPIO_Port->ODR&PWM_EN_Pin)!=0u &&
       (frame[CONV_ADC_VBUS]<1050u || frame[CONV_ADC_VBUS]>1400u))
    {
        s_fast_faults|=CONV_PROT_BENCH_VBUS;
        ConvController_TripISR();
    }
#endif
    if(frame[CONV_ADC_IBUS]>=CONV_PROT_IBUS_RAIL_RAW ||
       (s_zero_status==CONV_ZERO_READY && s_ibus_zero>=32u &&
        frame[CONV_ADC_IBUS]<=5u))
    {
        s_fast_faults|=CONV_PROT_IBUS_RAIL;
#ifndef CONV_PROTECTION_HOST_TEST
        ConvController_TripISR();
#endif
    }
    if(s_zero_status==CONV_ZERO_READY)
    {
        uint16_t current=frame[CONV_ADC_IBUS];
        uint16_t delta=(current>s_ibus_zero) ?
                       (uint16_t)(current-s_ibus_zero) :
                       (uint16_t)(s_ibus_zero-current);
        if(s_ibus_trip_delta_raw && delta>s_ibus_trip_delta_raw)
        {
            s_fast_faults|=CONV_PROT_IBUS_LIMIT;
#ifndef CONV_PROTECTION_HOST_TEST
            ConvController_TripISR();
#endif
        }
        return;
    }
    if(s_zero_status==CONV_ZERO_FAILED)return;
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
void ConvProtection_SetIbusTripDeltaRaw(uint16_t delta_raw)
{
    s_ibus_trip_delta_raw=delta_raw;
}
uint8_t ConvProtection_ZeroReady(void)
{
    return s_zero_status==CONV_ZERO_READY;
}
uint32_t ConvProtection_FaultMask(void)
{
    return s_faults|s_fast_faults;
}
uint8_t ConvProtection_WithinWindow(uint8_t above_low,uint8_t above_high)
{
    /* At positive comparator inputs shared from CT_OCP:
     * COMP1 compares signal > lower DAC, COMP2 compares signal > upper DAC.
     * Window synthesized on COMP1_OUT is (COMP1 xor COMP2).
     * HAL_COMP_GetOutputLevel() reads a comparator's individual CSR.VALUE
     * and therefore MUST NOT be used alone as the window output level. */
    return (uint8_t)((above_low!=0u) && (above_high==0u));
}
uint8_t ConvProtection_CompArmed(void)
{
#ifndef CONV_PROTECTION_HOST_TEST
    return (uint8_t)(s_comp_armed &&
                     (COMP1->CSR&COMP_CSR_EN)!=0u &&
                     (COMP2->CSR&COMP_CSR_EN)!=0u &&
                     ConvProtection_FaultMask()==0u);
#else
    return 0u;
#endif
}
uint8_t ConvProtection_WindowSafe(void)
{
#ifndef CONV_PROTECTION_HOST_TEST
    if(!s_comp_armed || !ConvProtection_ZeroReady() ||
       ConvProtection_FaultMask()!=0u)return 0u;
    if(!(COMP1->CSR&COMP_CSR_EN) || !(COMP2->CSR&COMP_CSR_EN))
        return 0u;
    /* PA6 is configured as AF7 COMP1_OUT; IDR reads the physical
     * synthesized window output (HIGH=inside window). Confirm independently
     * against the two raw comparator values and TIM1 Break latch. */
    if(!ConvProtection_WithinWindow((COMP1->CSR&COMP_CSR_VALUE)!=0u,
                                     (COMP2->CSR&COMP_CSR_VALUE)!=0u))
        return 0u;
    if((GPIOA->IDR&LL_GPIO_PIN_6)==0u || (TIM1->SR&TIM_SR_BIF)!=0u)
        return 0u;
    return 1u;
#else
    return 0u; /* host has no COMP/GPIO hardware */
#endif
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
    out->comp_armed=s_comp_armed;
    out->awd2_enabled=s_awd2_enabled;
    out->awd2_low_code=s_awd2_low;
    out->awd2_high_code=s_awd2_high;
    out->ibus_limit_ma=s_ibus_limit_ma;
    out->fault_mask=ConvProtection_FaultMask();
}
#ifndef CONV_PROTECTION_HOST_TEST
static uint8_t Protection_ApplyCompWindow(uint16_t lower,uint16_t upper)
{
    if(!s_comp_armed)
    {
        /* No switching while analog references and window mode initialize. */
        ConvController_TripISR();
        if(HAL_DAC_SetValue(&hdac1,DAC_CHANNEL_1,DAC_ALIGN_12B_R,lower)!=HAL_OK ||
           HAL_DAC_SetValue(&hdac1,DAC_CHANNEL_2,DAC_ALIGN_12B_R,upper)!=HAL_OK ||
           HAL_DAC_Start(&hdac1,DAC_CHANNEL_1)!=HAL_OK ||
           HAL_DAC_Start(&hdac1,DAC_CHANNEL_2)!=HAL_OK)
            return 0u;

        /* MX_COMP2_Init executes after MX_COMP1_Init and the HAL can clear
         * the shared WINMODE bits. Reapply the requested COMP1 window
         * configuration AFTER both handles are initialized. */
        if(HAL_COMP_Init(&hcomp1)!=HAL_OK)return 0u;
        if(HAL_COMP_Start(&hcomp1)!=HAL_OK ||
           HAL_COMP_Start(&hcomp2)!=HAL_OK)return 0u;
        /* COMP1 window output high=within thresholds, low=outside;
         * TIM1 Break polarity LOW. This must be oscilloscope-validated
         * before allowing a nonzero heater power target. */
        s_comp_armed=1u;
        return 1u;
    }
    if(lower!=s_dac1 || upper!=s_dac2)
    {
        if(HAL_DAC_SetValue(&hdac1,DAC_CHANNEL_1,DAC_ALIGN_12B_R,lower)!=HAL_OK ||
           HAL_DAC_SetValue(&hdac1,DAC_CHANNEL_2,DAC_ALIGN_12B_R,upper)!=HAL_OK)
            return 0u;
    }
    return 1u;
}
#endif

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
        /* For ±12A at 0.1V/A, both DAC codes must be inside 0..4095.
         * An out-of-range VBIAS would invalidate the two-sided trip window. */
        if(s_profile_ready && ConvProtection_ZeroReady() &&
           !ConvProtection_FaultMask())
        {
            uint16_t bias=adc->filtered[CONV_ADC_VBIAS];
            uint16_t lower,upper;
            if(bias<s_low || (uint32_t)bias+s_high>4095u)
                fresh|=CONV_PROT_DAC_RANGE;
            else
            {
                ConvProtection_CalcDacWindow(bias,0u,s_low,s_high,
                                              &lower,&upper);
#ifndef CONV_PROTECTION_HOST_TEST
                if(!Protection_ApplyCompWindow(lower,upper))
                    fresh|=CONV_PROT_COMP_SETUP;
                else
#endif
                {
                    s_dac1=lower;
                    s_dac2=upper;
                }
            }
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
    ConvController_TripISR(); /* foreground faults also gate PWM immediately */
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
