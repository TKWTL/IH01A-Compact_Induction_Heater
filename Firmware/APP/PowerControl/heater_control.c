/* Persistent cooking policy, independent of the OLED and PM UI state.
 * 100 ms foreground supervisor; no heap, float, or control math in ISR.
 * TIM1 bench logic PWM may run with FD2204 PC6 held LOW.
 * Hardware heating remains disabled until separately commissioned.
 */
#include "PowerControl/heater_control.h"
#include <string.h>
#define HEATER_TEMP_MIN_DECI 400u
#define HEATER_TEMP_MAX_DECI 1200u
#define HEATER_MAX_MINUTES 1440u
#define HEATER_TEMP_MAX_AGE_MS 500u
#define HEATER_SYS_TRIP_DECI 850  /* provisional SW guard, not HW protection */
#define HEATER_POT_TRIP_DECI 1300 /* provisional SW guard, not HW protection */
#define HEATER_KP_MW_PER_DECI 300L
#define HEATER_KI_STEP_MW_PER_DECI 1L /* 100ms rate: slow integration */
#define HEATER_KD_MW_PER_DELTA_DECI 800L /* derivative on measurement,
                                             smoothed in Q4 */
static HeaterSnapshot s;
static uint32_t s_stop_deadline_ms;
static int32_t s_integrator_mw,s_derivative_q4;
static int16_t s_previous_pot;
static uint8_t s_stop_timer_armed,s_derivative_ready;
/* Cortex-M0+ naturally aligned single 32-bit store/load. Read-only from
 * any interrupt; no ISR should edit heater policy or these shared fields. */
static volatile uint32_t s_proposed_target_mw;

