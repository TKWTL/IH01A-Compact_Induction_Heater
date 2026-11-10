#include <assert.h>
#include <stdio.h>
#include "conv_protection.h"
int main(void)
{
    uint16_t low,high;
    volatile uint16_t f[CONV_ADC_COUNT]={0};
    ConvProtectionSnapshot s;
    ConvADC_Snapshot adc={0};
    TempService_Snapshot tmp={0};
    ConvProtection_Init();
    /* Operator may re-acknowledge transient trips, but may never
     * bypass failed startup zero calibration or comparator setup. */
    assert(ConvProtection_RearmableMask(CONV_PROT_AWD_IBUS |
                                       CONV_PROT_TIM1_BREAK));
    assert(ConvProtection_RearmableMask(CONV_PROT_NTC_SYS |
                                       CONV_PROT_IBUS_LIMIT));
    /* A failed zero is rearmable only after an explicit new calibration
     * with both PC6 and MOE held low, never by merely clearing flags. */
    assert(ConvProtection_RearmableMask(CONV_PROT_ZERO_UNSTABLE));
    assert(ConvProtection_RearmableMask(CONV_PROT_ZERO_SWITCHING));
    assert(!ConvProtection_RearmableMask(CONV_PROT_COMP_SETUP));
    assert(!ConvProtection_RearmableMask(CONV_PROT_AUTO_FREQ));
    assert(!ConvProtection_ManualRearm(0u)); /* no fake host hardware */
    /* Stale 12V-only rearm window must not reject 20/28V valid PD levels.
     * VBUS pin ADC = bus / 16 / 2.5V * 4095. */
    assert(ConvProtection_VbusSamplesSafe(1229u,1229u)); /* ~12V */
    assert(ConvProtection_VbusSamplesSafe(2048u,2048u)); /* ~20V */
    assert(ConvProtection_VbusSamplesSafe(2867u,2867u)); /* ~28V */
    assert(!ConvProtection_VbusSamplesSafe(818u,818u)); /* <8V */
    assert(!ConvProtection_VbusSamplesSafe(4070u,4070u)); /* rail */
    assert(!ConvProtection_VbusSamplesSafe(2867u,4070u)); /* filter rail */
    assert(!ConvProtection_ZeroReady());
    /* HAL raw CSR.VALUE for COMP1 alone cannot establish a safe window. */
    assert(ConvProtection_WithinWindow(1u,0u));
    assert(!ConvProtection_WithinWindow(0u,0u));
    assert(!ConvProtection_WithinWindow(1u,1u));
    assert(!ConvProtection_WithinWindow(0u,1u));
    assert(!ConvProtection_WindowSafe());
    f[CONV_ADC_CT_RMS]=1500u;
    f[CONV_ADC_IBUS]=1900u;
    f[CONV_ADC_VBUS]=2500u;
    for(unsigned i=0;i<CONV_ZERO_SETTLE_FRAMES+CONV_ZERO_AVG_FRAMES;i++)
        ConvProtection_FrameISR(f);
    ConvProtection_GetSnapshot(&s);
    assert(s.zero_status==CONV_ZERO_READY);
    assert(s.ct_zero_raw==1500u && s.ibus_zero_raw==1900u);
    assert(s.zero_samples==CONV_ZERO_AVG_FRAMES);
    ConvProtection_SetIbusLimitMa(5000u);
    ConvProtection_GetSnapshot(&s);
    assert(s.awd2_enabled && s.ibus_limit_ma==5000u);
    assert(s.awd2_low_code==160u && s.awd2_high_code==3648u);
    assert(!s.awd2_trip_valid);
    assert(s.ibus_run_last_raw==1900u &&
           s.ibus_run_min_raw==1900u && s.ibus_run_max_raw==1900u);
    f[CONV_ADC_IBUS]=2500u;
    ConvProtection_FrameISR(f);
    f[CONV_ADC_IBUS]=1820u;
    ConvProtection_FrameISR(f);
    ConvProtection_GetSnapshot(&s);
    assert(s.ibus_run_last_raw==1820u);
    assert(s.ibus_run_min_raw==1820u);
    assert(s.ibus_run_max_raw==2500u);
    f[CONV_ADC_IBUS]=1900u;
    ConvProtection_SetIbusLimitMa(0u);
    ConvProtection_GetSnapshot(&s);
    assert(!s.awd2_enabled);
    ConvProtection_CalcDacWindow(2000u,853u,100u,120u,&low,&high);
    assert(low==1900u && high==2120u);
    ConvProtection_CalcDacWindow(2048u,426u,1966u,1966u,&low,&high);
    assert(low==82u && high==4014u); /* fixed ±16A */
    ConvProtection_CalcDacWindow(2048u,853u,1966u,1966u,&low,&high);
    assert(low==82u && high==4014u); /* independent of frequency */
    adc.frames=1u;
    adc.filtered[CONV_ADC_VBIAS]=2048u;
    ConvProtection_Service(900u,&adc,&tmp);
    ConvProtection_GetSnapshot(&s);
    assert(s.dac_profile_ready && s.dac1_code==82u &&
           s.dac2_code==4014u); /* actual default profile, ±16A */
    ConvProtection_SetDacProfile(100u,120u,1u);
    adc.frames=1u;
    adc.filtered[CONV_ADC_VBIAS]=2000u;
    ConvProtection_Service(1000u,&adc,&tmp);
    ConvProtection_GetSnapshot(&s);
    assert(s.dac_profile_ready && s.dac1_code==1900u);
    ConvProtection_SetIbusTripDeltaRaw(150u);
    f[CONV_ADC_IBUS]=2100u; /* 200 counts above calibrated 1900 */
    ConvProtection_FrameISR(f);
    assert(ConvProtection_FaultMask()&CONV_PROT_IBUS_LIMIT);
    f[CONV_ADC_IBUS]=1900u;
    f[CONV_ADC_VBUS]=4095u;
    ConvProtection_FrameISR(f);
    ConvProtection_Service(1100u,&adc,&tmp);
    assert(ConvProtection_FaultMask()&CONV_PROT_VBUS_RAIL);
    ConvProtection_Init();
    f[CONV_ADC_VBUS]=2000u;f[CONV_ADC_CT_RMS]=1500u;
    for(unsigned i=0;i<CONV_ZERO_SETTLE_FRAMES;i++)
        ConvProtection_FrameISR(f);
    for(unsigned i=0;i<CONV_ZERO_AVG_FRAMES;i++)
    {
        f[CONV_ADC_CT_RMS]=(uint16_t)((i&1u)?1400u:1700u);
        ConvProtection_FrameISR(f);
    }
    ConvProtection_GetSnapshot(&s);
    assert(s.zero_status==CONV_ZERO_FAILED);
    assert(s.fault_mask&CONV_PROT_ZERO_UNSTABLE);
    assert(ConvProtection_RequestZeroRecalibration());
    assert(!ConvProtection_RequestZeroRecalibration());
    ConvProtection_GetSnapshot(&s);
    assert(s.zero_status==CONV_ZERO_WAIT && s.zero_samples==0u);
    assert(s.fault_mask&CONV_PROT_ZERO_UNSTABLE);
    f[CONV_ADC_CT_RMS]=1500u;
    f[CONV_ADC_IBUS]=1900u;
    for(unsigned i=0;i<CONV_ZERO_SETTLE_FRAMES+CONV_ZERO_AVG_FRAMES;i++)
        ConvProtection_FrameISR(f);
    ConvProtection_GetSnapshot(&s);
    assert(s.zero_status==CONV_ZERO_READY);
    assert(s.ct_zero_raw==1500u && s.ibus_zero_raw==1900u);
    assert(s.fault_mask&CONV_PROT_ZERO_UNSTABLE);
    assert(!ConvProtection_RequestZeroRecalibration());
    puts("conv protection/zero: PASS");
    return 0;
}
