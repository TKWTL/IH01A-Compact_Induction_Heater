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
/* 12V low-power bench experiment: gated PC6, any phase, <=10s per RUN.
 * Normal power stage commissioning gate remains disabled. */
void ConvController_BenchPowerOff(void);
uint8_t ConvController_BenchSecondsLeft(uint32_t now_ms);
/* Why the last bench RUN was refused (or why it stopped). The gate below is a
 * long AND-list; without this code a refused RUN is indistinguishable from a
 * lost key press on the OLED. Latched until a RUN is actually accepted. */
typedef enum {
    CONV_BENCH_OK=0,   /* no block; RUN accepted (or never attempted)      */
    CONV_BENCH_MODE,   /* not in POWER mode                               */
    CONV_BENCH_PHASE,  /* requested angle not yet applied to TIM1         */
    CONV_BENCH_NOPD,   /* no confirmed explicit PD contract                */
    CONV_BENCH_VOLT,   /* contract is not 12 V                            */
    CONV_BENCH_CURR,   /* contract current zero or above 3 A              */
    CONV_BENCH_SOURCE, /* input is not the PD port                        */
    CONV_BENCH_BUS,    /* measured VBUS outside 10.8..13.2 V              */
    CONV_BENCH_ZERO,   /* ADC zero average not settled yet                */
    CONV_BENCH_NTC,    /* pot or system NTC not valid                     */
    CONV_BENCH_HOT,    /* pot >=70 C or system >=60 C                     */
    CONV_BENCH_AGE,    /* temperature sample older than 250 ms            */
    CONV_BENCH_PWM,    /* TIM1 logic PWM not running                      */
    CONV_BENCH_COMP,   /* comparators not armed / already faulted         */
    CONV_BENCH_FAULT,  /* protection fault mask not clear                 */
    CONV_BENCH_BRK,    /* TIM1 Break flag latched                         */
    CONV_BENCH_TIME    /* run ended because the 10 s budget expired       */
} ConvBenchBlock;
uint8_t ConvController_BenchBlockReason(void);
void ConvController_BenchRunPoll(uint32_t now_ms,
                                 const HeaterObservation *observation);
uint8_t ConvController_BenchAdcFrameWatchdogISR(void);
#endif
