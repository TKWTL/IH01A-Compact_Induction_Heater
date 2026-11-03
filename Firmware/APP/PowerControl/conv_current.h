#ifndef IH01A_CONV_CURRENT_H
#define IH01A_CONV_CURRENT_H
#include <stdint.h>
/* CT_RMS analog voltage rises 0.25253V per ampere from the measured
 * no-switching startup zero. Input values are 12-bit ADC samples at 2.5V.
 * Returns 0 for readings <=zero; conversion is a display estimate,
 * NOT a fast current trip and NOT proof of a calibrated true-RMS detector. */
uint32_t ConvCurrent_CtRmsMilliAmp(uint16_t filtered_raw,uint16_t zero_raw);
#endif
