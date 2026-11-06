#include "PowerControl/heater_budget.h"
#include "algorithm/pd_input.h"
/* Zero/undersized contracts must not cause unsigned underflow. */
uint32_t Heater_InputControlLimitMa(uint32_t contract_mv,uint32_t contract_ma)
{
    uint32_t margin;
    if(!contract_mv || !contract_ma)return 0u;
    margin=contract_mv>HEATER_RDO_HIGH_VOLTAGE_MV?
           HEATER_RDO_HIGH_MARGIN_MA:HEATER_RDO_LOW_MARGIN_MA;
    return contract_ma>margin?contract_ma-margin:0u;
}
uint32_t Heater_InputProtectionLimitMa(uint32_t contract_mv,uint32_t contract_ma)
{
    uint32_t control=Heater_InputControlLimitMa(contract_mv,contract_ma);
    if(!control)return 0u;
    if(control>UINT32_MAX-HEATER_IBUS_PROTECTION_HEADROOM_MA)
        return UINT32_MAX;
    return control+HEATER_IBUS_PROTECTION_HEADROOM_MA;
}
HeaterObservation Heater_BudgetClassify(uint32_t bus_mv,uint8_t pd_mode,
    uint8_t contract_valid,uint32_t contract_mv,uint32_t contract_ma)
{
    HeaterObservation out={0};
    uint32_t power,control_ma;
    out.bus_mv=bus_mv;
    if(contract_valid && contract_mv>0u && contract_ma>0u)
    {
        out.input=HEATER_INPUT_PD;
        /* Future control power budget reserves 100mA (>20V) or 200mA
         * (<=20V) from the RDO current. This does not close a loop. */
        control_ma=Heater_InputControlLimitMa(contract_mv,contract_ma);
        power=(uint32_t)(((uint64_t)contract_mv*(uint64_t)control_ma)/1000u);
    }
    else if(pd_mode==PD_INPUT_DCIN && bus_mv>=HEATER_DCIN_MIN_MV)
    {
        out.input=HEATER_INPUT_DCIN;
        power=(uint32_t)(((uint64_t)bus_mv*HEATER_DCIN_CURRENT_MA)/1000u);
    }
    else
    {
        out.input=(pd_mode==PD_INPUT_WAIT || pd_mode==PD_INPUT_PD) ?
                  HEATER_INPUT_WAIT:HEATER_INPUT_NONE;
        return out;
    }
    /* DPC's hard software ceiling is retained as an uncommissioned upper
     * sanity bound; real IH01A hardware limits may be lower. */
    if(power>HEATER_BUDGET_MAX_MW)power=HEATER_BUDGET_MAX_MW;
    out.budget_mw=power;
    return out;
}
