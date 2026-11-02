#ifndef IH01A_CONV_CONTROLLER_H
#define IH01A_CONV_CONTROLLER_H
#include <stdint.h>
typedef enum {
    CONV_STATE_STOP=0,
    CONV_STATE_ADC_MONITOR,
    CONV_STATE_FAULT
} ConvState;
void ConvController_Init(void);
ConvState ConvController_GetState(void);
uint32_t ConvController_ErrorCount(void);
void ConvController_FaultISR(void);
void ConvController_TripISR(void); /* PC6 low FIRST, optional TIM1 MOE clear */
/* Future PWM start entry: requires measured window HIGH, two COMP inputs,
 * cleared TIM1 Break and an independently commissioned power stage.
 * Current build remains locked OFF (IH_POWER_STAGE_COMMISSIONED=0). */
uint8_t ConvController_TryEnableOutput(void);
uint8_t ConvController_OutputEnabled(void);
/* Bench-only timer PWM: TIM1 CH1 and combined CH3/CH4 run with FD2204 PC6=0.
 * This must not be conflated with ConvController_TryEnableOutput(). */
void ConvController_SetPhaseDegrees(uint16_t phase_deg);
uint16_t ConvController_GetPhaseDegrees(void);
uint8_t ConvController_StartLogicPwm(void);
uint8_t ConvController_LogicPwmRunning(void);
#endif
