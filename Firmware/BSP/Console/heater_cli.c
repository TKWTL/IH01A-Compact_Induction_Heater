/* IH01A UART automation API. No raw PC6 writes, no HAL polling delays,
 * no floating point or heap. One response line per complete heater command.
 * Existing PD diagnostic logs remain independently routed to USART1.
 */
#include "heater_cli.h"
#include "heater_cli_parse.h"
#ifndef HEATER_CLI_HOST_TEST
#include "PowerControl/conv_controller.h"
#include "PowerControl/conv_phase.h"
#include "PowerControl/conv_zcd.h"
#include "PowerControl/conv_adc.h"
#include "PowerControl/conv_protection.h"
#include "PowerControl/conv_current.h"
#include "PowerControl/heater_control.h"
#include "framework/pm_api.h"
#include "main.h"
#include "tim.h"
#else
#include "heater_cli_mock.h"
#endif
#include <stdio.h>
#ifdef HEATER_CLI_HOST_TEST
#define printf HeaterCLI_MockPrintf
#endif

static const char *bench_reason(uint8_t code)
{
    switch(code)
    {
    case CONV_BENCH_MODE: return "MODE";
    case CONV_BENCH_PHASE:return "PHASE";
    case CONV_BENCH_NOPD: return "NO_PD";
    case CONV_BENCH_VOLT: return "PD_VOLT";
    case CONV_BENCH_CURR: return "PD_CURR";
    case CONV_BENCH_SOURCE:return "INPUT";
    case CONV_BENCH_BUS:  return "BUS";
    case CONV_BENCH_ZERO: return "ZERO";
    case CONV_BENCH_NTC:  return "NTC";
    case CONV_BENCH_HOT:  return "TEMP";
    case CONV_BENCH_AGE:  return "STALE";
    case CONV_BENCH_PWM:  return "PWM";
    case CONV_BENCH_COMP: return "COMP";
    case CONV_BENCH_FAULT:return "FAULT";
    case CONV_BENCH_BRK:  return "BREAK";
    default:              return "NOT_READY";
    }
}
static void report_snapshot(void)
{
    HeaterSnapshot h;
    ConvADC_Snapshot adc;
    ConvProtectionSnapshot protection;
    ConvZcdSnapshot zcd;
    const char *state;
    uint32_t now=HAL_GetTick(),current_ma;
    uint32_t actual_hz;
    uint16_t arr=(uint16_t)TIM1->ARR;
    char ic2[12],lag[16],current[18];
    unsigned phase,lag_abs;

    Heater_GetSnapshot(&h);
    ConvADC_CopySnapshot(&adc);
    ConvProtection_GetSnapshot(&protection);
    ConvZcd_GetSnapshot(&zcd);

    if(h.state==HEATER_FAULT || protection.fault_mask)
        state="FAULT";
    else if(ConvController_OutputEnabled() &&
            ConvController_OutputPinLevel())
        state="ON";
    else if(h.state==HEATER_RUN_REQUESTED)
        state="REQUESTED";
    else
        state="OFF";

    if(ConvController_OutputEnabled() &&
       zcd.valid && (uint32_t)(now-zcd.last_ms)<=250u &&
       zcd.phase_deg==ConvController_GetPhaseDegrees() &&
       zcd.capture_raw<=arr)
    {
        int val=(int)zcd.lag_deci_deg;
        lag_abs=(unsigned)(val<0?-val:val);
        (void)snprintf(ic2,sizeof(ic2),"%u",(unsigned)zcd.capture_raw);
        (void)snprintf(lag,sizeof(lag),"%c%u.%u",
                       val<0?'-':'+',lag_abs/10u,lag_abs%10u);
    }
    else
    {
        (void)snprintf(ic2,sizeof(ic2),"NA");
        (void)snprintf(lag,sizeof(lag),"NA");
    }

    if(adc.frames && ConvController_GetState()==CONV_STATE_ADC_MONITOR &&
       protection.zero_status==CONV_ZERO_READY)
    {
        if(adc.raw[CONV_ADC_CT_RMS]>=4090u)
            (void)snprintf(current,sizeof(current),"SAT");
        else
        {
            current_ma=ConvCurrent_CtRmsMilliAmp(
                          adc.filtered[CONV_ADC_CT_RMS],
                          protection.ct_zero_raw);
            (void)snprintf(current,sizeof(current),"%lu.%03lu",
                         (unsigned long)(current_ma/1000u),
                         (unsigned long)(current_ma%1000u));
        }
    }
    else
        (void)snprintf(current,sizeof(current),"NA");

    actual_hz=(CONV_TIM1_CLOCK_KHZ*1000u+(arr+1u)/2u)/(arr+1u);
    phase=(unsigned)ConvController_GetPhaseDegrees();
    printf("HCLI OK HEATER=%s PHASE_DEG=%u FREQ_KHZ=%u FREQ_HZ=%lu IC2=%s LAG_DEG=%s CT_RMS_A=%s\r\n",
           state,phase,(unsigned)ConvController_GetFrequencyKhz(),
           (unsigned long)actual_hz,ic2,lag,current);
}
uint8_t HeaterCLI_Dispatch(const char *line)
{
    HeaterCliRequest req;
    HeaterSnapshot heater;
    if(!HeaterCli_Parse(line,&req))return 0u;
    switch(req.op)
    {
    case HEATER_CLI_GET:
        report_snapshot();
        return 1u;

    case HEATER_CLI_PHASE:
        /* Share Heater state with MiaoUI, apply TIM1 preloads now; do not
         * energize the driver or modify the RUN request on success. */
        Heater_SetPhaseDegrees(req.value);
        ConvController_SetPhaseDegrees(req.value);
        Heater_GetSnapshot(&heater);
        if(heater.target_phase_deg==req.value &&
           ConvController_GetPhaseDegrees()==req.value)
            printf("HCLI OK PHASE_DEG=%u\r\n",
                   (unsigned)ConvController_GetPhaseDegrees());
        else printf("HCLI ERR APPLY\r\n");
        return 1u;

    case HEATER_CLI_FREQ:
        if(ConvController_SetFrequencyKhz(req.value))
            printf("HCLI OK FREQ_KHZ=%u\r\n",
                   (unsigned)ConvController_GetFrequencyKhz());
        else
            printf("HCLI ERR APPLY\r\n");
        return 1u;

    case HEATER_CLI_ON:
        if(ConvController_RequestBenchRunNow(HAL_GetTick()))
            printf("HCLI OK HEATER=ON\r\n");
        else
            printf("HCLI ERR HEATER_BLOCK=%s\r\n",
                   bench_reason(ConvController_BenchBlockReason()));
        return 1u;

    case HEATER_CLI_OFF:
        ConvController_BenchPowerOff();
        Heater_Stop();
        pm_api_refresh_idle();
        if(ConvController_OutputEnabled() || ConvController_OutputPinLevel())
            printf("HCLI ERR HEATER_OFF_FAILED\r\n");
        else
            printf("HCLI OK HEATER=OFF\r\n");
        return 1u;

    case HEATER_CLI_OUT_OF_RANGE:
        printf("HCLI ERR RANGE\r\n");
        return 1u;

    default:
        printf("HCLI ERR SYNTAX\r\n");
        return 1u;
    }
}
