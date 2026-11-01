/* IH01A PowerControl v1: observe-only control ISR and fixed-point model.
 *
 * TIM6 TRGO (20kHz nominal) triggers ADC1 8-rank sequence, DMA1 Channel2
 * in circular mode holds two 8-channel frames. DMA HT and TC are complete
 * frames. TIM1 is independently clocked and NOT started by this module.
 *
 * IMPORTANT: no PWM enable, no TIM1 CCR/ARR/BDTR output enable writes.
 * Phase/frequency/burst are abstract Q15 commands, not power hardware drivers.
 * Calibration/protection/bridge mapping must be verified before enabling power.
 */
#include "power_control.h"
#include "main.h"
#include "adc.h"
#include "tim.h"

#define PC_RAMFUNC __attribute__((section(".ih_ramfunc"), used, noinline))
#define PC_Q15_MAX 32767u
#define PC_RAMP_Q15_PER_FRAME 32u

/* Each frame has eight 12-bit samples, as configured by CubeMX. */
static volatile uint16_t s_dma_samples[PC_DMA_WORDS] __attribute__((aligned(4)));
static volatile PC_Snapshot s_snapshot;
static int32_t s_filter_q4[PC_ADC_CHANNELS];
static uint8_t s_burst_position;
static uint8_t s_started;

static uint32_t pc_enter_critical(void)
{
    uint32_t was_masked = __get_PRIMASK();
    __disable_irq();
    return was_masked;
}
static void pc_leave_critical(uint32_t was_masked)
{
    if((was_masked & 1u) == 0u) __enable_irq();
}

/* CMSIS STM32G071 advertises __VTOR_PRESENT=1; startup RESET remains in Flash.
 * The scatter file reserves a 256-byte, 256-aligned NOINIT SRAM vector image.
 * Copy the entire 256-byte window (unused slots are harmless), then use VTOR.
 */
static uint32_t s_vectors[64] __attribute__((section(".ih_vectors"), aligned(256), used));
extern const uint32_t __Vectors[];

void PowerControl_RelocateVectors(void)
{
    uint32_t mask, i;
    mask = pc_enter_critical();
    for(i = 0u; i < 64u; ++i)
        s_vectors[i] = __Vectors[i];
    __DSB();
    SCB->VTOR = (uint32_t)s_vectors;
    __DSB();
    __ISB();
    pc_leave_critical(mask);
}

static void pc_force_safe(void)
{
    /* Hardware output gate and TIM1 main output always remain disabled.
     * Do not start TIM1/PWM from an observational control port. */
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port, PWM_EN_Pin);
    TIM1->BDTR &= ~TIM_BDTR_MOE;
}

void PowerControl_Init(void)
{
    uint32_t i;
    pc_force_safe();
    /* Fast PF1 edges allow precise measurement of the ISR active pulse. */
    LL_GPIO_SetPinSpeed(ISR_TIME_GPIO_Port, ISR_TIME_Pin,
                        LL_GPIO_SPEED_FREQ_VERY_HIGH);
    s_started = 0u;
    s_burst_position = 0u;
    for(i = 0u; i < PC_DMA_WORDS; ++i) s_dma_samples[i] = 0u;
    for(i = 0u; i < PC_ADC_CHANNELS; ++i)
    {
        s_snapshot.raw[i] = 0u;
        s_snapshot.filtered[i] = 0u;
        s_filter_q4[i] = 0;
    }
    s_snapshot.frames = 0u;
    s_snapshot.errors = 0u;
    s_snapshot.requested_q15 = 0u;
    s_snapshot.applied_q15 = 0u;
    s_snapshot.phase_q15 = 0u;
    s_snapshot.frequency_q15 = 0u;
    s_snapshot.burst_gate = 0u;
    s_snapshot.mode = PC_MOD_PHASE;
    s_snapshot.state = PC_STATE_STOP;

    /* HAL_ADC_Start_DMA enables HT and TC IRQ for the 16 halfword buffer.
     * TIM6 starts last, so there is no DMA event before all init completes. */
    if(HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK ||
       HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)s_dma_samples,
                         PC_DMA_WORDS) != HAL_OK)
    {
        PowerControl_DmaFaultISR();
        return;
    }
    s_snapshot.state = PC_STATE_MONITOR;
    s_started = 1u;
    if(HAL_TIM_Base_Start(&htim6) != HAL_OK)
    {
        PowerControl_DmaFaultISR();
        s_started = 0u;
    }
}

