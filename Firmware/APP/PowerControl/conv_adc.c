/* G071 TIM6 TRGO -> ADC1 8 ranks -> DMA1 CH2 circular dual frames.
 * Q4 IIR in RAM ISR; integer calibrated voltage/temperature in UI context.
 */
#include "conv_adc.h"
#include "adc.h"
#include "tim.h"
#include "stm32g0xx_ll_adc.h"
#define IH_RAMFUNC __attribute__((section(".ih_ramfunc"), used, noinline))
static volatile uint16_t s_dma[CONV_ADC_DMA_LENGTH] __attribute__((aligned(4)));
static volatile ConvADC_Snapshot s_adc;
static int32_t s_filter_q4[CONV_ADC_COUNT];

void ConvADC_Reset(void)
{
    unsigned i;
    for(i=0u;i<CONV_ADC_DMA_LENGTH;++i) s_dma[i]=0u;
    for(i=0u;i<CONV_ADC_COUNT;++i)
    {
        s_adc.raw[i]=0u;
        s_adc.filtered[i]=0u;
        s_filter_q4[i]=0;
    }
    s_adc.frames=0u;
}

uint8_t ConvADC_Start(void)
{
    if(HAL_ADCEx_Calibration_Start(&hadc1)!=HAL_OK) return 0u;
    if(HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)s_dma,
                         CONV_ADC_DMA_LENGTH)!=HAL_OK) return 0u;
    if(HAL_TIM_Base_Start(&htim6)!=HAL_OK) return 0u;
    return 1u;
}

IH_RAMFUNC const volatile uint16_t *ConvADC_DmaFrame(uint8_t second)
{
    return &s_dma[second ? CONV_ADC_COUNT : 0u];
}

IH_RAMFUNC void ConvADC_ProcessFrameISR(const volatile uint16_t *frame)
{
    unsigned i;
    for(i=0u;i<CONV_ADC_COUNT;++i)
    {
        int32_t sample = ((int32_t)frame[i] & 4095) << 4;
        int32_t filtered = s_filter_q4[i];
        filtered += (sample - filtered) / 8;
        s_filter_q4[i] = filtered;
        s_adc.raw[i] = (uint16_t)(sample >> 4);
        s_adc.filtered[i] = (uint16_t)((filtered + 8) >> 4);
    }
    ++s_adc.frames;
}

void ConvADC_CopySnapshot(ConvADC_Snapshot *out)
{
    uint32_t mask;
    unsigned i;
    if(out==0) return;
    mask=__get_PRIMASK();
    __disable_irq();
    for(i=0u;i<CONV_ADC_COUNT;++i)
    {
        out->raw[i]=s_adc.raw[i];
        out->filtered[i]=s_adc.filtered[i];
    }
    out->frames=s_adc.frames;
    if((mask & 1u)==0u) __enable_irq();
}

/* These are ADC-pin volts; external gains and offsets are not assumed. */
uint16_t ConvADC_PinMillivolts(uint16_t raw)
{
    if(raw>4095u) raw=4095u;
    return (uint16_t)(((uint32_t)raw*CONV_ADC_VREF_MV+2047u)/4095u);
}

/* ST G0 TS_CAL1: 30C; TS_CAL2: 130C; both were measured at VREF+=3V.
 * Convert the 2.5V-referenced raw reading into factory 3V code units.
 * Return tenths of a Celsius degree, integer-only, no heap or float. */
uint8_t ConvADC_TempFromCalibration(uint16_t raw, uint16_t cal1,
                                    uint16_t cal2, int32_t *out)
{
    int32_t calibrated;
    if(out==0 || cal1==0u || cal2==0u || cal1==0xffffu ||
       cal2==0xffffu || cal1==cal2) return 0u;
    if(raw>4095u) raw=4095u;
    calibrated=(int32_t)(((uint32_t)raw*CONV_ADC_VREF_MV+1500u)/3000u);
    *out=300+((calibrated-(int32_t)cal1)*1000)/
             ((int32_t)cal2-(int32_t)cal1);
    return 1u;
}
uint8_t ConvADC_McuTempDeciC(uint16_t raw, int32_t *out)
{
    return ConvADC_TempFromCalibration(raw, *TEMPSENSOR_CAL1_ADDR,
                                      *TEMPSENSOR_CAL2_ADDR, out);
}
