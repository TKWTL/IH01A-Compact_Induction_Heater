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
     * ARR=426 gives f=128MHz/427=299.77kHz and half=213 counts. */
    d=((uint32_t)angle_deg*arr+180u)/360u;
    if(d>half)d=half;
    out->arr=arr;
    out->ccr1=half;
    out->ccr3=(uint16_t)d;
    out->ccr4=(uint16_t)(d+half);
    out->angle_deg=angle_deg;
    return 1u;
}