static uint32_t remaining_s(uint32_t now,uint32_t deadline)
{
    int32_t remaining=(int32_t)(deadline-now);
    return remaining>0 ? ((uint32_t)remaining+999u)/1000u : 0u;
}
static void clear_output(void)
{
    s.requested_power_mw=0u;
    s_proposed_target_mw=0u;
    s.isr_target_mw=0u;
    s_integrator_mw=0;
    s_derivative_q4=0;
    s_derivative_ready=0u;
}
void Heater_Init(void)
{
    memset(&s,0,sizeof(s));
    s.mode=HEATER_MODE_POWER; /* default manual phase bench mode */
    s.state=HEATER_OFF;
    s_proposed_target_mw=0u;
    s.target_temp_deci_c=800u;
    s.target_phase_deg=180u; /* default maximum bridge phase-shift */
    s.pan_detect_enabled=0u;
    s_stop_timer_armed=0u;
    s_stop_deadline_ms=0u;
}
void Heater_SetMode(HeaterMode mode)
{
    if(mode!=HEATER_MODE_TEMP && mode!=HEATER_MODE_POWER)return;
    if(mode==s.mode)return;
    s.mode=mode;
    /* Phase angle persists across mode switches; no watt inference. */
    clear_output();
}
void Heater_SetTargetTemp(uint16_t deci_c)
{
    if(deci_c<HEATER_TEMP_MIN_DECI)deci_c=HEATER_TEMP_MIN_DECI;
    if(deci_c>HEATER_TEMP_MAX_DECI)deci_c=HEATER_TEMP_MAX_DECI;
    s.target_temp_deci_c=deci_c;
}
void Heater_SetPhaseDegrees(uint16_t degrees)
{
    if(degrees>180u)degrees=180u;
    s.target_phase_deg=degrees;
    /* Only a configuration request; NEVER raises PC6. */
}
uint8_t Heater_SetPanDetectEnabled(uint8_t enabled)
{
    if(s.state!=HEATER_OFF)return 0u;
    s.pan_detect_enabled=(uint8_t)(enabled!=0u);
    return 1u;
}
void Heater_SetOffMinutes(uint16_t minutes,uint32_t now_ms)
{
    if(minutes>HEATER_MAX_MINUTES)minutes=HEATER_MAX_MINUTES;
    s.off_minutes=minutes;
    /* Disabling the stop timer cannot create an unlimited probe session. */
    if(s.pan_detect_enabled && s.state==HEATER_RUN_REQUESTED && !minutes)
    {
        Heater_Stop();
        return;
    }
    if(s.state==HEATER_RUN_REQUESTED && minutes)
    {
        s_stop_deadline_ms=now_ms+(uint32_t)minutes*60000u;
        s_stop_timer_armed=1u;
    }
    else if(s.state==HEATER_RUN_REQUESTED)
        s_stop_timer_armed=0u;
}
void Heater_StartConfirmed(uint32_t now_ms)
{
    /* Physical hardware remains disabled. In a future power-stage port,
     * human confirmation must be paired with pot/overtemp/interlock checks.
     * Deliberately immediate: there is no reservation/countdown start, so a
     * RUN press goes straight to RUN_REQUESTED. */
    if(s.state==HEATER_FAULT ||
       (s.pan_detect_enabled && (!s.off_minutes ||
                                s.mode!=HEATER_MODE_POWER)))return;
    s.state=HEATER_RUN_REQUESTED;
    s_integrator_mw=0;
    s_derivative_ready=0u;
    s_derivative_q4=0;
    s_stop_timer_armed=s.off_minutes!=0u;
    if(s_stop_timer_armed)
        s_stop_deadline_ms=now_ms+(uint32_t)s.off_minutes*60000u;
}
void Heater_LatchFault(uint32_t reason_mask)
{
    if(!reason_mask)return;
    s.fault_mask|=reason_mask;
    s.state=HEATER_FAULT;
    s_stop_timer_armed=0u;
    clear_output();
}
void Heater_AcknowledgeFault(void)
{
    /* Keep user setpoints; clear only the fault state and run proposal. */
    s.fault_mask=0u;
    if(s.state==HEATER_FAULT)s.state=HEATER_OFF;
    s_stop_timer_armed=0u;
    clear_output();
}
void Heater_Stop(void)
{
    if(s.state==HEATER_FAULT)
    {
        clear_output();
        return;
    }
    s.state=HEATER_OFF;
    s_stop_timer_armed=0u;
    clear_output();
}
void Heater_Poll(uint32_t now_ms,const HeaterObservation *obs)
{
    int32_t err;
    int32_t proportional;
    int32_t derivative;
    int32_t raw;
    int32_t cap;
    uint32_t policy_fault=0u;
    if(!obs)return;
    s.input=obs->input;
    s.bus_mv=obs->bus_mv;
    s.budget_mw=obs->budget_mw;
    s.pot_deci_c=obs->pot_deci_c;
    s.sys_deci_c=obs->sys_deci_c;
    s.pot_valid=obs->pot_valid;
    s.sys_valid=obs->sys_valid;
    s.off_remaining_s=0u;
    if(s.state!=HEATER_RUN_REQUESTED)
    {
        clear_output();
        return;
    }
    if(s_stop_timer_armed)
    {
        s.off_remaining_s=remaining_s(now_ms,s_stop_deadline_ms);
        if(!s.off_remaining_s){Heater_Stop();return;}
    }
    /* Software-level fail closed; real PWM/overcurrent protection is
     * implemented separately and is NOT commissioned here. */
    if(!obs->budget_mw)policy_fault|=HEATER_FAULT_NO_BUDGET;
    if(!obs->pot_valid || !obs->sys_valid)
        policy_fault|=HEATER_FAULT_NTC_INVALID;
    if((uint32_t)(now_ms-obs->temperature_updated_ms)>
       HEATER_TEMP_MAX_AGE_MS)policy_fault|=HEATER_FAULT_TEMP_STALE;
    if(obs->sys_deci_c>=HEATER_SYS_TRIP_DECI)
        policy_fault|=HEATER_FAULT_SYS_OVERHEAT;
    if(obs->pot_deci_c>=HEATER_POT_TRIP_DECI)
        policy_fault|=HEATER_FAULT_POT_OVERHEAT;
    if(policy_fault)
    {
        /* Previously set HEATER_FAULT without fault_mask, yielding
         * the misleading UI status FAULT:00000000. */
        s.fault_mask|=policy_fault;
        s.state=HEATER_FAULT;
        clear_output();
        return;
    }
    /* The ADC zero averages must finish with the bridge disabled before
     * any nonzero proposed target is published. This is still NOT an
     * authorization to drive TIM1. */
    if(!obs->zero_calibrated)
    {
        clear_output();
        return;
    }
    cap=(int32_t)obs->budget_mw;
    if(s.mode==HEATER_MODE_POWER)
    {
        /* Bench angle setting is NOT a watt request: 180deg != 180W.
         * It is sent to the TIM1 logic PWM with PC6 held LOW. */
        s.requested_power_mw=0u;
        s_integrator_mw=0;
    }
    else
    {
        /* Integer slow PI, 100ms cadence in thread_heater.
         * Anti-windup: integrate only when output is not limited (or when
         * the new error would move output away from saturation). */
        err=(int32_t)s.target_temp_deci_c-(int32_t)obs->pot_deci_c;
        proportional=HEATER_KP_MW_PER_DECI*err;
        /* D term is based on measured pot temperature, not the setpoint:
         * this avoids a large derivative kick when the user edits TARGET.
         * IIR Q4 smoothing reduces quantization noise at 10Hz. */
        if(!s_derivative_ready)
        {
            s_previous_pot=obs->pot_deci_c;
            s_derivative_q4=0;
            s_derivative_ready=1u;
        }
        else
        {
            int32_t delta=(int32_t)obs->pot_deci_c-s_previous_pot;
            s_derivative_q4+=(delta*16-s_derivative_q4)/4;
            s_previous_pot=obs->pot_deci_c;
        }
        derivative=-(HEATER_KD_MW_PER_DELTA_DECI*s_derivative_q4)/16;
        raw=proportional+s_integrator_mw+derivative;
        if((raw>=0 && raw<=cap) || (raw>cap && err<0) ||
           (raw<0 && err>0))
        {
            s_integrator_mw+=HEATER_KI_STEP_MW_PER_DECI*err;
            if(s_integrator_mw>cap)s_integrator_mw=cap;
            if(s_integrator_mw<0)s_integrator_mw=0;
        }
        raw=proportional+s_integrator_mw+derivative;
        if(raw<0)raw=0;
        if(raw>cap)raw=cap;
        s.requested_power_mw=(uint32_t)raw;
    }
    /* Publish a coherent single-word proposal for the future ISR without
     * allowing it to energize hardware. Commissioning gate stays at zero. */
    s_proposed_target_mw=s.requested_power_mw;
    s.isr_target_mw=0u;
}
void Heater_GetSnapshot(HeaterSnapshot *out)
{
    if(out)*out=s;
}
uint32_t Heater_GetProposedTargetMw(void)
{
    return s_proposed_target_mw;
}
uint32_t Heater_GetIsrTargetMw(void)
{
    return 0u; /* explicit HW commissioning gate */
}
