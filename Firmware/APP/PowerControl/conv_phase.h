#ifndef IH01A_CONV_PHASE_H
#define IH01A_CONV_PHASE_H
#include <stdint.h>
/* Pure mapping from user phase angle to TIM1 up-counting PWM registers.
 * CH1 PWM1: CCR1=ARR/2. CH3 Combined PWM2 AND CH4 PWM1:
 * logical CH3 pulse lies between CCR3=D and CCR4=D+ARR/2.
 * PA8(CH1) / PA10(CH3) are the two bridge-leg logic waveforms.
 * Current power-stage gate PC6 is NEVER manipulated by this math.
 */
typedef struct {
    uint16_t arr,ccr1,ccr3,ccr4;
    uint16_t angle_deg;
} ConvPhaseConfig;
/* 0<=angle<=180; clamps larger requests. Returns 0 only if invalid ARR. */
uint8_t ConvPhase_Calculate(uint16_t arr,uint16_t angle_deg,
                             ConvPhaseConfig *out);
#endif
