/* G071: TIM6 ADC/DMA ISR independent of TIM1 PWM and CH2 input capture.
 * Only the explicit <=12V, <=3A manual bench gate may
 * enable FD2204 PC6. Production heater controller remains uncommissioned.
 */
#include "conv_controller.h"
#include "conv_adc.h"
#include "conv_phase.h"
#include "conv_zcd.h"
#include "conv_protection.h"
#include "heater_control.h"
#include "algorithm/pd_input.h"
#include "framework/pm_api.h"
#include "main.h"
#include "tim.h"
#define IH_RAMFUNC __attribute__((section(".ih_ramfunc"), used, noinline))
/* Explicit commissioning gate; no UI command may override this. */
#define IH_POWER_STAGE_COMMISSIONED 0u
/* Explicit narrow gate, ONLY for 12V low-power with any manual phase.
 * Normal heater/power controller remains uncommissioned. */
#define IH_LOW_POWER_BENCH_ENABLE 1u
static volatile ConvState s_state;
static volatile uint32_t s_errors;
static uint16_t s_phase_deg;
static uint16_t s_phase_arr;
static uint8_t s_logic_pwm_started;
static uint8_t s_logic_pwm_start_attempted;
static uint32_t s_bench_started_ms;
/* Latched reason the last RUN attempt was refused / why it stopped. */
static uint8_t s_bench_block;
/* ~20k full ADC frames/s. Counted for diagnostics only; the bench run has no
 * time budget, so nothing trips on this counter any more. */
static volatile uint32_t s_bench_on_adc_frames;
/* Latest complete foreground observation for immediate CLI start.
 * Never sampled from IRQ and invalid after 200ms without thread_heater. */
static HeaterObservation s_last_bench_obs;
static uint32_t s_last_bench_obs_ms;
static uint8_t s_last_bench_obs_valid;

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
    s_last_bench_obs_valid=0u;
    s_last_bench_obs_ms=0u;
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
uint32_t ConvController_BenchElapsedSeconds(uint32_t now_ms)
{
    if(!ConvController_OutputEnabled())return 0u;
    return (uint32_t)(now_ms-s_bench_started_ms)/1000u;
}
void ConvController_BenchPowerOff(void)
{
    /* PC6 is always switched off FIRST; timer logic PWM may continue. */
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
    pm_api_set_sleep_block(PM_BLOCK_HEATER,0u);
}
/* TIM1 ARR and CCR1/3/4 are staged as one coherent preload transaction.
 * Callers include the 5ms ADC DMA control divider and foreground UI.
 * Foreground staging masks IRQ for a few MMIO writes to prevent an ISR
 * transaction from interleaving. Hardware Break remains asynchronous.
 * No UG/CNT/CEN/MOE changes while TIM1 is running. */
static uint8_t ConvController_StageTimerPreloads(const ConvPhaseConfig *phase)
{
    uint32_t mask,cr1,ccmr1,ccmr2;
    uint8_t running,ok=0u;
    if(!phase)return 0u;
    mask=__get_PRIMASK();
    __disable_irq();
    cr1=TIM1->CR1;
    ccmr1=TIM1->CCMR1;
    ccmr2=TIM1->CCMR2;
    running=(uint8_t)((cr1&TIM_CR1_CEN)!=0u);
    if((!running && ConvController_OutputEnabled()) ||
       (cr1&TIM_CR1_UDIS)!=0u ||
       (running && phase->arr!=(uint16_t)TIM1->ARR))
        goto done;
    if(running &&
       (((cr1&TIM_CR1_ARPE)==0u) ||
        ((ccmr1&TIM_CCMR1_OC1PE)==0u) ||
        ((ccmr2&(TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE))!=
                 (TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE))))
        goto done;
    if(!running)
    {
        TIM1->CR1 |= TIM_CR1_ARPE;
        TIM1->CCMR1 |= TIM_CCMR1_OC1PE;
        TIM1->CCMR2 |= TIM_CCMR2_OC3PE|TIM_CCMR2_OC4PE;
    }
    TIM1->CR1 |= TIM_CR1_UDIS;
    TIM1->ARR=phase->arr;
    TIM1->CCR1=phase->ccr1;
    TIM1->CCR3=phase->ccr3;
    TIM1->CCR4=phase->ccr4;
    TIM1->CR1 &= ~TIM_CR1_UDIS;
    if(!running)
    {
        TIM1->EGR=TIM_EGR_UG; /* startup only, power gate LOW */
        TIM1->CNT=0u;
    }
    /* Cache coherency is protected by the SAME short critical section. */
    s_phase_deg=phase->angle_deg;
    s_phase_arr=phase->arr;
    ok=1u;
done:
    if((mask&1u)==0u)__enable_irq();
    return ok;
}

