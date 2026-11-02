/* G071 observer: TIM6/ADC-DMA ISR decoupled from the TIM1 PWM domain.
 * Output always disabled in this stage, regardless of ADC readings.
 */
#include "conv_controller.h"
#include "conv_adc.h"
#include "conv_phase.h"
#include "conv_protection.h"
#include "heater_control.h"
#include "main.h"
#include "tim.h"
#define IH_RAMFUNC __attribute__((section(".ih_ramfunc"), used, noinline))
/* Explicit commissioning gate; no UI command may override this. */
#define IH_POWER_STAGE_COMMISSIONED 0u
static volatile ConvState s_state;
static volatile uint32_t s_errors;
static uint16_t s_phase_deg;
static uint16_t s_phase_arr;
static uint8_t s_logic_pwm_started;

void ConvController_Init(void)
{
    s_state=CONV_STATE_STOP;
    s_errors=0u;
    s_logic_pwm_started=0u;
    s_phase_deg=0u;
    s_phase_arr=0u;
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port, PWM_EN_Pin);
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    LL_GPIO_ResetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
    LL_GPIO_SetPinSpeed(ISR_TIME_GPIO_Port, ISR_TIME_Pin,
                        LL_GPIO_SPEED_FREQ_VERY_HIGH);
    /* Preload the 0deg waveform with PC6 low and MOE disabled. */
    ConvController_SetPhaseDegrees(0u);
    ConvADC_Reset();
    if(ConvADC_Start()!=0u) s_state=CONV_STATE_ADC_MONITOR;
    else ConvController_FaultISR();
}
ConvState ConvController_GetState(void) { return s_state; }
uint32_t ConvController_ErrorCount(void) { return s_errors; }