/* This is an intentional command model, not a closed-loop heater enable.
 * It can be called from UI/main context without tearing the two fields. */
void PowerControl_SetRequest(PC_Modulation mode, uint16_t q15)
{
    uint32_t mask;
    if(mode > PC_MOD_BURST) return;
    if(q15 > PC_Q15_MAX) q15 = PC_Q15_MAX;
    mask = pc_enter_critical();
    s_snapshot.mode = (uint8_t)mode;
    s_snapshot.requested_q15 = q15;
    pc_leave_critical(mask);
}

PC_RAMFUNC void PowerControl_DmaFaultISR(void)
{
    s_snapshot.errors++;
    s_snapshot.state = PC_STATE_FAULT;
    s_snapshot.applied_q15 = 0u;
    s_snapshot.phase_q15 = 0u;
    s_snapshot.frequency_q15 = 0u;
    s_snapshot.burst_gate = 0u;
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port, PWM_EN_Pin);
    TIM1->BDTR &= ~TIM_BDTR_MOE;
}

/* Hot path lives in SRAM. Integer Q4 IIR filtering and Q15 ramp only.
 * No float, no dynamic allocation, no HAL access and no synchronous waits. */
PC_RAMFUNC void PowerControl_DmaFrameISR(const volatile uint16_t *samples)
{
    uint8_t i;
    uint16_t goal, applied;
    if(!s_started || s_snapshot.state != PC_STATE_MONITOR) return;
    for(i = 0u; i < PC_ADC_CHANNELS; ++i)
    {
        int32_t x = (int32_t)(samples[i] & 0x0FFFu) << 4u;
        int32_t f = s_filter_q4[i];
        f += (x - f) >> 3;  /* 1/8 first-order low-pass, no fixed dt assumption */
        s_filter_q4[i] = f;
        s_snapshot.raw[i] = (uint16_t)(x >> 4u);
        s_snapshot.filtered[i] = (uint16_t)((f + 8) >> 4u);
    }
    ++s_snapshot.frames;

    goal = s_snapshot.requested_q15;
    applied = s_snapshot.applied_q15;
    if(goal > applied)
    {
        uint16_t diff = goal - applied;
        applied += (diff > PC_RAMP_Q15_PER_FRAME) ? PC_RAMP_Q15_PER_FRAME : diff;
    }
    else if(goal < applied)
    {
        uint16_t diff = applied - goal;
        applied -= (diff > PC_RAMP_Q15_PER_FRAME) ? PC_RAMP_Q15_PER_FRAME : diff;
    }
    s_snapshot.applied_q15 = applied;

    /* Separate asynchronous command representations. No TIM1 register writes:
     * without the IH01A bridge polarity / deadtime map it would be unsafe. */
    switch(s_snapshot.mode)
    {
    case PC_MOD_PHASE:
        s_snapshot.phase_q15 = applied;
        s_snapshot.frequency_q15 = 0u;
        s_snapshot.burst_gate = 0u;
        break;
    case PC_MOD_FREQUENCY:
        s_snapshot.phase_q15 = 0u;
        s_snapshot.frequency_q15 = applied;
        s_snapshot.burst_gate = 0u;
        break;
    case PC_MOD_BURST:
        s_snapshot.phase_q15 = 0u;
        s_snapshot.frequency_q15 = 0u;
        /* 256 observation slots per window, independent of TIM1 periods.
         * (Q15_MAX+1)>>7 == 256, zero stays zero. */
        s_snapshot.burst_gate =
            (s_burst_position < ((uint32_t)(applied + 1u) >> 7u)) ? 1u : 0u;
        break;
    default:
        PowerControl_DmaFaultISR();
        break;
    }
    ++s_burst_position;
}