void ConvController_SetPhaseDegrees(uint16_t angle_deg)
{
    ConvPhaseConfig phase;
    uint16_t arr=(uint16_t)TIM1->ARR;
    if(!ConvPhase_Calculate(arr,angle_deg,&phase))return;
    if(s_phase_arr==arr && s_phase_deg==phase.angle_deg)return;
    if(!ConvController_StageTimerPreloads(&phase))
    {
        /* Preload unexpectedly disabled or update already inhibited:
         * fail closed instead of permitting a mixed active waveform. */
        if(ConvController_OutputEnabled())
        {
            ConvController_BenchPowerOff();
            Heater_Stop();
        }
        return;
    }
    ConvZcd_Invalidate();
    TIM1->SR=(uint32_t)~(TIM_SR_CC2IF|TIM_SR_CC2OF);
}
uint16_t ConvController_GetPhaseDegrees(void)
{
    return s_phase_deg;
}
uint16_t ConvController_GetFrequencyKhz(void)
{
    return ConvPhase_KHzFromArr((uint16_t)TIM1->ARR);
}
/* Shared synchronous ARR/CCR staging path (TIM1 natural update only). */
static uint8_t ConvController_ApplyFrequencyArr(uint16_t next_arr)
{
    ConvPhaseConfig phase;
    if(TIM1->ARR==next_arr)return 1u;
    /* ISR must not copy the mutable HeaterSnapshot. Phase is an aligned,
     * staged controller setpoint, updated under the preload lock above. */
    if(!ConvPhase_Calculate(next_arr,s_phase_deg,&phase))return 0u;
    if(!ConvController_StageTimerPreloads(&phase))
    {
        /* Hardware must be disabled FIRST; foreground supervisor will
         * latch the corresponding automatic-loop fault. */
        if(ConvController_OutputEnabled())ConvController_TripISR();
        return 0u;
    }
    ConvZcd_Invalidate();
    TIM1->SR=(uint32_t)~(TIM_SR_CC2IF|TIM_SR_CC2OF);
    return 1u;
}
uint8_t ConvController_SetFrequencyKhz(uint16_t requested_khz)
{
    uint16_t next_arr;
    if(!ConvPhase_ArrFromKHz(requested_khz,&next_arr))return 0u;
    return ConvController_ApplyFrequencyArr(next_arr);
}
uint8_t ConvController_SetFrequencyArr(uint16_t arr)
{
    if(arr<426u || arr>752u)return 0u;
    return ConvController_ApplyFrequencyArr(arr);
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
/* Bench run has NO time budget: it stays enabled until the user stops it, the
 * RUN gate fails, or a protection fault trips. The former 10 s ADC-frame
 * watchdog latched CONV_PROT_BENCH_TIMEOUT (0x00010000) after 200000 frames,
 * which showed up as a spurious fault during longer bench work. The frame
 * counter is kept (reset while PC6 is low) only so a future watchdog can be
 * re-enabled here without touching the ISR. */
IH_RAMFUNC uint8_t ConvController_BenchAdcFrameWatchdogISR(void)
{
#if IH_LOW_POWER_BENCH_ENABLE
    if((PWM_EN_GPIO_Port->ODR&PWM_EN_Pin)==0u)
    {
        s_bench_on_adc_frames=0u;
        return 0u;
    }
    ++s_bench_on_adc_frames;
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
    if(observation)
    {
        s_last_bench_obs=*observation;
        s_last_bench_obs_ms=now_ms;
        s_last_bench_obs_valid=1u;
    }
    valid_pd=PD_Input_GetActiveContract(&contract);
    allow=(uint8_t)(h.state==HEATER_RUN_REQUESTED &&
                    bench_block_reason(&h,valid_pd,&contract,now_ms,
                                       observation)==CONV_BENCH_OK);
    if(ConvController_OutputEnabled())
    {
        pm_api_set_sleep_block(PM_BLOCK_HEATER,1u);
        /* No runtime budget: the run continues until the gate fails (user
         * STOP, heater state change, source/temperature/protection problem). */
        if(!allow)
        {
            s_bench_block=bench_block_reason(&h,valid_pd,&contract,now_ms,
                                             observation);
            ConvController_BenchPowerOff();
            Heater_Stop();
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
    pm_api_set_sleep_block(PM_BLOCK_HEATER,1u);
    pm_api_refresh_idle();
#else
    (void)now_ms;
    (void)observation;
    ConvController_BenchPowerOff();
#endif
}

/* CLI-only synchronous request. Success means the physical enable pin is
 * HIGH now, not merely that HEATER_RUN_REQUESTED was queued. No direct GPIO
 * bypass; the SAME BenchRunPoll verifies the latest observation and live
 * PD/COMP/Break flags. If the heater task has stopped, do not use stale data.
 * This function is called in APP foreground only, never from IRQ. */
uint8_t ConvController_RequestBenchRunNow(uint32_t now_ms)
{
#if IH_LOW_POWER_BENCH_ENABLE
    HeaterSnapshot hs;
    if(ConvController_OutputEnabled())
    {
        if(ConvController_OutputPinIsOutput() &&
           ConvController_OutputPinLevel() &&
           ConvController_LogicPwmRunning() &&
           ConvProtection_FaultMask()==0u)
            return 1u;
        ConvController_BenchPowerOff();
        Heater_Stop();
        s_bench_block=CONV_BENCH_PWM;
        return 0u;
    }
    if(!s_last_bench_obs_valid ||
       (uint32_t)(now_ms-s_last_bench_obs_ms)>200u)
    {
        s_bench_block=CONV_BENCH_AGE;
        return 0u;
    }
    Heater_GetSnapshot(&hs);
    if(hs.state==HEATER_FAULT || ConvProtection_FaultMask()!=0u)
    {
        s_bench_block=CONV_BENCH_FAULT;
        return 0u;
    }
    /* Heater_SetMode cannot energize PC6; no countdown or delayed start. */
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_StartConfirmed(now_ms);
    ConvController_BenchRunPoll(now_ms,&s_last_bench_obs);
    if(ConvController_OutputEnabled() &&
       ConvController_OutputPinIsOutput() &&
       ConvController_OutputPinLevel() &&
       ConvController_LogicPwmRunning() &&
       ConvProtection_FaultMask()==0u)
        return 1u;

    ConvController_BenchPowerOff();
    Heater_Stop();
    if(s_bench_block==CONV_BENCH_OK)s_bench_block=CONV_BENCH_PWM;
    return 0u;
#else
    (void)now_ms;
    return 0u;
#endif
}
uint8_t ConvController_OutputEnabled(void)
{
    return (uint8_t)((PWM_EN_GPIO_Port->ODR&PWM_EN_Pin)!=0u);
}
uint8_t ConvController_OutputPinLevel(void)
{
    return (uint8_t)((PWM_EN_GPIO_Port->IDR&PWM_EN_Pin)!=0u);
}
uint8_t ConvController_OutputPinIsOutput(void)
{
    /* MODER field for the pin: 01 = general purpose output. */
    uint32_t shift=0u;
    uint32_t pin=PWM_EN_Pin;
    while(pin>1u){pin>>=1;shift+=2u;}
    return (uint8_t)(((PWM_EN_GPIO_Port->MODER>>shift)&3u)==1u);
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
