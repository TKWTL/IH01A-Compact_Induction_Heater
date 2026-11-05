#include <assert.h>
#include <stdio.h>
#include "PowerControl/conv_current.h"
int main(void)
{
    const uint16_t zero=1100u;
    assert(ConvCurrent_CtRmsMilliAmp(zero,zero)==0u);
    assert(ConvCurrent_CtRmsMilliAmp(zero-4u,zero)==0u);
    /* 0.1893975V/A -> about 310 ADC counts/A (2.5V reference).
     * 310 counts rounds to 0.999A; 311 counts to 1.002A. */
    assert(ConvCurrent_CtRmsMilliAmp(zero+310u,zero)==999u);
    assert(ConvCurrent_CtRmsMilliAmp(zero+311u,zero)==1002u);
    assert(ConvCurrent_CtRmsMilliAmp(4095u,0u)==13200u);
    puts("CT_RMS current scale: PASS");
    return 0;
}
