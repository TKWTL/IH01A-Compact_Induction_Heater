#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "conv_zcd.h"
#include "conv_phase.h"
#include "conv_auto_freq.h"

static ConvAutoFreqCore loop;
static ConvAutoFreqInput in;
static ConvAutoFreqDecision out;
static void tick(void)
{
    ConvAutoFreq_CoreStep(&loop,&in,&out);
}
int main(void)
{
    unsigned i,arr,degree,ic;
    ConvAutoFreq_CoreReset(&loop);
    in.enabled=0u;in.output_on=0u;in.hardware_ok=1u;
    in.arr=639u;in.ibus_delta_raw=0u;
    in.phase_fit=0u;in.lag_corr_deci_deg=0;
    tick();
    assert(out.state==CONV_AF_OFF && out.requested_arr==639u);
    in.enabled=1u;
    tick();
    assert(out.state==CONV_AF_SAFE && out.requested_arr==426u);
    in.output_on=1u;
    tick();
    assert(out.state==CONV_AF_ACQUIRE && out.requested_arr==426u);
    in.arr=426u;in.ibus_delta_raw=174u;
    tick();assert(out.state==CONV_AF_ACQUIRE && !loop.ibus_locked);
    in.ibus_delta_raw=175u;in.phase_fit=1u;in.lag_corr_deci_deg=300;
    tick();tick();tick();
    assert(loop.ibus_locked && out.state==CONV_AF_TRACK);
    assert(out.requested_arr==430u); /* +30deg => +4ARR (slow down) */
    in.arr=550u;
    in.lag_corr_deci_deg=200;
    tick();assert(out.requested_arr==550u); /* +/-2deg deadband */
    in.lag_corr_deci_deg=170;
    tick();assert(out.requested_arr==548u); /* +17deg => -2ARR (faster) */
    in.arr=CONV_AF_START_ARR;
    tick();assert(out.requested_arr==CONV_AF_START_ARR); /* clamp */
    in.arr=CONV_AF_MIN_ARR;in.lag_corr_deci_deg=300;
    tick();assert(out.requested_arr==CONV_AF_MIN_ARR);
    in.ibus_delta_raw=141u;in.arr=620u;
    tick();assert(loop.ibus_locked);
    in.ibus_delta_raw=140u;
    tick();assert(!loop.ibus_locked && out.requested_arr==426u);
    /* Feedback lost at high input current: high-frequency recovery, then
     * hard stop after 100ms without a fresh corrected ZCD sample. */
    in.ibus_delta_raw=200u;in.phase_fit=0u;
    for(i=0;i<19u;i++)
    {
        tick();
        assert(out.state==CONV_AF_RECOVER && !out.trip);
        assert(out.requested_arr==426u);
    }
    tick();assert(out.state==CONV_AF_FAULT && out.trip);
    /* A real negative current lag cannot simply be "fixed" by stepping
     * frequency while continuing to energize the bridge. */
    ConvAutoFreq_CoreReset(&loop);
    in.ibus_delta_raw=200u;in.phase_fit=1u;
    in.lag_corr_deci_deg=-10;
    tick();assert(out.state==CONV_AF_FAULT && out.trip);
    /* Phase <120 / missing protection data fails closed while powered. */
    ConvAutoFreq_CoreReset(&loop);
    in.hardware_ok=0u;
    tick();assert(out.state==CONV_AF_FAULT && out.trip);
    assert(ConvAutoFreq_SlewArr(752u,426u,1u)==744u);
    assert(ConvAutoFreq_SlewArr(426u,752u,1u)==430u);
    assert(ConvAutoFreq_SlewArr(752u,426u,0u)==426u);
    /* Q12 LUT against the original milli-tick corrected phase math:
     * <=0.3 electrical degrees throughout the guarded 170..300kHz band.
     * Check wrap boundaries modulo a full 360deg as well. */
    for(arr=CONV_AF_START_ARR;arr<=CONV_AF_MIN_ARR;arr++)
    {
        for(degree=120u;degree<=180u;degree+=30u)
        {
            ConvPhaseConfig pc;
            assert(ConvPhase_Calculate((uint16_t)arr,(uint16_t)degree,&pc));
            for(ic=0u;ic<=arr;ic+=29u)
            {
                ConvZcdSnapshot z;
                int16_t fast;
                int diff;
                assert(ConvZcd_Calculate((uint16_t)arr,
                        (uint16_t)degree,(uint16_t)ic,&z));
                assert(z.correction_valid);
                assert(ConvAutoFreq_CorrectLagFast((uint16_t)arr,pc.ccr3,
                                                   (uint16_t)ic,&fast));
                diff=abs((int)fast-(int)z.lag_corr_deci_deg);
                if(diff>1800)diff=3600-diff;
                assert(diff<=3);
            }
        }
    }
    assert(!ConvAutoFreq_CorrectLagFast(425u,200u,300u,0));
    puts("auto frequency core and quantized phase: PASS");
    return 0;
}
