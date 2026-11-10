#include <assert.h>
#include <stdio.h>
#include "PowerControl/pan_probe_config.h"
#include "PowerControl/conv_phase.h"

int main(void)
{
    uint16_t arr=0u;
    ConvPhaseConfig phase;
    assert(PAN_FREQ_KHZ==160u);
    assert(PAN_PHASE==180u);
    assert(PAN_CYCLES==2u);
    assert(ConvPhase_ArrFromKHz(PAN_FREQ_KHZ,&arr));
    /* TIM1 = 128 MHz / (799+1) => exactly 160 kHz. */
    assert(arr==799u);
    assert(ConvPhase_KHzFromArr(arr)==160u);
    assert(ConvPhase_Calculate(arr,PAN_PHASE,&phase));
    assert(phase.arr==799u && phase.angle_deg==180u);
    /* Nominal high time = 2 / 160 kHz = 12500 ns (actual
     * time includes ISR latency and may be cut short by Break). */
    assert((PAN_CYCLES*1000000u)/PAN_FREQ_KHZ==12500u);
    puts("pan probe 160kHz two-period profile: PASS");
    return 0;
}