void PowerControl_GetSnapshot(PC_Snapshot *out)
{
    uint8_t i;
    uint32_t mask;
    if(out == 0) return;
    mask = pc_enter_critical();
    for(i = 0u; i < PC_ADC_CHANNELS; ++i)
    {
        out->raw[i] = s_snapshot.raw[i];
        out->filtered[i] = s_snapshot.filtered[i];
    }
    out->frames = s_snapshot.frames;
    out->errors = s_snapshot.errors;
    out->requested_q15 = s_snapshot.requested_q15;
    out->applied_q15 = s_snapshot.applied_q15;
    out->phase_q15 = s_snapshot.phase_q15;
    out->frequency_q15 = s_snapshot.frequency_q15;
    out->burst_gate = s_snapshot.burst_gate;
    out->state = s_snapshot.state;
    out->mode = s_snapshot.mode;
    pc_leave_critical(mask);
}

uint16_t PowerControl_AdcPinMillivolts(uint16_t raw)
{
    if(raw > PC_ADC_MAX_CODE) raw = PC_ADC_MAX_CODE;
    return (uint16_t)(((uint32_t)raw * PC_ADC_REFERENCE_MV
                     + PC_ADC_MAX_CODE / 2u) / PC_ADC_MAX_CODE);
}

uint16_t PowerControl_IsrLoadPermille(uint16_t raw)
{
    /* ISR_TIME is 3.3V logic, ADC reference is only 2.5V.
     * Thus the ISR_LOAD ADC saturates at ~757.6 permille duty.
     * This is a diagnostic, NOT a full-scale utilization measurement. */
    uint32_t mv = PowerControl_AdcPinMillivolts(raw);
    return (uint16_t)((mv * 1000u + PC_ISR_TIME_HIGH_MV / 2u)
                      / PC_ISR_TIME_HIGH_MV);
}

/* Strong RAM-resident handler replaces Core/Src generated ADC DMA handler.
 * Do not call HAL_DMA_IRQHandler: HT is one entire 8-rank frame, as is TC.
 * Channel 3 is currently unused; reserve this shared vector if it changes.
 */
PC_RAMFUNC void DMA1_Channel2_3_IRQHandler(void)
{
    uint32_t flags;
    LL_GPIO_SetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
    flags = DMA1->ISR;
    /* ADC overrun means a TIM6 trigger arrived before the previous
     * 8-channel sequence was safely consumed: latch a fault. */
    if((ADC1->ISR & ADC_ISR_OVR) != 0u)
    {
        ADC1->ISR = ADC_ISR_OVR;
        DMA1->IFCR = DMA_IFCR_CGIF2;
        PowerControl_DmaFaultISR();
    }
    else if((flags & DMA_ISR_TEIF2) != 0u)
    {
        DMA1->IFCR = DMA_IFCR_CGIF2;
        PowerControl_DmaFaultISR();
    }
    else if((flags & (DMA_ISR_HTIF2 | DMA_ISR_TCIF2)) != 0u)
    {
        DMA1->IFCR = DMA_IFCR_CHTIF2 | DMA_IFCR_CTCIF2;
        if((flags & (DMA_ISR_HTIF2 | DMA_ISR_TCIF2)) ==
           (DMA_ISR_HTIF2 | DMA_ISR_TCIF2))
        {
            /* Two unserviced frames means we missed a DMA deadline. */
            PowerControl_DmaFaultISR();
        }
        else if((flags & DMA_ISR_HTIF2) != 0u)
        {
            PowerControl_DmaFrameISR(&s_dma_samples[0]);
        }
        else
        {
            PowerControl_DmaFrameISR(&s_dma_samples[PC_ADC_CHANNELS]);
        }
    }
    LL_GPIO_ResetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
}
