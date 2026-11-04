/* IH01A frequency-only phase loop: phase command controls power, while
 * frequency keeps the CT-detected current on the inductive side.
 * The current CT_ZERO delay regression is EMPTY-COIL only. This is an
 * experimental 12V bench controller, NOT a validated ZVS interlock.
 * Low current holds 300kHz; no blind frequency descent is permitted. */
#include "conv_auto_freq.h"
#ifndef CONV_AUTO_FREQ_HOST_TEST
#include "conv_controller.h"
#include "conv_zcd.h"
#include "conv_adc.h"
#include "conv_protection.h"
#include "conv_current.h"
#include "heater_control.h"
#include "tim.h"
#endif

static ConvAutoFreqCore s_core;
static ConvAutoFreqSnapshot s_snapshot;
static uint8_t s_enabled;
static uint32_t s_last_poll_ms;
#ifndef CONV_AUTO_FREQ_HOST_TEST
static uint32_t s_last_capture_sequence;
#endif

void ConvAutoFreq_CoreReset(ConvAutoFreqCore *core)
{
    if(!core)return;
    core->state=CONV_AF_OFF;
    core->ibus_locked=0u;
    core->good_samples=0u;
    core->bad_samples=0u;
}
void ConvAutoFreq_CoreStep(ConvAutoFreqCore *core,
                           const ConvAutoFreqInput *in,
                           ConvAutoFreqDecision *out)
{
    if(!core || !in || !out)return;
    out->requested_arr=in->arr;
    out->trip=0u;
    if(!in->enabled)
    {
        ConvAutoFreq_CoreReset(core);
        out->state=CONV_AF_OFF;
        return;
    }
    if(!in->output_on)
    {
        ConvAutoFreq_CoreReset(core);
        core->state=CONV_AF_SAFE;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    /* ADC zero, COMP, BREAK, and phase >=120deg are prerequisites. */
    if(!in->hardware_ok)
    {
        core->state=CONV_AF_FAULT;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        out->trip=1u;
        return;
    }
    if(!core->ibus_locked && in->ibus_delta_raw>=CONV_AF_LOCK_RAW)
        core->ibus_locked=1u;
    else if(core->ibus_locked &&
            in->ibus_delta_raw<=CONV_AF_RELEASE_RAW)
        core->ibus_locked=0u;
    if(!core->ibus_locked)
    {
        core->state=CONV_AF_ACQUIRE;
        core->good_samples=0u;
        core->bad_samples=0u;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    /* No fitted current-zero edge => high-frequency recovery.
     * Persistent missing feedback at nontrivial input current trips after
     * 20*5ms=100ms. Never substitute the raw, uncompensated angle. */
    if(!in->phase_fit)
    {
        core->state=CONV_AF_RECOVER;
        core->good_samples=0u;
        if(core->bad_samples<20u)++core->bad_samples;
        if(core->bad_samples>=20u)
        {
            core->state=CONV_AF_FAULT;
            out->trip=1u;
        }
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    /* Zero or negative lag is capacitive/ambiguous: withdraw PC6 first.
     * +0..+5deg is too close to the boundary: recover to 300kHz,
     * and trip if it persists for 6 samples. */
    if(in->lag_corr_deci_deg<=0)
    {
        core->state=CONV_AF_FAULT;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        out->trip=1u;
        return;
    }
    if(in->lag_corr_deci_deg<50)
    {
        core->state=CONV_AF_RECOVER;
        core->good_samples=0u;
        if(core->bad_samples<6u)++core->bad_samples;
        if(core->bad_samples>=6u)
        {
            core->state=CONV_AF_FAULT;
            out->trip=1u;
        }
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    core->bad_samples=0u;
    if(core->good_samples<3u)++core->good_samples;
    if(core->good_samples<3u)
    {
        core->state=CONV_AF_ACQUIRE;
        out->state=core->state;
        /* Enter the loop without a sudden downward step. */
        return;
    }
    core->state=CONV_AF_TRACK;
    out->state=core->state;
    if(in->arr<CONV_AF_START_ARR || in->arr>CONV_AF_MIN_ARR)
    {
        out->requested_arr=CONV_AF_START_ARR;
        core->state=CONV_AF_RECOVER;
        out->state=core->state;
        return;
    }
    /* One TIM1 tick per 5ms; ARR increases as frequency decreases.
     * Positive current lag means inductive; never descend while <18deg. */
    if(in->lag_corr_deci_deg>CONV_AF_TARGET_DECI+CONV_AF_DEADBAND_DECI)
    {
        if(in->arr<CONV_AF_MIN_ARR)
            out->requested_arr=(uint16_t)(in->arr+1u);
    }
    else if(in->lag_corr_deci_deg<
                CONV_AF_TARGET_DECI-CONV_AF_DEADBAND_DECI)
    {
        if(in->arr>CONV_AF_START_ARR)
            out->requested_arr=(uint16_t)(in->arr-1u);
    }
}
const char *ConvAutoFreq_StateName(ConvAutoFreqState state)
{
    switch(state)
    {
    case CONV_AF_OFF: return "OFF";
    case CONV_AF_SAFE: return "SAFE";
    case CONV_AF_ACQUIRE: return "ACQUIRE";
    case CONV_AF_TRACK: return "TRACK";
    case CONV_AF_RECOVER: return "RECOVER";
    case CONV_AF_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}
void ConvAutoFreq_Init(void)
{
    ConvAutoFreq_CoreReset(&s_core);
    s_enabled=0u; /* explicit operator opt-in; regression not yet validated */
    s_snapshot.enabled=0u;
    s_snapshot.state=CONV_AF_OFF;
    s_snapshot.ibus_locked=0u;
    s_snapshot.phase_fit=0u;
    s_snapshot.ibus_delta_raw=0u;
    s_snapshot.requested_arr=CONV_AF_START_ARR;
    s_snapshot.lag_corr_deci_deg=0;
    s_snapshot.last_ms=0u;
    s_last_poll_ms=0u;
#ifndef CONV_AUTO_FREQ_HOST_TEST
    s_last_capture_sequence=0u;
#endif
}
uint8_t ConvAutoFreq_SetEnabled(uint8_t enabled)
{
    enabled=(uint8_t)(enabled!=0u);
    if(enabled==s_enabled)return 1u;
#ifndef CONV_AUTO_FREQ_HOST_TEST
    /* Both transitions move to the conservative high-frequency end,
     * without interrupting PC6 or TIM1, using atomic staged preloads. */
    if(!ConvController_SetFrequencyArr(CONV_AF_START_ARR))
        return 0u;
#endif
    s_enabled=enabled;
    ConvAutoFreq_CoreReset(&s_core);
    s_snapshot.state=enabled?CONV_AF_SAFE:CONV_AF_OFF;
    s_snapshot.enabled=enabled;
    return 1u;
}
uint8_t ConvAutoFreq_Enabled(void){return s_enabled;}
void ConvAutoFreq_GetSnapshot(ConvAutoFreqSnapshot *out)
{
    if(out)*out=s_snapshot;
}
void ConvAutoFreq_Poll(uint32_t now_ms)
{
#ifndef CONV_AUTO_FREQ_HOST_TEST
    ConvADC_Snapshot adc;
    ConvProtectionSnapshot protection;
    ConvZcdSnapshot zcd;
    ConvAutoFreqInput in;
    ConvAutoFreqDecision action;
    uint32_t ct_ma=0u;
    uint8_t ct_valid=0u;
    uint32_t current_seq;

    if((uint32_t)(now_ms-s_last_poll_ms)<CONV_AF_INTERVAL_MS)return;
    s_last_poll_ms=now_ms;
    ConvADC_CopySnapshot(&adc);
    ConvProtection_GetSnapshot(&protection);
    /* Poll a NEW CH2 capture every 5ms, independent of OLED or serial get.
     * Never reuse a stale fit after ARR/phase has been staged. */
    ConvZcd_Poll(now_ms);
    ConvZcd_GetSnapshot(&zcd);
    current_seq=zcd.sequence;
    in.enabled=s_enabled;
    in.output_on=ConvController_OutputEnabled();
    in.arr=(uint16_t)TIM1->ARR;
    in.ibus_delta_raw=0u;
    if(protection.zero_status==CONV_ZERO_READY &&
       adc.filtered[CONV_ADC_IBUS]>protection.ibus_zero_raw)
        in.ibus_delta_raw=(uint16_t)
              (adc.filtered[CONV_ADC_IBUS]-protection.ibus_zero_raw);
    in.hardware_ok=(uint8_t)(
        protection.zero_status==CONV_ZERO_READY &&
        protection.fault_mask==0u &&
        adc.frames!=0u &&
        adc.raw[CONV_ADC_IBUS]<4090u &&
        ConvController_GetState()==CONV_STATE_ADC_MONITOR &&
        ConvController_LogicPwmRunning() &&
        ConvController_GetPhaseDegrees()>=120u);
    if(protection.zero_status==CONV_ZERO_READY && adc.frames &&
       adc.raw[CONV_ADC_CT_RMS]<4090u)
    {
        ct_ma=ConvCurrent_CtRmsMilliAmp(
                        adc.filtered[CONV_ADC_CT_RMS],
                        protection.ct_zero_raw);
        ct_valid=1u;
    }
    in.phase_fit=(uint8_t)(
        in.output_on &&
        zcd.valid &&
        (uint32_t)(now_ms-zcd.last_ms)<=15u &&
        current_seq!=s_last_capture_sequence &&
        zcd.phase_deg==ConvController_GetPhaseDegrees() &&
        ConvZcd_CorrectionStatus(&zcd,ct_ma,ct_valid)==CONV_ZCD_CAL_FIT);
    in.lag_corr_deci_deg=zcd.lag_corr_deci_deg;
    s_last_capture_sequence=current_seq;
    ConvAutoFreq_CoreStep(&s_core,&in,&action);
    s_snapshot.state=action.state;
    s_snapshot.enabled=s_enabled;
    s_snapshot.ibus_locked=s_core.ibus_locked;
    s_snapshot.phase_fit=in.phase_fit;
    s_snapshot.ibus_delta_raw=in.ibus_delta_raw;
    s_snapshot.lag_corr_deci_deg=in.phase_fit?in.lag_corr_deci_deg:0;
    s_snapshot.requested_arr=action.requested_arr;
    s_snapshot.last_ms=now_ms;

    if(action.trip)
    {
        /* The bridge enable is removed before latching the fault. Never
         * auto-restart after loss of phase feedback at >0.5A. */
        ConvController_BenchPowerOff();
        Heater_LatchFault(CONV_PROT_AUTO_FREQ);
        return;
    }
    if(action.requested_arr!=in.arr &&
       !ConvController_SetFrequencyArr(action.requested_arr))
    {
        s_snapshot.state=CONV_AF_FAULT;
        ConvController_BenchPowerOff();
        Heater_LatchFault(CONV_PROT_AUTO_FREQ);
    }
#else
    (void)now_ms;
#endif
}
