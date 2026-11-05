#ifndef IH01A_CONV_CURRENT_H
#define IH01A_CONV_CURRENT_H
#include <stdint.h>
/* Revised CT_OUT nominal 0.075V/A. Assuming unchanged rectifier gain,
 * CT_RMS is nominally 0.1893975V/A after startup-zero subtraction.
 * Input is 12-bit at 2.5V. This is NOT yet a calibrated true-RMS
 * measurement or a fast overcurrent protection path. */
#define CONV_CT_OUT_MV_PER_A 75u
/* IBUS remains 0.2134V/A. Signed reading after IBUS startup zero;
 * foreground display estimate only, never used for fast protection. */
int32_t ConvCurrent_IbusMilliAmp(uint16_t filtered_raw,uint16_t zero_raw);
uint32_t ConvCurrent_CtRmsMilliAmp(uint16_t filtered_raw,uint16_t zero_raw);
#endif
