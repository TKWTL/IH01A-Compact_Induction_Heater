#ifndef IH01A_CONV_CONTROLLER_H
#define IH01A_CONV_CONTROLLER_H
#include <stdint.h>
#include "PowerControl/heater_control.h"
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
/* Future FD2204 power-stage enable: requires initialized comparators,
 * no latched TIM1 Break, and a commissioned output-control loop.
 * Instantaneous COMP low is handled by the hardware TIM1 Break input,
 * not by polling the window voltage level in software.
 * Current build remains locked OFF (IH_POWER_STAGE_COMMISSIONED=0). */
uint8_t ConvController_TryEnableOutput(void);
uint8_t ConvController_OutputEnabled(void);
/* Bench-only timer PWM: TIM1 CH1 and combined CH3/CH4 run with FD2204 PC6=0.
 * This must not be conflated with ConvController_TryEnableOutput(). */
void ConvController_SetPhaseDegrees(uint16_t phase_deg);
uint16_t ConvController_GetPhaseDegrees(void);
uint8_t ConvController_StartLogicPwm(void);
uint8_t ConvController_LogicPwmRunning(void);
/* 12V low-power bench experiment: gated PC6, <=20deg, <=10s per RUN.
 * Normal power stage commissioning gate remains disabled. */
void ConvController_BenchPowerOff(void);
uint8_t ConvController_BenchSecondsLeft(uint32_t now_ms);
void ConvController_BenchRunPoll(uint32_t now_ms,
                                 const HeaterObservation *observation);
uint8_t ConvController_BenchAdcFrameWatchdogISR(void);
#endif
