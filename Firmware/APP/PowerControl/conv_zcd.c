#include "conv_zcd.h"
#include "conv_phase.h"
#ifndef CONV_ZCD_HOST_TEST
#include "conv_controller.h"
#include "main.h"
#include "tim.h"
#endif

static ConvZcdSnapshot s_result;
static uint8_t s_capture_started;
/* Empirical CT_ZERO delay fit from "分析CT整流电路" (2026-11-04).
 * f in kHz, bias in TIM1 counts:
 * 58 + 0.105(f-300) -0.407*max(0,230-f) -2.057*max(0,175-f).
 * Here all calculations are milli-counts and use actual TIM1 ARR frequency.
 * The 58-count anchor is NOT the TP1941 datasheet propagation time.
 * Fit only covers 160..330kHz empty-coil conditions, not 330..400kHz.
 * Comparators are unreliable at weak CT current; no PLL is enabled here.
 */
uint8_t ConvZcd_CorrectionMilliTicks(uint16_t arr,int32_t *out)
{
    uint32_t period=(uint32_t)arr+1u;
    int32_t hz,delta;
    if(!out || arr<2u)return 0u;
    hz=(int32_t)((CONV_TIM1_CLOCK_KHZ*1000u+period/2u)/period);
    if(hz<160000 || hz>330000)return 0u;
    delta=58000+105*(hz-300000)/1000;
    if(hz<230000)delta-=407*(230000-hz)/1000;
    if(hz<175000)delta-=2057*(175000-hz)/1000;
    *out=delta;
    return 1u;
}
ConvZcdCalStatus ConvZcd_CorrectionStatus(const ConvZcdSnapshot *sample,
                                           uint32_t ct_ma,uint8_t ct_valid)
{
    if(!sample || !sample->valid)return CONV_ZCD_CAL_NO_CAPTURE;
    if(!sample->correction_valid)return CONV_ZCD_CAL_FREQ_RANGE;
    if(sample->phase_deg<120u || sample->phase_deg>180u)
        return CONV_ZCD_CAL_PHASE_RANGE;
    if(!ct_valid)return CONV_ZCD_CAL_ADC_INVALID;
    if(ct_ma<50u)return CONV_ZCD_CAL_LOW_CURRENT;
    return CONV_ZCD_CAL_FIT;
}

