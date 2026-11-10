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
#include "PowerControl/conv_auto_freq.h"
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
    case CONV_BENCH_COUNTDOWN:return "COUNTDOWN";
    default:              return "NOT_READY";
    }
}
static const char *lag_quality(ConvZcdCalStatus quality)
{
    switch(quality)
    {
    case CONV_ZCD_CAL_FIT:          return "FIT";
    case CONV_ZCD_CAL_NO_CAPTURE:   return "NO_IC2";
    case CONV_ZCD_CAL_FREQ_RANGE:   return "FREQ";
    case CONV_ZCD_CAL_RECAL_REQUIRED: return "RECAL";
    case CONV_ZCD_CAL_PHASE_RANGE:  return "PHASE";
    case CONV_ZCD_CAL_ADC_INVALID:  return "ADC";
    case CONV_ZCD_CAL_LOW_CURRENT:  return "LOW_CT";
    default:                        return "NO_IC2";
    }
}
static const char *pan_step_name(uint8_t step)
{
    switch(step)
    {
    case 0u:return "IDLE";
    case 1u:return "ARMED";
    case 2u:return "ACTIVE";
    case 3u:return "COLLECT";
    case 4u:return "ABORTED";
    default:return "UNKNOWN";
    }
}
static void report_snapshot(void)
{
    HeaterSnapshot h;
    ConvADC_Snapshot adc;
    ConvProtectionSnapshot protection;
    ConvZcdSnapshot zcd;
    ConvAutoFreqSnapshot af;
    const char *state;
    ConvZcdCalStatus quality=CONV_ZCD_CAL_NO_CAPTURE;
    uint32_t now=HAL_GetTick(),current_ma=0u;
    uint32_t actual_hz;
    uint16_t arr=(uint16_t)TIM1->ARR;
    char ic2[12],lag[16],lag_raw[16],current[18];
    unsigned phase,lag_abs;
    uint8_t ct_valid=0u;

    Heater_GetSnapshot(&h);
    ConvADC_CopySnapshot(&adc);
    ConvProtection_GetSnapshot(&protection);
    /* Active GET obtains the latest CCR2 instead of waiting for the
     * 100ms heater thread. This is a register read, no CC2 DMA/IRQ. */
    ConvZcd_Poll(now);
    ConvZcd_GetSnapshot(&zcd);
    ConvAutoFreq_GetSnapshot(&af);

    if(h.state==HEATER_FAULT || protection.fault_mask)
        state="FAULT";
    else if(ConvController_OutputEnabled() &&
            ConvController_OutputPinLevel())
        state="ON";
    else if(h.state==HEATER_RUN_REQUESTED)
        state="REQUESTED";
    else
        state="OFF";

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
            ct_valid=1u;
            (void)snprintf(current,sizeof(current),"%lu.%03lu",
                         (unsigned long)(current_ma/1000u),
                         (unsigned long)(current_ma%1000u));
        }
    }
    else
        (void)snprintf(current,sizeof(current),"NA");

    if(ConvController_OutputEnabled() &&
       zcd.valid && (uint32_t)(now-zcd.last_ms)<=250u &&
       zcd.phase_deg==ConvController_GetPhaseDegrees() &&
       zcd.capture_raw<=arr)
    {
        int raw=(int)zcd.lag_deci_deg;
        lag_abs=(unsigned)(raw<0?-raw:raw);
        (void)snprintf(ic2,sizeof(ic2),"%u",(unsigned)zcd.capture_raw);
        (void)snprintf(lag_raw,sizeof(lag_raw),"%c%u.%u",
                       raw<0?'-':'+',lag_abs/10u,lag_abs%10u);
        quality=ConvZcd_CorrectionStatus(&zcd,current_ma,ct_valid);
        if(quality==CONV_ZCD_CAL_FIT)
        {
            int corr=(int)zcd.lag_corr_deci_deg;
            lag_abs=(unsigned)(corr<0?-corr:corr);
            (void)snprintf(lag,sizeof(lag),"%c%u.%u",
                            corr<0?'-':'+',lag_abs/10u,lag_abs%10u);
        }
        else
            (void)snprintf(lag,sizeof(lag),"NA");
    }
    else
    {
        (void)snprintf(ic2,sizeof(ic2),"NA");
        (void)snprintf(lag_raw,sizeof(lag_raw),"NA");
        (void)snprintf(lag,sizeof(lag),"NA");
    }

    actual_hz=(CONV_TIM1_CLOCK_KHZ*1000u+(arr+1u)/2u)/(arr+1u);
    phase=(unsigned)ConvController_GetPhaseDegrees();
    /* Preserve IC2 and raw lag for regression; LAG_DEG is now the
     * fitted corrected angle, or NA whenever the fit gate is invalid. */
    printf("HCLI OK HEATER=%s PHASE_DEG=%u FREQ_KHZ=%u FREQ_HZ=%lu IC2=%s LAG_DEG=%s LAG_RAW_DEG=%s LAG_QUAL=%s CT_RMS_A=%s AUTO_FREQ=%s AF_STATE=%s IBUS_DELTA=%u PAN=%s OFF_MIN=%u LEFT_S=%lu PAN_STEP=%s PAN_SEQ=%lu\r\n",
           state,phase,(unsigned)ConvController_GetFrequencyKhz(),
           (unsigned long)actual_hz,ic2,lag,lag_raw,
           lag_quality(quality),current,af.enabled?"ON":"OFF",
           ConvAutoFreq_StateName(af.state),(unsigned)af.ibus_delta_raw,
           h.pan_detect_enabled?"ON":"OFF",(unsigned)h.off_minutes,
           (unsigned long)h.off_remaining_s,
           pan_step_name(ConvController_PanProbeState()),
           (unsigned long)ConvController_PanProbeSequence());
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

    case HEATER_CLI_AUTO_ON:
    case HEATER_CLI_AUTO_OFF:
        if(req.op==HEATER_CLI_AUTO_ON && !CONV_ZCD_CT75_CALIBRATED)
        {
            printf("HCLI ERR CT_ZCD_RECAL_REQUIRED\r\n");
            return 1u;
        }
        if(ConvAutoFreq_SetEnabled(req.op==HEATER_CLI_AUTO_ON))
            printf("HCLI OK AUTO_FREQ=%s\r\n",
                   ConvAutoFreq_Enabled()?"ON":"OFF");
        else
            printf("HCLI ERR APPLY\r\n");
        return 1u;

    case HEATER_CLI_FREQ:
        if(ConvAutoFreq_Enabled())
            printf("HCLI ERR AUTO_FREQ_ENABLED\r\n");
        else if(ConvController_SetFrequencyKhz(req.value))
            printf("HCLI OK FREQ_KHZ=%u\r\n",
                   (unsigned)ConvController_GetFrequencyKhz());
        else
            printf("HCLI ERR APPLY\r\n");
        return 1u;

    case HEATER_CLI_ON:
        if(ConvController_RequestBenchRunNow(HAL_GetTick()))
        {
            Heater_GetSnapshot(&heater);
            printf("HCLI OK HEATER=%s\r\n",
                   heater.pan_detect_enabled?"PAN_REQUESTED":"ON");
        }
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
