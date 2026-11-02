#ifndef IH01A_HEATER_BUDGET_H
#define IH01A_HEATER_BUDGET_H
#include <stdint.h>
#include "PowerControl/heater_control.h"
/* DPC27A-style source selection. Real PD contract outranks VBUS heuristics.
 * No power is available during PD negotiation, even at high VBUS.
 * Mode parameter maps to PD_InputKind without importing the ST PD stack. */
#define HEATER_DCIN_MIN_MV 8000u
#define HEATER_DCIN_CURRENT_MA 7000u
#define HEATER_BUDGET_MAX_MW 270000u
HeaterObservation Heater_BudgetClassify(uint32_t bus_mv,uint8_t pd_mode,
    uint8_t contract_valid,uint32_t contract_mv,uint32_t contract_ma);
#endif
