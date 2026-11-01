#ifndef IH01A_NTC_LUT_H
#define IH01A_NTC_LUT_H
#include <stdint.h>
#define NTC_T_MIN_DECI_C (-400)
#define NTC_T_STEP_DECI_C 50
#define NTC_TABLE_SIZE 39u
/* Clamped to -40..150C; return 0 if ADC code is outside nominal LUT
 * where an open/short sensor must NOT be treated as a valid temperature.
 * The converted clamped value is still written to out_deci_c.
 */
uint8_t NTC_SYS_Convert(uint16_t adc, int16_t *out_deci_c);
uint8_t NTC_POT_Convert(uint16_t adc, int16_t *out_deci_c);
#endif
