#include <assert.h>
#include <stdio.h>
#include "conv_zcd.h"
#include "conv_phase.h"
int main(void)
{
    ConvZcdSnapshot v={0};
    /* ARR=639, D(20deg)=36: voltage +->- zero at 159.75+18 =177.75 */
    assert(ConvZcd_Calculate(639u,20u,178u,&v));
    assert(v.valid && v.capture_raw==178u && v.lag_deci_deg==1);
    assert(ConvZcd_Calculate(639u,20u,242u,&v));
    assert(v.lag_deci_deg>=360 && v.lag_deci_deg<=362);
    assert(ConvZcd_Calculate(639u,20u,80u,&v));
    assert(v.lag_deci_deg<0);
    /* Capture near CNT=0 precedes the voltage reference near 320 by
     * approximately half a cycle (negative lag). */
    assert(ConvZcd_Calculate(639u,180u,1u,&v));
    assert(v.lag_deci_deg<0);
    assert(!ConvZcd_Calculate(639u,0u,200u,&v));
    assert(!ConvZcd_Calculate(639u,20u,640u,&v));

    /* Pure count-domain fit: at 200kHz/ARR639, delta = +35.29 counts.
     * The 180deg empty-coil samples should map close to 89deg theoretical.
     * No current/phase gating is built into the math; report policy below. */
    {
        int32_t delay=0;
        static const struct { uint16_t khz,ic2; } cases[]={
            {180u,554u},{200u,515u},{220u,480u},{240u,450u},
            {260u,422u},{280u,399u},{300u,378u}
        };
        uint16_t arr;
        unsigned i;
        assert(ConvZcd_CorrectionMilliTicks(639u,&delay));
        assert(delay==35290);
        for(i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
        {
            assert(ConvPhase_ArrFromKHz(cases[i].khz,&arr));
            assert(ConvZcd_Calculate(arr,180u,cases[i].ic2,&v));
            assert(v.valid && v.correction_valid);
            assert(v.capture_raw==cases[i].ic2);
            assert(v.lag_corr_deci_deg>=860 && v.lag_corr_deci_deg<=920);
            assert(ConvZcd_CorrectionStatus(&v,700u,1u)==CONV_ZCD_CAL_FIT);
            assert(ConvZcd_CorrectionStatus(&v,49u,1u)==CONV_ZCD_CAL_LOW_CURRENT);
            assert(ConvZcd_CorrectionStatus(&v,700u,0u)==CONV_ZCD_CAL_ADC_INVALID);
        }
        assert(ConvZcd_Calculate(639u,90u,444u,&v));
        assert(ConvZcd_CorrectionStatus(&v,700u,1u)==CONV_ZCD_CAL_PHASE_RANGE);
        assert(ConvPhase_ArrFromKHz(150u,&arr));
        assert(!ConvZcd_CorrectionMilliTicks(arr,&delay));
        assert(ConvZcd_Calculate(arr,180u,600u,&v));
        assert(!v.correction_valid);
        assert(ConvZcd_CorrectionStatus(&v,700u,1u)==CONV_ZCD_CAL_FREQ_RANGE);
        assert(ConvPhase_ArrFromKHz(400u,&arr));
        assert(!ConvZcd_CorrectionMilliTicks(arr,&delay));
        assert(ConvZcd_Calculate(arr,180u,250u,&v));
        assert(ConvZcd_CorrectionStatus(&v,700u,1u)==CONV_ZCD_CAL_FREQ_RANGE);
        /* Early phase, invalid sample and missing output must never be FIT. */
        assert(ConvZcd_CorrectionStatus(0,700u,1u)==CONV_ZCD_CAL_NO_CAPTURE);
    }
    puts("zcd: PASS");
    return 0;
}