IH_RAMFUNC void ConvController_TripISR(void)
{
    /* FD2204 EN is the FINAL physical output gate. Shut it off FIRST.
     * TIM1 shift PWM could safely keep running with PC6 low; clearing
     * MOE here is an additional defensive measure, not the primary gate. */
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
    TIM1->BDTR &= ~TIM_BDTR_MOE;
}
void ConvController_SetPhaseDegrees(uint16_t angle_deg)
{
    ConvPhaseConfig phase;
    uint16_t arr=(uint16_t)TIM1->ARR;
    if(!ConvPhase_Calculate(arr,angle_deg,&phase))return;
    if(s_phase_arr==arr && s_phase_deg==phase.angle_deg)return;
    /* Enable compare preload BEFORE writing CH3/CH4. UDIS prevents
     * an intervening update event from loading a mixed phase pair.
     * At the next natural update both CCR3/4 become active together.
     * This sequence is for BENCH logic output with PC6 held LOW.
     * Future energized operation also needs bounded phase slew. */
    TIM1->CCMR1 |= TIM_CCMR1_OC1PE;
    TIM1->CCMR2 |= TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE;
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->CCR1=phase.ccr1;
    TIM1->CCR3=phase.ccr3;
    TIM1->CCR4=phase.ccr4;
    TIM1->CR1 &= ~TIM_CR1_UDIS;
    s_phase_deg=phase.angle_deg;
    s_phase_arr=arr;
}
uint16_t ConvController_GetPhaseDegrees(void)
{
    return s_phase_deg;
}
uint8_t ConvController_LogicPwmRunning(void)
{
    return (uint8_t)((TIM1->CR1&TIM_CR1_CEN)!=0u &&
       (TIM1->CCER&(TIM_CCER_CC1E|TIM_CCER_CC3E))==
                   (TIM_CCER_CC1E|TIM_CCER_CC3E) &&
       (TIM1->BDTR&TIM_BDTR_MOE)!=0u);
}
uint8_t ConvController_StartLogicPwm(void)
{
    /* Start only after startup ADC zero capture and valid COMP/DAC setup.
     * No polling for the COMP1 window level: its brief active-low excursions
     * are handled asynchronously by TIM1 BRK hardware. Never assert PC6.
     * This bench output is independent of Heater RUN/OFF/schedule state. */
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    if(s_state!=CONV_STATE_ADC_MONITOR ||
       !ConvProtection_ZeroReady() ||
       ConvProtection_FaultMask()!=0u ||
       !ConvProtection_CompArmed())
        return 0u;
    if((TIM1->SR&TIM_SR_BIF)!=0u)return 0u;
    if(s_logic_pwm_started)return ConvController_LogicPwmRunning();
    /* CH4's PWM1 compare is an INTERNAL constituent of combined CH3 output.
     * No CH4 pin is required. Both PWM reference sources are already
     * configured by MX_TIM1_Init() including OC3 Combined PWM2. */
    TIM1->CCMR1 |= TIM_CCMR1_OC1PE;
    TIM1->CCMR2 |= TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE;
    TIM1->CCER |= TIM_CCER_CC1E | TIM_CCER_CC3E;
    TIM1->EGR=TIM_EGR_UG;
    TIM1->CR1 |= TIM_CR1_CEN;
    TIM1->BDTR |= TIM_BDTR_MOE;
    __DSB();
    if((TIM1->SR&TIM_SR_BIF)!=0u || ConvProtection_FaultMask()!=0u)
    {
        ConvController_TripISR();
        return 0u;
    }
    s_logic_pwm_started=1u;
    return 1u;
}
uint8_t ConvController_OutputEnabled(void)
{
    return (uint8_t)((PWM_EN_GPIO_Port->ODR&PWM_EN_Pin)!=0u);
}
uint8_t ConvController_TryEnableOutput(void)
{
#if IH_POWER_STAGE_COMMISSIONED
    /* Future gated start sequence, not reached in the current firmware.
     * The logical comparator "HIGH" must be the synthesized *window*
     * output, not HAL_COMP_GetOutputLevel(COMP1) alone. */
    if(s_state!=CONV_STATE_ADC_MONITOR ||
       !ConvProtection_WindowSafe() ||
       Heater_GetIsrTargetMw()==0u ||
       (TIM1->SR&TIM_SR_BIF)!=0u)
        return 0u;
    TIM1->BDTR |= TIM_BDTR_MOE;
    __DSB();
    if(!ConvProtection_WindowSafe())
    {
        ConvController_TripISR();
        return 0u;
    }
    /* PC6 HIGH enables FD2204 only after every upstream check. */
    LL_GPIO_SetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
    if(!ConvProtection_WindowSafe())
    {
        ConvController_TripISR();
        return 0u;
    }
    return 1u;
#else
    /* The current build never asserts PC6 or enables TIM1 outputs. */
    return 0u;
#endif
}
IH_RAMFUNC void ConvController_FaultISR(void)
{
    ++s_errors;
    s_state=CONV_STATE_FAULT;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    ConvController_TripISR();
}

/* Sole DMA1 CH2/3 handler: HT/TC each reports a full 8-channel frame.
 * ISR_TIME pulse starts at IRQ entry and ends at IRQ exit.
 * Unlike the original G474 code, no PWM-period-coupled PI/PR loop runs.
 */
IH_RAMFUNC void DMA1_Channel2_3_IRQHandler(void)
{
    uint32_t flags;
    LL_GPIO_SetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
    flags=DMA1->ISR;
    if((ADC1->ISR & ADC_ISR_OVR)!=0u)
    {
        ADC1->ISR=ADC_ISR_OVR;
        DMA1->IFCR=DMA_IFCR_CGIF2;
        ConvController_FaultISR();
    }
    else if((flags & DMA_ISR_TEIF2)!=0u)
    {
        DMA1->IFCR=DMA_IFCR_CGIF2;
        ConvController_FaultISR();
    }
    else if((flags & (DMA_ISR_HTIF2|DMA_ISR_TCIF2))!=0u)
    {
        DMA1->IFCR=DMA_IFCR_CHTIF2|DMA_IFCR_CTCIF2;
        if(s_state==CONV_STATE_ADC_MONITOR)
        {
            if((flags & (DMA_ISR_HTIF2|DMA_ISR_TCIF2))==
               (DMA_ISR_HTIF2|DMA_ISR_TCIF2))
                ConvController_FaultISR();
            else
                ConvADC_ProcessFrameISR(
                  ConvADC_DmaFrame((flags & DMA_ISR_TCIF2)!=0u));
        }
    }
    LL_GPIO_ResetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
}
