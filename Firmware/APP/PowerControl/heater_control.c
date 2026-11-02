/* Persistent cooking policy, independent of the OLED and PM UI state.
 * 100 ms foreground supervisor; no heap, float, or control math in ISR.
 * All requested PWM/ISRs remain HARD-DISABLED during bring-up.
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
static uint32_t s_start_deadline_ms,s_stop_deadline_ms;
static int32_t s_integrator_mw,s_derivative_q4;
static int16_t s_previous_pot;
static uint8_t s_stop_timer_armed,s_power_manual,s_derivative_ready;
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
    s.mode=HEATER_MODE_TEMP;
    s.state=HEATER_OFF;
    s_proposed_target_mw=0u;
    s.target_temp_deci_c=800u;
    s.target_power_w=60u;
    s_stop_timer_armed=0u;
    s_power_manual=0u;
    s_start_deadline_ms=s_stop_deadline_ms=0u;
}
void Heater_SetMode(HeaterMode mode)
{
    if(mode!=HEATER_MODE_TEMP && mode!=HEATER_MODE_POWER)return;
    if(mode==s.mode)return;
    s.mode=mode;
    if(mode==HEATER_MODE_POWER)
    {
        /* Default power mode to the current nominal input ceiling,
         * matching DPC27A; L/R may immediately reduce the target. */
        s_power_manual=0u;
        s.target_power_w=(uint16_t)(s.budget_mw/1000u);
    }
    clear_output();
}
void Heater_SetTargetTemp(uint16_t deci_c)
{
    if(deci_c<HEATER_TEMP_MIN_DECI)deci_c=HEATER_TEMP_MIN_DECI;
    if(deci_c>HEATER_TEMP_MAX_DECI)deci_c=HEATER_TEMP_MAX_DECI;
    s.target_temp_deci_c=deci_c;
}
void Heater_SetTargetPowerW(uint16_t watts)
{
    uint32_t ceiling=s.budget_mw/1000u;
    if(ceiling>270u)ceiling=270u;
    if(watts>270u)watts=270u;
    if(ceiling && watts>ceiling)watts=(uint16_t)ceiling;
    s.target_power_w=watts;
    s_power_manual=1u;
}
void Heater_SetOffMinutes(uint16_t minutes,uint32_t now_ms)
{
    if(minutes>HEATER_MAX_MINUTES)minutes=HEATER_MAX_MINUTES;
    s.off_minutes=minutes;
    if(s.state==HEATER_RUN_REQUESTED && minutes)
    {
        s_stop_deadline_ms=now_ms+(uint32_t)minutes*60000u;
        s_stop_timer_armed=1u;
    }
    else if(s.state==HEATER_RUN_REQUESTED)
        s_stop_timer_armed=0u;
}
void Heater_ScheduleMinutes(uint16_t minutes,uint32_t now_ms)
{
    if(minutes>HEATER_MAX_MINUTES)minutes=HEATER_MAX_MINUTES;
    /* Scheduling and a current run are mutually exclusive. */
    if(!minutes)
    {
        s.on_minutes=0u;
        if(s.state==HEATER_WAIT_START || s.state==HEATER_WAIT_CONFIRM)
            Heater_Stop();
        return;
    }
    clear_output();
    s_stop_timer_armed=0u;
    s.on_minutes=minutes;
    s_start_deadline_ms=now_ms+(uint32_t)minutes*60000u;
    s.state=HEATER_WAIT_START;
}
void Heater_StartConfirmed(uint32_t now_ms)
{
    /* Physical hardware remains disabled. In a future power-stage port,
     * human confirmation must be paired with pot/overtemp/interlock checks. */
    if(s.state==HEATER_FAULT || s.state==HEATER_WAIT_START)return;
    s.state=HEATER_RUN_REQUESTED;
    s.on_minutes=0u;
    s_integrator_mw=0;
    s_derivative_ready=0u;
    s_derivative_q4=0;
    s_stop_timer_armed=s.off_minutes!=0u;
    if(s_stop_timer_armed)
        s_stop_deadline_ms=now_ms+(uint32_t)s.off_minutes*60000u;
}
void Heater_Stop(void)
{
    s.state=HEATER_OFF;
    s.on_minutes=0u;
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
    if(!obs)return;
    s.input=obs->input;
    s.bus_mv=obs->bus_mv;
    s.budget_mw=obs->budget_mw;
    if(s.mode==HEATER_MODE_POWER && !s_power_manual)
        s.target_power_w=(uint16_t)(obs->budget_mw/1000u);
    s.pot_deci_c=obs->pot_deci_c;
    s.sys_deci_c=obs->sys_deci_c;
    s.pot_valid=obs->pot_valid;
    s.sys_valid=obs->sys_valid;
    s.off_remaining_s=0u;
    s.on_remaining_s=0u;
    if(s.state==HEATER_WAIT_START)
    {
        s.on_remaining_s=remaining_s(now_ms,s_start_deadline_ms);
        if(s.on_remaining_s==0u)
        {
            /* Never auto-energize unattended induction heating. */
            s.state=HEATER_WAIT_CONFIRM;
            s.on_minutes=0u;
        }
    }
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
    if(!obs->budget_mw || !obs->pot_valid || !obs->sys_valid ||
       (uint32_t)(now_ms-obs->temperature_updated_ms)>HEATER_TEMP_MAX_AGE_MS ||
       obs->sys_deci_c>=HEATER_SYS_TRIP_DECI ||
       obs->pot_deci_c>=HEATER_POT_TRIP_DECI)
    {
        s.state=HEATER_FAULT;
        clear_output();
        return;
    }
    cap=(int32_t)obs->budget_mw;
    if(s.mode==HEATER_MODE_POWER)
    {
        uint32_t target=(uint32_t)s.target_power_w*1000u;
        s.requested_power_mw=(target>obs->budget_mw)?obs->budget_mw:target;
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
