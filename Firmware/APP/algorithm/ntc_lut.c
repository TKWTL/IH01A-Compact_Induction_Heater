/* IH01A 10k B3950 5C LUT, ADC 12-bit VREF=2.5V, excitation=3.3V.
 * SYS 6.8k top / 20k parallel; POT 1.2k top / 3.6k parallel.
 * No float, log, exp or allocation. 39 * 2 * 2 = 156 bytes of table.
 * Temperature conversion MUST NOT run in the 20kHz ADC DMA interrupt.
 */
#include "algorithm/ntc_lut.h"
static const uint16_t s_sys[NTC_TABLE_SIZE]={
    3984,3962,3934,3897,3849,3787,3711,3617,3505,3374,
    3223,3055,2871,2676,2473,2266,2061,1861,1671,1492,
    1326,1175,1039,916,808,711,627,553,487,431,
    381,337,300,266,237,212,190,170,153
};
static const uint16_t s_pot[NTC_TABLE_SIZE]={
    4045,4041,4036,4029,4020,4008,3992,3973,3948,3918,
    3881,3836,3782,3719,3646,3561,3466,3359,3241,3113,
    2977,2832,2682,2528,2373,2217,2064,1915,1770,1632,
    1501,1378,1263,1156,1057,966,882,806,736
};
static uint8_t ntc_convert(uint16_t adc,const uint16_t *t,int16_t *out)
{
    uint8_t lo=0u,hi=NTC_TABLE_SIZE-1u,mid;
    uint16_t c0,c1,dc,dx;
    int32_t result;
    if(out==0)return 0u;
    if(adc>=t[0])
    {
        *out=NTC_T_MIN_DECI_C;
        return (adc==t[0])?1u:0u;
    }
    if(adc<=t[NTC_TABLE_SIZE-1u])
    {
        *out=NTC_T_MIN_DECI_C+(NTC_TABLE_SIZE-1u)*NTC_T_STEP_DECI_C;
        return (adc==t[NTC_TABLE_SIZE-1u])?1u:0u;
    }
    while((uint8_t)(hi-lo)>1u)
    {
        mid=(uint8_t)((lo+hi)>>1);
        if(adc>t[mid])hi=mid;
        else lo=mid;
    }
    c0=t[lo]; c1=t[hi];
    dc=(uint16_t)(c0-c1);
    dx=(uint16_t)(c0-adc);
    result=NTC_T_MIN_DECI_C+(int32_t)lo*NTC_T_STEP_DECI_C+
           ((uint32_t)dx*NTC_T_STEP_DECI_C+dc/2u)/dc;
    *out=(int16_t)result;
    return 1u;
}
uint8_t NTC_SYS_Convert(uint16_t adc,int16_t *out)
{
    return ntc_convert(adc,s_sys,out);
}
uint8_t NTC_POT_Convert(uint16_t adc,int16_t *out)
{
    return ntc_convert(adc,s_pot,out);
}
