/* G071: TIM6 ADC/DMA ISR independent of TIM1 PWM and CH2 input capture.
 * Only the explicit <=12V, <=3A, <=10s manual bench gate may
 * enable FD2204 PC6. Production heater controller remains uncommissioned.
 */
#include "conv_controller.h"
#include "conv_adc.h"
#include "conv_phase.h"
#include "conv_zcd.h"
#include "conv_protection.h"
#include "heater_control.h"
#include "algorithm/pd_input.h"
#include "main.h"
#include "tim.h"
#define IH_RAMFUNC __attribute__((section(".ih_ramfunc"), used, noinline))
/* Explicit commissioning gate; no UI command may override this. */
#define IH_POWER_STAGE_COMMISSIONED 0u
/* Explicit narrow gate, ONLY for 12V low-power, any manual phase and 10s.
 * Normal heater/power controller remains uncommissioned. */
#define IH_LOW_POWER_BENCH_ENABLE 1u
#define IH_BENCH_MAX_RUNTIME_MS 10000u
static volatile ConvState s_state;
static volatile uint32_t s_errors;
static uint16_t s_phase_deg;
static uint16_t s_phase_arr;
static uint8_t s_logic_pwm_started;
static uint8_t s_logic_pwm_start_attempted;
static uint32_t s_bench_started_ms;
/* Latched reason the last RUN attempt was refused / why it stopped. */
static uint8_t s_bench_block;
/* ~20k full ADC frames/s. Hard deadline independent of PD superloop. */
static volatile uint32_t s_bench_on_adc_frames;

