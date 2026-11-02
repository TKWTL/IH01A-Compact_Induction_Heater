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
    ConvProtection_SetIbusLimitMa(0u);
    ConvProtection_GetSnapshot(&s);
    assert(!s.awd2_enabled);
    ConvProtection_SetIbusTripDeltaRaw(150u);
    f[CONV_ADC_IBUS]=2100u; /* 200 counts above calibrated 1900 */
    ConvProtection_FrameISR(f);
    assert(ConvProtection_FaultMask()&CONV_PROT_IBUS_LIMIT);
    f[CONV_ADC_IBUS]=1900u;
    ConvProtection_CalcDacWindow(2000u,853u,100u,120u,&low,&high);
    assert(low==1900u && high==2120u);
    ConvProtection_CalcDacWindow(2048u,426u,1966u,1966u,&low,&high);
    assert(low==82u && high==4014u); /* 300kHz still ±12A */
    ConvProtection_CalcDacWindow(2048u,853u,1966u,1966u,&low,&high);
    assert(low==82u && high==4014u); /* independent of ARR */
    ConvProtection_SetDacProfile(100u,120u,1u);
    adc.frames=1u;
    adc.filtered[CONV_ADC_VBIAS]=2000u;
    ConvProtection_Service(1000u,&adc,&tmp);
    ConvProtection_GetSnapshot(&s);
    assert(s.dac_profile_ready && s.dac1_code==1900u);
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
    puts("conv protection/zero: PASS");
    return 0;
}
