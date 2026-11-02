#include "PowerControl/heater_budget.h"
#include "algorithm/pd_input.h"
HeaterObservation Heater_BudgetClassify(uint32_t bus_mv,uint8_t pd_mode,
    uint8_t contract_valid,uint32_t contract_mv,uint32_t contract_ma)
{
    HeaterObservation out={0};
    uint32_t power;
    out.bus_mv=bus_mv;
    if(contract_valid && contract_mv>0u && contract_ma>0u)
    {
        out.input=HEATER_INPUT_PD;
        /* Millivolts x milliamps -> milliwatts; use 64-bit to avoid overflow.
         * This reflects the negotiated RDO current allowance, not draw. */
        power=(uint32_t)(((uint64_t)contract_mv*(uint64_t)contract_ma)/1000u);
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
