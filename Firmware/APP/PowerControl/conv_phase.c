#include "conv_phase.h"
uint8_t ConvPhase_Calculate(uint16_t arr,uint16_t angle_deg,
                             ConvPhaseConfig *out)
{
    uint32_t d;
    uint16_t half;
    if(!out || arr<2u)return 0u;
    if(angle_deg>180u)angle_deg=180u;
    half=(uint16_t)(arr/2u);
    /* D=round(angle * ARR / 360); at 180deg clamp to floor(ARR/2).
     * ARR=639 gives f=128MHz/640=200kHz and half=319 counts. */
    d=((uint32_t)angle_deg*arr+180u)/360u;
    if(d>half)d=half;
    out->arr=arr;
    out->ccr1=half;
    out->ccr3=(uint16_t)d;
    out->ccr4=(uint16_t)(d+half);
    out->angle_deg=angle_deg;
    return 1u;
}

/* Timer period includes the zero count (ARR+1). Choose nearest integer
 * ARR; CCR1/3/4 must always be recalculated against the new period. */
uint8_t ConvPhase_ArrFromKHz(uint16_t khz,uint16_t *arr)
{
    uint32_t period;
    if(!arr || khz<CONV_FREQ_MIN_KHZ || khz>CONV_FREQ_MAX_KHZ)return 0u;
    period=(CONV_TIM1_CLOCK_KHZ+(uint32_t)khz/2u)/khz;
    if(period<4u || period>65536u)return 0u;
    *arr=(uint16_t)(period-1u);
    return 1u;
}
uint16_t ConvPhase_KHzFromArr(uint16_t arr)
{
    uint32_t period=(uint32_t)arr+1u;
    return (uint16_t)((CONV_TIM1_CLOCK_KHZ+period/2u)/period);
}
