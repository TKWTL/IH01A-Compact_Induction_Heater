#include <assert.h>
#include <stdio.h>
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
    unsigned i;
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
    assert(out.requested_arr==427u); /* +30deg => lower f */
    in.arr=550u;
    in.lag_corr_deci_deg=200;
    tick();assert(out.requested_arr==550u); /* +/-2deg deadband */
    in.lag_corr_deci_deg=170;
    tick();assert(out.requested_arr==549u); /* +17deg => higher f */
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
    puts("auto frequency core: PASS");
    return 0;
}
