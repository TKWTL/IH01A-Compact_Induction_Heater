#include <assert.h>
#include <stdio.h>
#include "PowerControl/heater_control.h"
#include "PowerControl/heater_budget.h"
#include "algorithm/pd_input.h"
static HeaterObservation sensor(uint32_t budget)
{
    HeaterObservation o={0};
    o.input=HEATER_INPUT_PD;o.budget_mw=budget;
    o.pot_valid=o.sys_valid=1u;
    o.zero_calibrated=1u;
    o.pot_deci_c=250;o.sys_deci_c=300;
    o.temperature_updated_ms=100u;
    return o;
}
int main(void)
{
    HeaterSnapshot s;
    HeaterObservation o=sensor(140000u);
    HeaterObservation dc=Heater_BudgetClassify(20000u,PD_INPUT_DCIN,0u,0u,0u);
    assert(dc.input==HEATER_INPUT_DCIN && dc.budget_mw==140000u);
    dc=Heater_BudgetClassify(20000u,PD_INPUT_PD,0u,0u,0u);
    assert(dc.input==HEATER_INPUT_WAIT && dc.budget_mw==0u);
    dc=Heater_BudgetClassify(5000u,PD_INPUT_DCIN,0u,0u,0u);
    assert(dc.budget_mw==0u);
    dc=Heater_BudgetClassify(28000u,PD_INPUT_PD,1u,28000u,5000u);
    assert(dc.input==HEATER_INPUT_PD && dc.budget_mw==137200u);
    /* RDO voltage boundary and control-to-watchdog +150mA. */
    assert(Heater_InputControlLimitMa(28000u,5000u)==4900u);
    assert(Heater_InputProtectionLimitMa(28000u,5000u)==5050u);
    assert(Heater_InputControlLimitMa(28000u,2600u)==2500u);
    assert(Heater_InputProtectionLimitMa(28000u,2600u)==2650u);
    assert(Heater_InputControlLimitMa(20000u,3000u)==2800u);
    assert(Heater_InputProtectionLimitMa(20000u,3000u)==2950u);
    assert(Heater_InputControlLimitMa(12000u,3000u)==2800u);
    assert(Heater_InputProtectionLimitMa(12000u,3000u)==2950u);
    assert(Heater_InputControlLimitMa(20001u,3000u)==2900u);
    assert(Heater_InputProtectionLimitMa(20001u,3000u)==3050u);
    assert(Heater_InputProtectionLimitMa(28000u,0u)==0u);
    assert(Heater_InputProtectionLimitMa(20000u,200u)==0u);
    assert(Heater_InputProtectionLimitMa(0u,3000u)==0u);
    dc=Heater_BudgetClassify(20000u,PD_INPUT_PD,1u,20000u,3000u);
    assert(dc.input==HEATER_INPUT_PD && dc.budget_mw==56000u);
    Heater_Init();
    assert(Heater_GetProposedTargetMw()==0u);
    Heater_GetSnapshot(&s);
    assert(s.mode==HEATER_MODE_POWER && s.target_phase_deg==180u);
    Heater_SetPhaseDegrees(90u);
    Heater_GetSnapshot(&s);
    assert(s.target_phase_deg==90u);
    Heater_SetPhaseDegrees(193u);
    Heater_GetSnapshot(&s);
    assert(s.target_phase_deg==180u);
    Heater_SetPhaseDegrees(0u);
    /* Requests during zero capture must not publish power to the ISR. */
    Heater_StartConfirmed(0u);
    o.zero_calibrated=0u;
    o.temperature_updated_ms=100u;
    Heater_Poll(100u,&o);
    Heater_GetSnapshot(&s);
    assert(s.requested_power_mw==0u && Heater_GetProposedTargetMw()==0u);
    assert(s.state==HEATER_RUN_REQUESTED);
    Heater_Stop();
    o.zero_calibrated=1u;
    Heater_LatchFault(0x01u);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_FAULT && s.fault_mask==1u);
    Heater_Stop(); /* fault must remain latched */
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_FAULT);
    Heater_AcknowledgeFault(); /* only called by verified controller path */
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF && s.fault_mask==0u);
    assert(s.target_phase_deg==0u); /* preserves the user setpoint */
    Heater_StartConfirmed(2u);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_RUN_REQUESTED);
    Heater_Stop();
    Heater_Init();
    assert(Heater_GetProposedTargetMw()==0u);
    /* No reservation / countdown start: RUN enters RUN_REQUESTED at once. */
    Heater_StartConfirmed(60100u);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_RUN_REQUESTED);
    o.temperature_updated_ms=60100u;
    Heater_SetOffMinutes(2u,60100u);
    Heater_Poll(60200u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_RUN_REQUESTED && s.off_remaining_s>0u);
    assert(s.requested_power_mw<=o.budget_mw && s.isr_target_mw==0u);
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_SetPhaseDegrees(120u);
    Heater_Poll(60300u,&o);
    Heater_GetSnapshot(&s);
    assert(s.target_phase_deg==120u && s.requested_power_mw==0u);
    assert(Heater_GetProposedTargetMw()==0u);
    assert(Heater_GetIsrTargetMw()==0u);
    o.budget_mw=80000u;
    Heater_Poll(60400u,&o);
    Heater_GetSnapshot(&s);
    assert(s.target_phase_deg==120u); /* no watts-to-phase mapping */
    Heater_SetMode(HEATER_MODE_TEMP);
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_Poll(60500u,&o);
    Heater_GetSnapshot(&s);
    assert(s.target_phase_deg==120u);
    o.budget_mw=140000u;
    Heater_Poll(60600u,&o);
    Heater_GetSnapshot(&s);
    assert(s.target_phase_deg==120u);
    o.temperature_updated_ms=180100u;
    Heater_Poll(180100u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF && s.requested_power_mw==0u);
    assert(Heater_GetProposedTargetMw()==0u);
    /* OFF countdown must survive 32-bit tick rollover. */
    Heater_Stop();
    Heater_SetOffMinutes(1u,0xffffff00u);
    Heater_StartConfirmed(0xffffff00u);
    o.temperature_updated_ms=0xffff0010u;
    Heater_Poll(0xffff0010u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_RUN_REQUESTED && s.off_remaining_s>0u);
    Heater_Poll(0xffffff00u+60000u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF); /* rollover-safe auto stop */
    Heater_Stop();
    Heater_StartConfirmed(200000u);
    o.temperature_updated_ms=0u;
    Heater_Poll(200000u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_FAULT && s.isr_target_mw==0u);
    assert(s.fault_mask==HEATER_FAULT_TEMP_STALE);
    Heater_Stop();
    Heater_AcknowledgeFault();
    o.temperature_updated_ms=200000u;
    o.budget_mw=0u;
    o.pot_valid=0u;
    o.sys_valid=1u;
    o.sys_deci_c=860;
    o.pot_deci_c=1310;
    Heater_StartConfirmed(200000u);
    Heater_Poll(200100u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_FAULT);
    assert((s.fault_mask&(HEATER_FAULT_NO_BUDGET|
                          HEATER_FAULT_NTC_INVALID|
                          HEATER_FAULT_SYS_OVERHEAT|
                          HEATER_FAULT_POT_OVERHEAT))==
                         (HEATER_FAULT_NO_BUDGET|
                          HEATER_FAULT_NTC_INVALID|
                          HEATER_FAULT_SYS_OVERHEAT|
                          HEATER_FAULT_POT_OVERHEAT));
    assert((s.fault_mask&HEATER_FAULT_TEMP_STALE)==0u);
    Heater_AcknowledgeFault();
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF && s.fault_mask==0u);
    /* Pan detection never runs by default or without a countdown, and its
     * switch must not change during an active RUN. */
    Heater_Init();
    Heater_GetSnapshot(&s);
    assert(s.pan_detect_enabled==0u);
    assert(Heater_SetPanDetectEnabled(1u));
    Heater_GetSnapshot(&s);
    assert(s.pan_detect_enabled==1u);
    Heater_StartConfirmed(1000u); /* no auto-stop duration => refused */
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF);
    Heater_SetOffMinutes(1u,1000u);
    Heater_SetMode(HEATER_MODE_TEMP);
    Heater_StartConfirmed(1000u); /* pulse mode never enters TEMP */
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF);
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_StartConfirmed(1000u);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_RUN_REQUESTED);
    assert(!Heater_SetPanDetectEnabled(0u));
    o=sensor(140000u);
    o.temperature_updated_ms=1500u;
    Heater_Poll(1500u,&o);
    Heater_GetSnapshot(&s);
    assert(s.pan_detect_enabled && s.off_remaining_s>0u);
    Heater_SetOffMinutes(0u,1500u); /* remove timer => stop, not forever RUN */
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF);
    assert(Heater_SetPanDetectEnabled(0u));
    Heater_GetSnapshot(&s);
    assert(s.pan_detect_enabled==0u);
    puts("heater control and budget: PASS");
    return 0;
}
