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
    CONV_PROT_ZERO_SWITCHING=1u<<7
};
typedef struct {
    ConvZeroStatus zero_status;
    uint16_t ct_zero_raw,ibus_zero_raw,zero_samples;
    uint16_t dac1_code,dac2_code;
    uint8_t dac_profile_ready;
    uint32_t fault_mask;
} ConvProtectionSnapshot;
void ConvProtection_Init(void); /* before ADC/DMA start */
void ConvProtection_FrameISR(const volatile uint16_t *frame);
void ConvProtection_Service(uint32_t now_ms,const ConvADC_Snapshot *adc,
                            const TempService_Snapshot *temp);
void ConvProtection_GetSnapshot(ConvProtectionSnapshot *out);
uint8_t ConvProtection_ZeroReady(void);
uint32_t ConvProtection_FaultMask(void);
/* DAC codes are PROPOSALS only until analog transfer and COMP polarity are
 * verified. The driver does not arm DAC/COMP or modify TIM1 break routing. */
void ConvProtection_SetDacProfile(uint16_t ch1_delta_at_arr853,
                                  uint16_t ch2_delta_at_arr853,
                                  uint8_t calibrated);
void ConvProtection_CalcDacWindow(uint16_t vbias_raw,uint16_t arr,
                                  uint16_t low_delta,uint16_t high_delta,
                                  uint16_t *dac1,uint16_t *dac2);
#ifndef CONV_PROTECTION_HOST_TEST
void ConvProtection_DrawFaultOverlay(void);
#endif
#endif
