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
/* Actual PC6 pin level (IDR), not the commanded ODR bit. On a push-pull output
 * IDR mirrors the driven level, so OutputEnabled()==1 && OutputPinLevel()==0
 * means an external load holds the pin low. OutputPinIsOutput() reports the
 * GPIOC MODER setting so a lost output configuration is visible too. */
uint8_t ConvController_OutputPinLevel(void);
uint8_t ConvController_OutputPinIsOutput(void);
/* Bench-only timer PWM: TIM1 CH1 and combined CH3/CH4 run with FD2204 PC6=0.
 * This must not be conflated with ConvController_TryEnableOutput(). */
void ConvController_SetPhaseDegrees(uint16_t phase_deg);
uint16_t ConvController_GetPhaseDegrees(void);
/* Manual 150..400kHz, 1kHz CLI / 10kHz UI steps, only when auto is OFF.
 * ARR and CCR1/3/4 are staged with UDIS
 * and take effect simultaneously at the next natural TIM1 update event.
 * While timer is running, PC6/MOE/CEN/CNT are NOT changed; hardware Break
 * stays active. Missing preload bits fail closed (power gate OFF). */
uint8_t ConvController_SetFrequencyKhz(uint16_t requested_khz);
/* Auto loop uses exact ARR steps in 170..300kHz, same preload transaction. */
uint8_t ConvController_SetFrequencyArr(uint16_t arr);
uint16_t ConvController_GetFrequencyKhz(void);
uint8_t ConvController_StartLogicPwm(void);
uint8_t ConvController_LogicPwmRunning(void);
/* 12V low-power bench experiment: gated PC6, any phase, no time budget.
 * Normal power stage commissioning gate remains disabled. */
void ConvController_BenchPowerOff(void);
uint32_t ConvController_BenchElapsedSeconds(uint32_t now_ms);
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
    CONV_BENCH_BRK,    /* TIM1 Break flag latched                        */
    CONV_BENCH_COUNTDOWN /* Pan probe requires nonzero off timer         */
} ConvBenchBlock;
uint8_t ConvController_BenchBlockReason(void);
void ConvController_BenchRunPoll(uint32_t now_ms,
                                 const HeaterObservation *observation);
/* Synchronous CLI start, reports success only if PC6 is physically HIGH. */
uint8_t ConvController_RequestBenchRunNow(uint32_t now_ms);
uint8_t ConvController_BenchAdcFrameWatchdogISR(void);
/* Opt-in pan probe: 150kHz, 180deg, six periods every 2s while the
 * Heater run countdown is active. TIM1 CEN remains set after PC6 goes low.
 * CC2 DMA captures raw edge positions; this does NOT classify cookware. */
uint8_t ConvController_PanProbeBusy(void);
void ConvController_PanProbeService(uint32_t now_ms);
void ConvController_PanProbeUpdateISR(void);
#endif