uint8_t ConvZcd_Calculate(uint16_t arr,uint16_t phase_deg,
                           uint16_t ccr2,ConvZcdSnapshot *out)
{
    ConvPhaseConfig p;
    int32_t delta4,period;
    int32_t rounded;
    if(!out || !ConvPhase_Calculate(arr,phase_deg,&p) ||
       ccr2>arr || p.ccr3==0u)return 0u;
    /* In quarter timer counts:
     * 4*t_voltage = ARR + 2*D, as requested by the analog polarity model.
     * 4*t_current = 4*CCR2, with both using TIM1 CNT=0 as origin.
     * Wrap to the signed nearest electrical cycle: [-180,+180] degrees.
     * Treat CH2 rising edges as the inverted path's +->- current crossing.
     */
    period=4*(int32_t)(arr+1u);
    delta4=4*(int32_t)ccr2-((int32_t)arr+2*(int32_t)p.ccr3);
    if(delta4>period/2)delta4-=period;
    else if(delta4<-(period/2))delta4+=period;
    /* (quarter ticks)*(3600/4)/(period ticks) -> deci-degrees. */
    rounded=delta4*3600;
    if(rounded>=0)rounded+=period/2;
    else rounded-=period/2;
    out->capture_raw=ccr2;
    out->phase_deg=p.angle_deg;
    out->lag_deci_deg=(int16_t)(rounded/period);
    out->lag_quarter_ticks=delta4;
    out->correction_valid=0u;
    out->delay_milliticks=0;
    out->lag_corr_deci_deg=0;
    if(ConvZcd_CorrectionMilliTicks(arr,&out->delay_milliticks))
    {
        /* Compensation is applied in count-domain before converting to
         * tenths of degrees. Both negative and positive biases are valid.
         * Round signed division symmetrically and wrap to [-180,+180]. */
        int32_t divisor=1000*(int32_t)(arr+1u);
        int32_t numerator=out->delay_milliticks*3600;
        int32_t correction=(numerator>=0) ?
                            (numerator+divisor/2)/divisor :
                            (numerator-divisor/2)/divisor;
        int32_t compensated=(int32_t)out->lag_deci_deg-correction;
        if(compensated>1800)compensated-=3600;
        else if(compensated<-1800)compensated+=3600;
        out->lag_corr_deci_deg=(int16_t)compensated;
        out->correction_valid=1u;
    }
    out->valid=1u;
    return 1u;
}
void ConvZcd_Init(void)
{
    s_result.valid=0u;
    s_result.correction_valid=0u;
    s_result.delay_milliticks=0;
    s_result.lag_corr_deci_deg=0;
    s_result.sequence=0u;
    s_result.overcapture_seen=0u;
    s_result.last_ms=0u;
    s_capture_started=0u;
}
void ConvZcd_StartCapture(void)
{
#ifndef CONV_ZCD_HOST_TEST
    if(s_capture_started)return;
    /* TIM1 is configured for CC2 rising-edge direct TI2, PA9 AF2.
     * Neither DMA CC2DE nor CC2IE is used for this diagnostic phase. */
    TIM1->DIER &= ~(TIM_DIER_CC2DE|TIM_DIER_CC2IE);
    TIM1->SR=(uint32_t)~(TIM_SR_CC2IF|TIM_SR_CC2OF);
    TIM1->CCER |= TIM_CCER_CC2E;
#endif
    s_capture_started=1u;
}
void ConvZcd_Invalidate(void)
{
    s_result.valid=0u;
    s_result.correction_valid=0u;
    s_result.last_ms=0u;
}
void ConvZcd_Poll(uint32_t now_ms)
{
#ifndef CONV_ZCD_HOST_TEST
    uint32_t flags;
    uint16_t captured;
    ConvZcdSnapshot next;
    /* With PC6 LOW there is no powered resonant current; a TP1941
     * comparator may nevertheless oscillate or pick up noise. Never
     * present that captured edge as a valid current phase measurement. */
    if(!s_capture_started || !ConvController_LogicPwmRunning() ||
       !ConvController_OutputEnabled())
    {
        s_result.valid=0u;
        return;
    }
    flags=TIM1->SR;
    if((flags&(TIM_SR_CC2IF|TIM_SR_CC2OF))==0u)
    {
        if((uint32_t)(now_ms-s_result.last_ms)>250u)s_result.valid=0u;
        return;
    }
    /* Reading CCR2 returns the MOST RECENT latched edge and normally
     * clears CC2IF. At 200kHz the CC2OF flag is inevitable during 20ms
     * polling and does NOT by itself mean that CCR2 is incorrect. */
    captured=(uint16_t)TIM1->CCR2;
    if((flags&TIM_SR_CC2OF)!=0u)
    {
        s_result.overcapture_seen=1u;
        TIM1->SR=(uint32_t)~TIM_SR_CC2OF;
    }
    if(ConvZcd_Calculate((uint16_t)TIM1->ARR,
                          ConvController_GetPhaseDegrees(),
                          captured,&next))
    {
        s_result.capture_raw=next.capture_raw;
        s_result.phase_deg=next.phase_deg;
        s_result.lag_deci_deg=next.lag_deci_deg;
        s_result.lag_corr_deci_deg=next.lag_corr_deci_deg;
        s_result.delay_milliticks=next.delay_milliticks;
        s_result.correction_valid=next.correction_valid;
        s_result.lag_quarter_ticks=next.lag_quarter_ticks;
        s_result.last_ms=now_ms;
        ++s_result.sequence;
        s_result.valid=1u;
    }
    else s_result.valid=0u;
#else
    (void)now_ms;
#endif
}
void ConvZcd_GetSnapshot(ConvZcdSnapshot *out)
{
    if(out)*out=s_result;
}