void ConvController_Init(void)
{
    s_state=CONV_STATE_STOP;
    s_errors=0u;
    s_logic_pwm_started=0u;
    s_logic_pwm_start_attempted=0u;
    s_phase_deg=0u;
    s_phase_arr=0u;
    s_bench_started_ms=0u;
    s_bench_on_adc_frames=0u;
    s_bench_block=CONV_BENCH_OK;
    ConvZcd_Init();
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
uint8_t ConvController_BenchSecondsLeft(uint32_t now_ms)
{
    uint32_t elapsed;
    if(!ConvController_OutputEnabled())return 0u;
    elapsed=(uint32_t)(now_ms-s_bench_started_ms);
    if(elapsed>=IH_BENCH_MAX_RUNTIME_MS)return 0u;
    return (uint8_t)((IH_BENCH_MAX_RUNTIME_MS-elapsed+999u)/1000u);
}
void ConvController_BenchPowerOff(void)
{
    /* Keep TIM1 CH1/CH3 logic PWM running for oscilloscope capture. */
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
}
void ConvController_SetPhaseDegrees(uint16_t angle_deg)
{
    ConvPhaseConfig phase;
    uint16_t arr=(uint16_t)TIM1->ARR;
    if(!ConvPhase_Calculate(arr,angle_deg,&phase))return;
    /* The phase angle is free 0..180 deg and no longer stops a running bench:
     * it is a scope/observability setting, not a power request. Stopping stays
     * with the RUN gate, the 10 s budget and the protection hardware. */
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
#if !IH_POWER_STAGE_COMMISSIONED
    /* Bench build invariant: no path, including a timer restart attempt,
     * is permitted to leave FD2204 EN asserted. */
    /* Keep the power gate asserted only while an explicitly authorized
     * bench run is active. Outside bench mode the old hard gate applies. */
#if IH_LOW_POWER_BENCH_ENABLE
    if(!ConvController_OutputEnabled())
#endif
        LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
#endif
    if(s_state!=CONV_STATE_ADC_MONITOR ||
       !ConvProtection_ZeroReady() ||
       ConvProtection_FaultMask()!=0u ||
       !ConvProtection_CompArmed())
        return 0u;
    if(s_logic_pwm_started)return ConvController_LogicPwmRunning();
    /* COMP initialization can assert Break while its DAC references
     * settle. With PC6 already LOW, permit exactly ONE startup attempt:
     * clear the pre-existing BIF and let active Break hardware immediately
     * reassert it if the window is still out-of-range. No automatic retry
     * after a real/comparator-induced Break during timer operation. */
    if(s_logic_pwm_start_attempted)return 0u;
    s_logic_pwm_start_attempted=1u;
    TIM1->SR=(uint32_t)~TIM_SR_BIF;
    /* Break is asynchronous in hardware. Its IRQ additionally pulls
     * FD2204 EN low, not just TIM1.MOE, at the first fault event. */
    HAL_NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn,0u,0u);
    HAL_NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    TIM1->DIER |= TIM_DIER_BIE;
    /* CH4's PWM1 compare is an INTERNAL constituent of combined CH3 output.
     * No CH4 pin is required. Both PWM reference sources are already
     * configured by MX_TIM1_Init() including OC3 Combined PWM2. */
    TIM1->CCMR1 |= TIM_CCMR1_OC1PE;
    TIM1->CCMR2 |= TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE;
    ConvZcd_StartCapture(); /* PA9 rising IC, no DMA or CC interrupt */
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
IH_RAMFUNC uint8_t ConvController_BenchAdcFrameWatchdogISR(void)
{
#if IH_LOW_POWER_BENCH_ENABLE
    if((PWM_EN_GPIO_Port->ODR&PWM_EN_Pin)==0u)
    {
        s_bench_on_adc_frames=0u;
        return 0u;
    }
    /* 200000 complete TIM6 frames ≈10s. This requires interrupts and ADC
     * DMA working; it is not an independent hardware watchdog. */
    if(++s_bench_on_adc_frames>=200000u)
    {
        ConvController_TripISR();
        return 1u;
    }
#endif
    return 0u;
}
uint8_t ConvController_BenchBlockReason(void)
{
    return s_bench_block;
}
/* Evaluate the RUN gate and remember WHICH clause refused it. Same conditions
 * as the original single AND-expression, in the same order, only expanded so
 * a refusal can be shown on the OLED instead of silently reverting to STOP. */
static uint8_t bench_block_reason(const HeaterSnapshot *h,
                                  uint8_t valid_pd,
                                  const PD_InputContract *c,
                                  uint32_t now_ms,
                                  const HeaterObservation *o)
{
    if(o==0)                              return CONV_BENCH_SOURCE;
    if(h->mode!=HEATER_MODE_POWER)        return CONV_BENCH_MODE;
    /* Any 0..180 deg phase is allowed: the angle is a manual scope setting,
     * not a watt request, so it no longer gates RUN. */
    if(ConvController_GetPhaseDegrees()!=h->target_phase_deg)
                                          return CONV_BENCH_PHASE;
    if(!valid_pd)                         return CONV_BENCH_NOPD;
    if(c->voltage_mv!=12000u)             return CONV_BENCH_VOLT;
    if(c->current_ma==0u || c->current_ma>3000u) return CONV_BENCH_CURR;
    if(o->input!=HEATER_INPUT_PD)         return CONV_BENCH_SOURCE;
    if(o->bus_mv<10800u || o->bus_mv>13200u) return CONV_BENCH_BUS;
    if(!o->zero_calibrated)               return CONV_BENCH_ZERO;
    if(!o->pot_valid || !o->sys_valid)    return CONV_BENCH_NTC;
    if(o->pot_deci_c>=700 || o->sys_deci_c>=600) return CONV_BENCH_HOT;
    if((uint32_t)(now_ms-o->temperature_updated_ms)>250u) return CONV_BENCH_AGE;
    if(!ConvController_LogicPwmRunning()) return CONV_BENCH_PWM;
    if(!ConvProtection_CompArmed())       return CONV_BENCH_COMP;
    if(ConvProtection_FaultMask()!=0u)    return CONV_BENCH_FAULT;
    if((TIM1->SR&TIM_SR_BIF)!=0u)         return CONV_BENCH_BRK;
    return CONV_BENCH_OK;
}
void ConvController_BenchRunPoll(uint32_t now_ms,
                                 const HeaterObservation *observation)
{
#if IH_LOW_POWER_BENCH_ENABLE
    HeaterSnapshot h;
    PD_InputContract contract;
    uint8_t valid_pd;
    uint8_t allow;
    Heater_GetSnapshot(&h);
    valid_pd=PD_Input_GetActiveContract(&contract);
    allow=(uint8_t)(h.state==HEATER_RUN_REQUESTED &&
                    bench_block_reason(&h,valid_pd,&contract,now_ms,
                                       observation)==CONV_BENCH_OK);
    if(ConvController_OutputEnabled())
    {
        if(!allow ||
           (uint32_t)(now_ms-s_bench_started_ms)>=IH_BENCH_MAX_RUNTIME_MS)
        {
            if(allow)s_bench_block=CONV_BENCH_TIME;
            else if(s_bench_block==CONV_BENCH_OK)
                s_bench_block=bench_block_reason(&h,valid_pd,&contract,now_ms,
                                                 observation);
            ConvController_BenchPowerOff();
            Heater_Stop(); /* never auto re-arm after a 10s or fault stop */
        }
        return;
    }
    if(h.state!=HEATER_RUN_REQUESTED)return;
    if(!allow)
    {
        s_bench_block=bench_block_reason(&h,valid_pd,&contract,now_ms,
                                         observation);
        Heater_Stop(); /* no deferred power-on once missing PD becomes valid */
        return;
    }
    /* ONE explicit RUN can assert the gate; time budget starts before
     * the GPIO state changes to prevent the ISR seeing a stale counter. */
    s_bench_on_adc_frames=0u;
    __DMB();
    LL_GPIO_SetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
    if((TIM1->SR&TIM_SR_BIF)!=0u ||
       ConvProtection_FaultMask()!=0u)
    {
        s_bench_block=((TIM1->SR&TIM_SR_BIF)!=0u)?CONV_BENCH_BRK:
                                                 CONV_BENCH_FAULT;
        ConvController_BenchPowerOff();
        Heater_Stop();
        return;
    }
    s_bench_block=CONV_BENCH_OK;
    s_bench_started_ms=now_ms;
#else
    (void)now_ms;
    (void)observation;
    ConvController_BenchPowerOff();
#endif
}
uint8_t ConvController_OutputEnabled(void)
{
    return (uint8_t)((PWM_EN_GPIO_Port->ODR&PWM_EN_Pin)!=0u);
}
uint8_t ConvController_TryEnableOutput(void)
{
#if IH_POWER_STAGE_COMMISSIONED
    /* Future power-stage start; remain disabled until validated.
     * No software sampling of the momentary COMP window output. Real
     * overcurrent protection is asynchronous TIM1 BRK hardware. */
    if(s_state!=CONV_STATE_ADC_MONITOR ||
       !ConvProtection_CompArmed() ||
       Heater_GetIsrTargetMw()==0u ||
       (TIM1->SR&TIM_SR_BIF)!=0u)
        return 0u;
    TIM1->BDTR |= TIM_BDTR_MOE;
    __DSB();
    if((TIM1->SR&TIM_SR_BIF)!=0u)
    {
        ConvController_TripISR();
        return 0u;
    }
    /* PC6 HIGH enables FD2204 only after all upstream gates. */
    LL_GPIO_SetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
    if((TIM1->SR&TIM_SR_BIF)!=0u ||
       ConvProtection_FaultMask()!=0u)
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
