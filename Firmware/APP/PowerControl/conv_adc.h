#ifndef IH01A_CONV_ADC_H
#define IH01A_CONV_ADC_H
#include <stdint.h>
typedef enum {
    CONV_ADC_CT_RMS=0, CONV_ADC_IBUS, CONV_ADC_NTC_POT,
    CONV_ADC_VBUS, CONV_ADC_NTC_SYS, CONV_ADC_VBIAS,
    CONV_ADC_ISR_LOAD, CONV_ADC_MCU_TEMP, CONV_ADC_COUNT
} ConvADC_Channel;
#define CONV_ADC_VREF_MV 2500u
#define CONV_ADC_DMA_LENGTH (2u*CONV_ADC_COUNT)
typedef struct {
    uint16_t raw[CONV_ADC_COUNT];
    uint16_t filtered[CONV_ADC_COUNT];
    uint32_t frames;
} ConvADC_Snapshot;
void ConvADC_Reset(void);
uint8_t ConvADC_Start(void);
const volatile uint16_t *ConvADC_DmaFrame(uint8_t second_half);
void ConvADC_ProcessFrameISR(const volatile uint16_t *frame);
void ConvADC_CopySnapshot(ConvADC_Snapshot *out);
uint16_t ConvADC_PinMillivolts(uint16_t raw);
uint8_t ConvADC_TempFromCalibration(uint16_t raw, uint16_t cal1,
                                    uint16_t cal2, int32_t *out_deci_c);
uint8_t ConvADC_McuTempDeciC(uint16_t raw, int32_t *out_deci_c);
#endif
