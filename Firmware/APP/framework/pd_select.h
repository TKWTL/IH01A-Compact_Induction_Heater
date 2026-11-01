#ifndef IH01A_PD_SELECT_H
#define IH01A_PD_SELECT_H
#include <stdint.h>
/* Fixed SPR PDOs: 5..20V, strict project ceiling 32V, <=3A.
 * The bundled MINSNK USB-PD 3.0 library has no EPR entry sequence. */
#define PD_INPUT_MAX_MV 32000u
#define PD_INPUT_SPR_MAX_MV 20000u
#define PD_INPUT_MAX_MA 3000u
uint8_t PD_Input_BestFixed(const uint8_t *data,uint32_t bytes,
                           uint8_t *position,uint32_t *voltage_mv,
                           uint32_t *current_ma);
uint32_t PD_Input_FixedRDO(uint8_t position,uint32_t current_ma);
#endif
