#include "conv_current.h"
uint32_t ConvCurrent_CtRmsMilliAmp(uint16_t filtered_raw,uint16_t zero_raw)
{
    uint32_t delta,den;
    if(filtered_raw>4095u)filtered_raw=4095u;
    if(zero_raw>4095u)zero_raw=4095u;
    if(filtered_raw<=zero_raw)return 0u;
    delta=(uint32_t)(filtered_raw-zero_raw);
    /* I_mA = delta * (2500mV/4095) * (1000mA/A)/(252.53mV/A)
     *       = delta * 250000000 / (4095 * 25253).
     * Intermediate must be uint64_t. No floats, no absolute-current offset. */
    den=4095u*25253u;
    return (uint32_t)(((uint64_t)delta*250000000u+den/2u)/den);
}
