#include "conv_current.h"
uint32_t ConvCurrent_CtRmsMilliAmp(uint16_t filtered_raw,uint16_t zero_raw)
{
    uint32_t delta,den;
    if(filtered_raw>4095u)filtered_raw=4095u;
    if(zero_raw>4095u)zero_raw=4095u;
    if(filtered_raw<=zero_raw)return 0u;
    delta=(uint32_t)(filtered_raw-zero_raw);
    /* CT_OUT 0.100 -> 0.075 V/A (3/4). Unchanged rectifier gain implies
     * CT_RMS 0.25253*3/4 = 0.1893975 V/A. Bench recalibration required.
     * I[mA] = delta * 1,000,000,000 / (4095 * 25253 * 3). */
    den=4095u*25253u*3u;
    return (uint32_t)(((uint64_t)delta*1000000000u+den/2u)/den);
}
