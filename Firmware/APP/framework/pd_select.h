#ifndef IH01A_PD_SELECT_H
#define IH01A_PD_SELECT_H
#include <stdint.h>
/* Fixed SPR PDOs: 5..20V, strict project ceiling 32V, <=3A.
 * EPR helpers below are for a future PD3_FULL stack migration; the linked
 * MINSNK library cannot enter EPR and must not be told otherwise. */
#define PD_INPUT_MAX_MV 32000u
#define PD_INPUT_SPR_MAX_MV 20000u
#define PD_INPUT_MAX_MA 3000u
uint8_t PD_Input_BestFixed(const uint8_t *data,uint32_t bytes,
                           uint8_t *position,uint32_t *voltage_mv,
                           uint32_t *current_ma);
uint32_t PD_Input_FixedRDO(uint8_t position,uint32_t current_ma);
/* EPR Source Capabilities carry up to six extra objects following the seven
 * SPR slots. EPR fixed PDO positions therefore start at object 8. */
uint8_t PD_Input_BestEPRFixed(const uint8_t *epr_data,uint32_t bytes,
                             uint8_t *position,uint32_t *voltage_mv,
                             uint32_t *current_ma,uint32_t *selected_pdo);
/* Generates only the RDO; the EPR_REQUEST also needs a copy of the PDO. */
uint32_t PD_Input_EPRFixedRDO(uint8_t position,uint32_t current_ma);
#endif
