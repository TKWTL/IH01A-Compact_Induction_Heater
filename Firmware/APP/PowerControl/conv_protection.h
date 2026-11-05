#ifndef IH01A_CONV_PROTECTION_H
#define IH01A_CONV_PROTECTION_H
#include <stdint.h>
#include "conv_adc.h"
#include "temp_service.h"
/* Startup zero capture: TIM6->ADC DMA frame count (at 20kHz ~115ms).
 * Settling + average window must contain no switching activity. */
#define CONV_ZERO_SETTLE_FRAMES 256u
#define CONV_ZERO_AVG_FRAMES 2048u
#define CONV_ZERO_MAX_SPAN_RAW 160u
/* Raw rail monitors are not replacements for calibrated ampere thresholds. */
#define CONV_PROT_VBUS_RAIL_RAW 4070u
#define CONV_PROT_IBUS_RAIL_RAW 4090u
typedef enum {
    CONV_ZERO_WAIT=0,CONV_ZERO_COLLECT,CONV_ZERO_READY,CONV_ZERO_FAILED
} ConvZeroStatus;
enum {
    CONV_PROT_VBUS_RAIL=1u<<0,
    CONV_PROT_IBUS_RAIL=1u<<1,
    CONV_PROT_ZERO_UNSTABLE=1u<<2,
    CONV_PROT_NTC_POT=1u<<3,
    CONV_PROT_NTC_SYS=1u<<4,
    CONV_PROT_VBUS_LOW=1u<<5,
    CONV_PROT_ADC_STALE=1u<<6,
    CONV_PROT_ZERO_SWITCHING=1u<<7,
    CONV_PROT_IBUS_LIMIT=1u<<8,
    CONV_PROT_AWD_VBUS=1u<<9,
    CONV_PROT_AWD_IBUS=1u<<10,
    CONV_PROT_AWD_SYS=1u<<11,
    CONV_PROT_COMP_SETUP=1u<<12,
    CONV_PROT_DAC_RANGE=1u<<13,
    CONV_PROT_TIM1_BREAK=1u<<14,
    CONV_PROT_BENCH_VBUS=1u<<15,
    /* OBSOLETE (2026-11-02): the bench run has no time budget, so nothing sets
     * this bit any more. Value kept so an old logged mask still decodes. */
    CONV_PROT_BENCH_TIMEOUT=1u<<16,
    CONV_PROT_AUTO_FREQ=1u<<17 /* auto loop lost valid phase at high current */
};
typedef struct {
    ConvZeroStatus zero_status;
    uint16_t ct_zero_raw,ibus_zero_raw,zero_samples;
    uint16_t dac1_code,dac2_code;
    uint8_t dac_profile_ready,comp_armed,awd2_enabled;
    uint16_t awd2_low_code,awd2_high_code;
    uint32_t ibus_limit_ma;
    uint32_t fault_mask;
} ConvProtectionSnapshot;
void ConvProtection_Init(void); /* before ADC/DMA start */
void ConvProtection_FrameISR(const volatile uint16_t *frame);
void ConvProtection_Service(uint32_t now_ms,const ConvADC_Snapshot *adc,
                            const TempService_Snapshot *temp);
void ConvProtection_GetSnapshot(ConvProtectionSnapshot *out);
uint8_t ConvProtection_ZeroReady(void);
/* One raw 12-bit ADC count threshold around the auto-calibrated IBUS zero.
 * 0 disables this protection until the analog A/count scaling is measured. */
void ConvProtection_SetIbusTripDeltaRaw(uint16_t delta_raw);
/* Start AWD2 after zero calibration; source current allowance may change
 * after a new PD contract. 0 disables AWD2 while no input source is valid. */
void ConvProtection_SetIbusLimitMa(uint32_t current_ma);
/* Handle ADC1 AWD1/2/3 flags before the regular HAL ADC IRQ dispatcher. */
void ConvProtection_AnalogWatchdogISR(void);
/* Called on TIM1 BRK IRQ before HAL, disables PC6 and latches fault. */
void ConvProtection_BreakISR(void);
/* Latches AUTO_FREQ bit and disables the bridge immediately in DMA ISR. */
void ConvProtection_AutoFreqTripISR(void);
uint32_t ConvProtection_FaultMask(void);
/* Operator acknowledgment only. Active sensor faults and uncalibrated
 * startup/setup faults cannot be cleared by a RUN toggle. */
uint8_t ConvProtection_RearmableMask(uint32_t mask);
uint8_t ConvProtection_ManualRearm(uint32_t now_ms);
/* Compare COMP1/2 and PA6 while the old Break latch may still be set. */
uint8_t ConvProtection_AnalogInputsSafe(void);
/* COMP1 + COMP2 raw state, PA6 synthesized window output and TIM1 Break.
 * All must agree on "inside window" before any future PC6 HIGH. */
uint8_t ConvProtection_WindowSafe(void);
/* Comparator peripherals initialized, no instantaneous level polling. */
uint8_t ConvProtection_CompArmed(void);
/* Pure logic for host tests: lower comparator must be HIGH and upper LOW. */
uint8_t ConvProtection_WithinWindow(uint8_t above_low,uint8_t above_high);
/* COMP1/2 window uses revised 0.075V/A centered on VBIAS.
 * For ±12A peak, DAC window is ±1474 codes at VREF=2.5V.
 * Do not silently extend to ±16A; requalify hardware first.
 * DAC1 CH1 is lower, CH2 upper. Hardware starts only after valid zero
 * calibration and voltage bounds; TIM1 MOE stays prohibited. */
/* Optional future calibration override: DAC deltas are raw counts,
 * independent of TIM1 ARR and PWM frequency. */
void ConvProtection_SetDacProfile(uint16_t low_delta_raw,
                                  uint16_t high_delta_raw,
                                  uint8_t calibrated);
void ConvProtection_CalcDacWindow(uint16_t vbias_raw,uint16_t arr,
                                  uint16_t low_delta,uint16_t high_delta,
                                  uint16_t *dac1,uint16_t *dac2);
#ifndef CONV_PROTECTION_HOST_TEST
void ConvProtection_DrawFaultOverlay(void);
#endif
#endif
