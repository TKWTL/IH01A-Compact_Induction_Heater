#ifndef IH01A_POWER_CONTROL_H
#define IH01A_POWER_CONTROL_H

#include <stdint.h>

/* Physical ADC1 order from Core/Src/adc.c and .ioc. */
typedef enum {
    PC_ADC_CT_RMS = 0,
    PC_ADC_IBUS,
    PC_ADC_NTC_POT,
    PC_ADC_VBUS,
    PC_ADC_NTC_SYS,
    PC_ADC_VBIAS,
    PC_ADC_ISR_LOAD,
    PC_ADC_MCU_TEMP,
    PC_ADC_CHANNELS
} PC_AdcIndex;

typedef enum {
    PC_STATE_STOP = 0,
    PC_STATE_MONITOR,
    PC_STATE_FAULT
} PC_State;

typedef enum {
    PC_MOD_PHASE = 0,
    PC_MOD_FREQUENCY,
    PC_MOD_BURST
} PC_Modulation;

/* No hardware output is permitted by this observational port. */
typedef struct {
    uint16_t raw[PC_ADC_CHANNELS];
    uint16_t filtered[PC_ADC_CHANNELS];
    uint32_t frames;
    uint32_t errors;
    uint16_t requested_q15;
    uint16_t applied_q15;
    uint16_t phase_q15;
    uint16_t frequency_q15;
    uint16_t burst_gate;
    uint8_t state;
    uint8_t mode;
} PC_Snapshot;

#define PC_ADC_REFERENCE_MV 2500u
#define PC_ISR_TIME_HIGH_MV 3300u
#define PC_ADC_MAX_CODE 4095u
#define PC_DMA_WORDS (2u * PC_ADC_CHANNELS)

void PowerControl_RelocateVectors(void);
void PowerControl_Init(void);
void PowerControl_SetRequest(PC_Modulation mode, uint16_t q15);
void PowerControl_GetSnapshot(PC_Snapshot *out);
uint16_t PowerControl_AdcPinMillivolts(uint16_t raw);
uint16_t PowerControl_IsrLoadPermille(uint16_t raw);

/* Used by the RAM-resident DMA1 Channel 2/3 interrupt handler. */
void PowerControl_DmaFrameISR(const volatile uint16_t *samples);
void PowerControl_DmaFaultISR(void);

#endif
