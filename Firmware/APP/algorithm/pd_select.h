#ifndef IH01A_PD_SELECT_H
#define IH01A_PD_SELECT_H
#include <stdint.h>
#include "algorithm/pd_epr_policy.h"
/* SPR ≤20V/7A board limit and EPR Fixed <32V/5A. ST typed RDO in pd_input.c.
 * The SPR current ceiling follows IH01A_PD_SPR_MAX_MA: the reference
 * EPR-capable baseline opens with a 20 V/5 A EPR-capable Request, and the
 * real value is still bounded by the Source PDO and the cable e-marker
 * (a 3 A cable makes the Source advertise 3 A, so 3 A is what we get). */
#define PD_INPUT_MAX_MV 32000u
#define PD_INPUT_SPR_MAX_MV 20000u
#define PD_INPUT_MAX_MA IH01A_PD_SPR_MAX_MA
#define PD_INPUT_EPR_MAX_MA 5000u /* Only for genuine EPR 5A-qualified contract */
uint8_t PD_Input_BestFixedLimited(const uint8_t *data,uint32_t bytes,
                                  uint32_t max_mv,uint8_t *position,
                                  uint32_t *voltage_mv,uint32_t *current_ma);
uint8_t PD_Input_BestFixed(const uint8_t *data,uint32_t bytes,
                           uint8_t *position,uint32_t *voltage_mv,
                           uint32_t *current_ma);
/* Pass the full combined EPR Source Capabilities list (8..13 objects):
 * SPR positions 1..7 followed by EPR positions 8..13. */
uint8_t PD_Input_BestEPRFixed(const uint8_t *epr_data,uint32_t bytes,
                             uint8_t *position,uint32_t *voltage_mv,
                             uint32_t *current_ma,uint32_t *selected_pdo);
#endif
