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
    assert(dc.input==HEATER_INPUT_PD && dc.budget_mw==140000u);
    Heater_Init();
    assert(Heater_GetProposedTargetMw()==0u);
    Heater_ScheduleMinutes(1u,100u);
    Heater_Poll(101u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_WAIT_START && s.requested_power_mw==0u);
    Heater_Poll(60100u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_WAIT_CONFIRM && Heater_GetIsrTargetMw()==0u);
    Heater_StartConfirmed(60100u);
    o.temperature_updated_ms=60100u;
    Heater_SetOffMinutes(2u,60100u);
    Heater_Poll(60200u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_RUN_REQUESTED && s.off_remaining_s>0u);
    assert(s.requested_power_mw<=o.budget_mw && s.isr_target_mw==0u);
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_GetSnapshot(&s);
    assert(s.target_power_w==140u); /* initially V*I or contract ceiling */
    Heater_SetTargetPowerW(120u);
    Heater_Poll(60300u,&o);
    Heater_GetSnapshot(&s);
    assert(s.requested_power_mw==120000u);
    assert(Heater_GetProposedTargetMw()==120000u);
    assert(Heater_GetIsrTargetMw()==0u);
    o.budget_mw=80000u;
    Heater_Poll(60400u,&o);
    Heater_GetSnapshot(&s);
    assert(s.requested_power_mw==80000u);
    Heater_SetMode(HEATER_MODE_TEMP);
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_Poll(60500u,&o);
    Heater_GetSnapshot(&s);
    assert(s.target_power_w==80u);
    o.budget_mw=140000u;
    Heater_Poll(60600u,&o);
    Heater_GetSnapshot(&s);
    assert(s.target_power_w==140u); /* auto tracks on new mode entry */
    o.temperature_updated_ms=180100u;
    Heater_Poll(180100u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_OFF && s.requested_power_mw==0u);
    assert(Heater_GetProposedTargetMw()==0u);
    Heater_ScheduleMinutes(1u,0xffffff00u);
    Heater_Poll(0xffff0010u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_WAIT_START);
    Heater_Poll(0xffffff00u+60000u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_WAIT_CONFIRM); /* rollover-safe */
    Heater_Stop();
    Heater_StartConfirmed(200000u);
    o.temperature_updated_ms=0u;
    Heater_Poll(200000u,&o);
    Heater_GetSnapshot(&s);
    assert(s.state==HEATER_FAULT && s.isr_target_mw==0u);
    Heater_Stop();
    puts("heater control and budget: PASS");
    return 0;
}
