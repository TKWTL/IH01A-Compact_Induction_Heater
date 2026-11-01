#include "temp_service.h"
#include "conv_adc.h"
#include "conv_controller.h"
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static uint32_t g_now;
static ConvState g_state=CONV_STATE_ADC_MONITOR;
static ConvADC_Snapshot g_frame;
static uint32_t g_mcu_calls;
uint32_t HAL_GetTick(void) { return g_now; }
ConvState ConvController_GetState(void) { return g_state; }
void ConvADC_CopySnapshot(ConvADC_Snapshot *out) { *out=g_frame; }
uint8_t ConvADC_McuTempDeciC(uint16_t raw,int32_t *out)
{
    ++g_mcu_calls;
    *out=(int32_t)raw;
    return 1u;
}
int main(void)
{
    TempService_Snapshot s;
    g_frame.frames=1u;
    g_frame.filtered[CONV_ADC_NTC_SYS]=2871u; /* 20C */
    g_frame.filtered[CONV_ADC_NTC_POT]=2064u; /* 90C */
    g_frame.filtered[CONV_ADC_MCU_TEMP]=333u;
    g_frame.raw[CONV_ADC_NTC_SYS]=3500u; /* prove filtered used */
    TempService_Poll();
    TempService_GetSnapshot(&s);
    assert(s.valid==(TEMP_VALID_SYS|TEMP_VALID_POT|TEMP_VALID_MCU));
    assert(s.sys_deci_c==200&&s.pot_deci_c==900&&s.mcu_deci_c==333);
    assert(g_mcu_calls==1u);
    g_now=99u;
    g_frame.frames=2u;
    g_frame.filtered[CONV_ADC_NTC_SYS]=2061u;
    TempService_Poll();
    assert(g_mcu_calls==1u); /* throttled */
    g_now=100u;
    TempService_Poll();
    TempService_GetSnapshot(&s);
    assert(s.sys_deci_c==400&&g_mcu_calls==2u);
    g_now=200u;
    g_frame.frames=3u;
    g_frame.filtered[CONV_ADC_NTC_SYS]=4095u;
    TempService_Poll();
    TempService_GetSnapshot(&s);
    assert((s.valid&TEMP_VALID_SYS)==0u);
    assert((s.valid&TEMP_VALID_POT)!=0u);
    g_now=300u;
    g_frame.frames=4u;
    g_state=CONV_STATE_FAULT;
    TempService_Poll();
    TempService_GetSnapshot(&s);
    assert(s.valid==0u);
    puts("temperature service: PASS");
    return 0;
}
