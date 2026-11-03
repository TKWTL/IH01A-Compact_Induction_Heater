#include <assert.h>
#include <stdio.h>
#include "PowerControl/conv_current.h"
int main(void)
{
    const uint16_t zero=1100u;
    assert(ConvCurrent_CtRmsMilliAmp(zero,zero)==0u);
    assert(ConvCurrent_CtRmsMilliAmp(zero-4u,zero)==0u);
    /* 1 A corresponds to ~413.65 ADC counts with 2.5V reference. */
    assert(ConvCurrent_CtRmsMilliAmp(zero+414u,zero)>=1000u);
    assert(ConvCurrent_CtRmsMilliAmp(zero+414u,zero)<=1002u);
    assert(ConvCurrent_CtRmsMilliAmp(4095u,0u)>=9890u);
    assert(ConvCurrent_CtRmsMilliAmp(4095u,0u)<=9910u);
    puts("CT_RMS current scale: PASS");
    return 0;
}
