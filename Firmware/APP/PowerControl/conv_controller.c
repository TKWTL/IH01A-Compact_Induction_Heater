/* G071: TIM6 ADC/DMA ISR independent of TIM1 PWM and CH2 input capture.
 * Explicit manual RUN with valid PD, NTC, ADC and hardware comparator status
 * may enable FD2204 PC6. Production closed-loop heater control is still
 * uncommissioned; a 28V PD contract never automatically starts heating.
 */
#include "conv_controller.h"
#include "conv_adc.h"
#include "conv_phase.h"
#include "conv_zcd.h"
#include "conv_auto_freq.h"
#include "conv_protection.h"
#include "heater_control.h"
#include "heater_budget.h"
#include "algorithm/pd_input.h"
#include "framework/pm_api.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#define IH_RAMFUNC __attribute__((section(".ih_ramfunc"), used, noinline))
/* Explicit commissioning gate; no UI command may override this. */
#define IH_POWER_STAGE_COMMISSIONED 0u
/* Deliberate manual bench gate only; no automatic power-stage enable on PD
 * contract change or EPR negotiation. Hardware fault interlocks still apply. */
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

/* Opt-in six-cycle pulse, then IC2 DMA while TIM1 keeps counting. */
#define PAN_PERIOD_MS 2000u
#define PAN_CAPTURE_MS 4u
#define PAN_TIMEOUT_MS 10u
#define PAN_FREQ_KHZ 150u
#define PAN_PHASE 180u
#define PAN_CYCLES 6u
#define PAN_DMA_LEN 48u
typedef enum { PAN_IDLE=0u,PAN_ARMED,PAN_ACTIVE,PAN_COLLECT,PAN_ABORTED } PanState;
static volatile PanState s_pan_state;
static volatile uint8_t s_pan_left,s_pan_adc_guard;
static volatile uint16_t s_pan_start_index,s_pan_off_index;
static volatile uint32_t s_pan_off_ms;
static volatile uint16_t s_pan_dma[PAN_DMA_LEN] __attribute__((aligned(4)));
static uint16_t s_pan_restore_arr,s_pan_restore_phase,s_pan_arr;
static uint32_t s_pan_due_ms,s_pan_armed_ms,s_pan_seq;
static uint8_t s_pan_session;
static char s_pan_log[360];
static void pan_cancel(void);

void ConvController_Init(void)
{
    s_state=CONV_STATE_STOP;
    s_errors=0u;
    s_logic_pwm_started=0u;
    s_logic_pwm_start_attempted=0u;
    s_phase_deg=180u;
    s_phase_arr=0u;
    s_bench_started_ms=0u;
    s_bench_on_adc_frames=0u;
    s_bench_block=CONV_BENCH_OK;
    s_last_bench_obs_valid=0u;
    s_last_bench_obs_ms=0u;
    s_pan_state=PAN_IDLE;
    s_pan_session=0u;
    s_pan_left=s_pan_adc_guard=0u;
    s_pan_start_index=s_pan_off_index=0u;
    s_pan_due_ms=s_pan_armed_ms=s_pan_off_ms=s_pan_seq=0u;
    ConvZcd_Init();
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port, PWM_EN_Pin);
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    LL_GPIO_ResetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
    LL_GPIO_SetPinSpeed(ISR_TIME_GPIO_Port, ISR_TIME_Pin,
                        LL_GPIO_SPEED_FREQ_VERY_HIGH);
    /* Default bridge phase to 180deg with PC6 LOW and MOE disabled. */
    ConvController_SetPhaseDegrees(180u);
    /* Fixed-frequency fallback when auto is OFF; never start at 200kHz. */
    if(!ConvController_SetFrequencyArr(426u))
    {
        ConvController_FaultISR();
        return;
    }
    ConvADC_Reset();
    if(ConvADC_Start()!=0u) s_state=CONV_STATE_ADC_MONITOR;
    else ConvController_FaultISR();
}
ConvState ConvController_GetState(void) { return s_state; }
uint32_t ConvController_ErrorCount(void) { return s_errors; }

IH_RAMFUNC void ConvController_TripISR(void)
{
    uint32_t mask=__get_PRIMASK();
    /* Atomic with a pending TIM1 update: prevent a fault from being
     * followed by a stale ISR that accidentally reasserts the gate. */
    __disable_irq();
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    __DSB();
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    if(s_pan_state!=PAN_IDLE)
    {
        s_pan_state=PAN_ABORTED;
        TIM1->DIER &= ~TIM_DIER_UIE;
    }
    __DSB();
    if((mask&1u)==0u)__enable_irq();
}
uint32_t ConvController_BenchElapsedSeconds(uint32_t now_ms)
{
    if(!ConvController_OutputEnabled())return 0u;
    return (uint32_t)(now_ms-s_bench_started_ms)/1000u;
}
void ConvController_BenchPowerOff(void)
{
    uint32_t mask=__get_PRIMASK();
    /* STOP must disable PC6 AND invalidate a pending pulse atomically.
     * Otherwise an UPDATE IRQ could reenable PC6 after the user stops. */
    __disable_irq();
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    if(s_pan_state!=PAN_IDLE)
    {
        s_pan_state=PAN_ABORTED;
        TIM1->DIER &= ~TIM_DIER_UIE;
    }
    __DSB();
    if((mask&1u)==0u)__enable_irq();
    pan_cancel(); /* DMA abort and preload restore, foreground only */
    pm_api_set_sleep_block(PM_BLOCK_HEATER,0u);
}
/* TIM1 ARR and CCR1/3/4 are staged as one coherent preload transaction.
 * Callers include the 5ms ADC DMA control divider and foreground UI.
 * Foreground staging masks IRQ for a few MMIO writes to prevent an ISR
 * transaction from interleaving. Hardware Break remains asynchronous.
 * No UG/CNT/CEN/MOE changes while TIM1 is running. */
static uint8_t ConvController_StageTimerPreloads(const ConvPhaseConfig *phase,
                                                  uint8_t allow_arr_change)
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
       (running && !allow_arr_change &&
        phase->arr!=(uint16_t)TIM1->ARR))
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
    uint32_t mask=__get_PRIMASK();
    uint16_t arr;
    uint8_t ok=1u;
    if(s_pan_state!=PAN_IDLE)return; /* defer external phase changes */
    /* Lock the ARR read AND subsequent staged write as one operation.
     * Without this, an ISR tick could change ARR after calculating
     * phase CCRs but before the foreground enters StageTimerPreloads. */
    __disable_irq();
    arr=(uint16_t)TIM1->ARR;
    if(!ConvPhase_Calculate(arr,angle_deg,&phase))
        ok=0u;
    else if(s_phase_arr!=arr || s_phase_deg!=phase.angle_deg)
        ok=ConvController_StageTimerPreloads(&phase,0u);
    if(ok)
    {
        ConvZcd_Invalidate();
        TIM1->SR=(uint32_t)~(TIM_SR_CC2IF|TIM_SR_CC2OF);
    }
    if((mask&1u)==0u)__enable_irq();
    if(!ok && ConvController_OutputEnabled())
    {
        ConvController_BenchPowerOff();
        Heater_Stop();
    }
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
    if(s_pan_state!=PAN_IDLE)return 0u;
    if(TIM1->ARR==next_arr)return 1u;
    /* ISR must not copy the mutable HeaterSnapshot. Phase is an aligned,
     * staged controller setpoint, updated under the preload lock above. */
    if(!(next_arr>=426u && next_arr<=752u ?
          ConvPhase_CalculateAuto(next_arr,s_phase_deg,&phase) :
          ConvPhase_Calculate(next_arr,s_phase_deg,&phase)))return 0u;
    if(!ConvController_StageTimerPreloads(&phase,1u))
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
    /* This public manual API cannot contend with the automatic ISR. */
    if(ConvAutoFreq_Enabled() || s_pan_state!=PAN_IDLE)return 0u;
    if(!ConvPhase_ArrFromKHz(requested_khz,&next_arr))return 0u;
    return ConvController_ApplyFrequencyArr(next_arr);
}
uint8_t ConvController_SetFrequencyArr(uint16_t arr)
{
    if(s_pan_state!=PAN_IDLE || arr<426u || arr>752u)return 0u;
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
/* Source and thermal preflight contains no historical fault or PWM-latch
 * checks. Manual RUN must pass this BEFORE attempting to acknowledge any
 * previous trip; transient faults never authorize an unsafe source. */
static uint8_t bench_preflight_reason(const HeaterSnapshot *h,
                                     uint8_t valid_pd,
                                     const PD_InputContract *c,
                                     uint32_t now_ms,
                                     const HeaterObservation *o)
{
    if(o==0)                              return CONV_BENCH_SOURCE;
    if(h->mode!=HEATER_MODE_POWER)        return CONV_BENCH_MODE;
    if(h->pan_detect_enabled)
    {
        if(!h->off_minutes)                return CONV_BENCH_COUNTDOWN;
        if(ConvAutoFreq_Enabled())        return CONV_BENCH_PHASE;
    }
    else
    {
        if(ConvAutoFreq_Enabled() && h->target_phase_deg<120u)
                                           return CONV_BENCH_PHASE;
        if(ConvController_GetPhaseDegrees()!=h->target_phase_deg)
                                           return CONV_BENCH_PHASE;
    }
    if(!valid_pd)                         return CONV_BENCH_NOPD;
    if(o->input!=HEATER_INPUT_PD)         return CONV_BENCH_SOURCE;
    if(!o->zero_calibrated)               return CONV_BENCH_ZERO;
    if(!o->pot_valid || !o->sys_valid)    return CONV_BENCH_NTC;
    if(o->pot_deci_c>=700 || o->sys_deci_c>=600) return CONV_BENCH_HOT;
    if((uint32_t)(now_ms-o->temperature_updated_ms)>250u)
                                           return CONV_BENCH_AGE;
    return CONV_BENCH_OK;
}
/* Evaluate the complete RUN gate; all explicit commands share it. */
static uint8_t bench_block_reason(const HeaterSnapshot *h,
                                  uint8_t valid_pd,
                                  const PD_InputContract *c,
                                  uint32_t now_ms,
                                  const HeaterObservation *o)
{
    uint8_t reason=bench_preflight_reason(h,valid_pd,c,now_ms,o);
    if(reason!=CONV_BENCH_OK)return reason;
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
    if(h.pan_detect_enabled)
    {
        /* Countdown RUN never becomes continuous output; pulses are
         * controlled exclusively by the TIM1 UPDATE ISR. */
        if(h.state!=HEATER_RUN_REQUESTED)
        {
            pm_api_set_sleep_block(PM_BLOCK_HEATER,0u);
            return;
        }
        if(!allow)
        {
            s_bench_block=bench_block_reason(&h,valid_pd,&contract,now_ms,
                                              observation);
            ConvController_BenchPowerOff();
            Heater_Stop();
        }
        else pm_api_set_sleep_block(PM_BLOCK_HEATER,1u);
        return;
    }
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

/* Restore TIM1 logic PWM only on a manually validated retry, keeping PC6
 * LOW while clearing its stale hardware Break flag and re-arming BIE. */
static uint8_t ConvController_ManualRearmLogicPwm(void)
{
    if(ConvController_LogicPwmRunning())return 1u;
    /* A first PWM start aborted by BIF after setting CEN leaves
     * start_attempted=1 and started=0. Manual recovery must handle it. */
    if(!s_logic_pwm_started && !s_logic_pwm_start_attempted)
        return ConvController_StartLogicPwm();
    if(ConvController_OutputEnabled() ||
       s_state!=CONV_STATE_ADC_MONITOR ||
       ConvProtection_FaultMask()!=0u ||
       !ConvProtection_CompArmed() ||
       !ConvProtection_AnalogInputsSafe())
        return 0u;
    if((TIM1->CR1&TIM_CR1_CEN)==0u)
    {
        /* The first start may have failed before the timer was running.
         * Restart ONLY on this explicit user request with PC6 held LOW. */
        if(s_logic_pwm_started)return 0u;
        s_logic_pwm_start_attempted=0u;
        return ConvController_StartLogicPwm();
    }
    if((TIM1->CCER&(TIM_CCER_CC1E|TIM_CCER_CC3E))!=
                      (TIM_CCER_CC1E|TIM_CCER_CC3E))
        return 0u;
    /* The old Break is history, not evidence of a currently active input.
     * Raw COMP1/COMP2 and PA6 were checked immediately above; the hardware
     * Break remains enabled and can still clear MOE asynchronously. */
    TIM1->SR=(uint32_t)~TIM_SR_BIF;
    __DSB();
    if((TIM1->SR&TIM_SR_BIF)!=0u)return 0u;
    TIM1->DIER |= TIM_DIER_BIE;
    TIM1->BDTR |= TIM_BDTR_MOE;
    __DSB();
    if(!ConvController_LogicPwmRunning() ||
       (TIM1->SR&TIM_SR_BIF)!=0u ||
       ConvProtection_FaultMask()!=0u)
    {
        ConvController_TripISR();
        return 0u;
    }
    s_logic_pwm_started=1u; /* keep state coherent after a failed first start */
    return 1u;
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
    PD_InputContract contract;
    ConvProtectionSnapshot protection;
    uint32_t ibus_trip_ma;
    uint8_t reason,valid_pd;
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
    /* An initial zero capture that FAILED does not heal by waiting.
     * One explicit RUN retries zero calibration with PC6/MOE disabled;
     * the operator must request RUN again once the new capture is READY.
     * Never energize power during the recapture window. */
    if(ConvController_GetState()==CONV_STATE_ADC_MONITOR &&
       !ConvProtection_ZeroReady())
    {
        ConvProtection_GetSnapshot(&protection);
        if(protection.zero_status==CONV_ZERO_FAILED &&
           ConvProtection_RequestZeroRecalibration())
        {
            s_bench_block=CONV_BENCH_ZERO;
            return 0u;
        }
    }
    /* User action is an acknowledgment, NOT an interlock bypass.
     * Check fresh PD/NTC/voltage/phase inputs before any fault is cleared. */
    Heater_SetMode(HEATER_MODE_POWER);
    Heater_GetSnapshot(&hs);
    valid_pd=PD_Input_GetActiveContract(&contract);
    if(hs.pan_detect_enabled && hs.state==HEATER_RUN_REQUESTED)
        return 1u; /* no implicit countdown restart */
    reason=bench_preflight_reason(&hs,valid_pd,&contract,now_ms,
                                  &s_last_bench_obs);
    if(reason!=CONV_BENCH_OK)
    {
        s_bench_block=reason;
        return 0u;
    }
    /* Target input current = RDO minus reserve; AWD2 is another 150mA
     * above that control limit. CT peak/TIM1 Break OCP is independent. */
    ibus_trip_ma=Heater_InputProtectionLimitMa(contract.voltage_mv,
                                              contract.current_ma);
    if(!ibus_trip_ma)
    {
        s_bench_block=CONV_BENCH_CURR;
        return 0u;
    }
    /* A previous fault leaves AWD2 disarmed until guarded ManualRearm. */
    ConvProtection_SetIbusLimitMa(ibus_trip_ma);
    if(!ConvProtection_ManualRearm(now_ms))
    {
        s_bench_block=CONV_BENCH_FAULT;
        return 0u;
    }
    ConvProtection_GetSnapshot(&protection);
    if(!protection.awd2_enabled ||
       protection.ibus_limit_ma!=ibus_trip_ma)
    {
        s_bench_block=CONV_BENCH_FAULT;
        return 0u;
    }
    if(!ConvController_ManualRearmLogicPwm())
    {
        s_bench_block=CONV_BENCH_PWM;
        return 0u;
    }
    /* Clear the Heater policy latch only after all hardware watchdogs
     * and TIM1 logic PWM have been restored. */
    if(hs.state==HEATER_FAULT)Heater_AcknowledgeFault();
    Heater_StartConfirmed(now_ms);
    if(hs.pan_detect_enabled)
    {
        /* Schedule short pulses only: do not energize PC6 on RUN. */
        s_pan_session=0u;
        pm_api_set_sleep_block(PM_BLOCK_HEATER,1u);
        pm_api_refresh_idle();
        return 1u;
    }
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
            {
                const volatile uint16_t *frame=
                  ConvADC_DmaFrame((flags & DMA_ISR_TCIF2)!=0u);
                ConvADC_ProcessFrameISR(frame);
                /* Backstop if TIM1 update IRQ stalls. Six periods at
                 * 150kHz take ~40us; three 20kHz frames force PC6/MOE OFF. */
                if(s_pan_state==PAN_ACTIVE && ++s_pan_adc_guard>=3u)
                    ConvController_TripISR();
                /* Exact 100-frame decimation: deterministic TIM6 clock,
                 * no coroOS latency, no HAL/heap/float in controller. */
                ConvAutoFreq_FrameISR(frame);
            }
        }
    }
    LL_GPIO_ResetOutputPin(ISR_TIME_GPIO_Port, ISR_TIME_Pin);
}

/* TIM1 UPDATE IRQ is enabled ONLY for a six-period probe. First update
 * commits the staged ARR/CCR preload and asserts PC6; six subsequent
 * PWM periods later, PC6 is cleared. TIM1 CEN stays high throughout. */
IH_RAMFUNC void ConvController_PanProbeUpdateISR(void)
{
    if(s_pan_state!=PAN_ARMED && s_pan_state!=PAN_ACTIVE)
    {
        TIM1->DIER &= ~TIM_DIER_UIE;
        return;
    }
    if((TIM1->SR&TIM_SR_BIF)!=0u ||
       (TIM1->BDTR&TIM_BDTR_MOE)==0u ||
       ConvProtection_FaultMask()!=0u)
    {
        LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
        __DSB();
        s_pan_state=PAN_ABORTED;
        TIM1->DIER &= ~TIM_DIER_UIE;
        return;
    }
    if(s_pan_state==PAN_ARMED)
    {
        s_pan_start_index=(uint16_t)(PAN_DMA_LEN -
                                     hdma_tim1_ch2.Instance->CNDTR);
        s_pan_adc_guard=0u;
        s_pan_left=PAN_CYCLES;
        s_pan_state=PAN_ACTIVE;
        LL_GPIO_SetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
        __DSB();
        if((TIM1->SR&TIM_SR_BIF)!=0u ||
           ConvProtection_FaultMask()!=0u)
            ConvController_TripISR();
        return;
    }
    if(--s_pan_left==0u)
    {
        LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
        __DSB();
        s_pan_off_index=(uint16_t)(PAN_DMA_LEN -
                                   hdma_tim1_ch2.Instance->CNDTR);
        s_pan_off_ms=HAL_GetTick();
        s_pan_state=PAN_COLLECT;
        TIM1->DIER &= ~TIM_DIER_UIE;
    }
}
uint8_t ConvController_PanProbeBusy(void)
{
    return (uint8_t)(s_pan_state!=PAN_IDLE);
}
static uint8_t pan_restore(void)
{
    ConvPhaseConfig phase;
    if(!ConvPhase_Calculate(s_pan_restore_arr,s_pan_restore_phase,&phase))
        return 0u;
    if(!ConvController_StageTimerPreloads(&phase,1u))return 0u;
    ConvZcd_Invalidate();
    TIM1->SR=(uint32_t)~(TIM_SR_CC2IF|TIM_SR_CC2OF);
    return 1u;
}
static void pan_cancel(void)
{
    if(s_pan_state==PAN_IDLE)return;
    /* Foreground only. PC6 has already been disabled by the caller. */
    TIM1->DIER &= ~(TIM_DIER_UIE|TIM_DIER_CC2DE);
    (void)HAL_DMA_Abort(&hdma_tim1_ch2);
    if(!pan_restore())ConvController_TripISR();
    s_pan_state=PAN_IDLE;
}
static uint8_t pan_arm(uint32_t now_ms)
{
    ConvPhaseConfig phase;
    uint16_t arr,i;
    if(s_pan_state!=PAN_IDLE || ConvController_OutputEnabled() ||
       ConvAutoFreq_Enabled() || !ConvController_LogicPwmRunning() ||
       (TIM1->SR&TIM_SR_BIF)!=0u ||
       ConvProtection_FaultMask()!=0u ||
       !ConvProtection_CompArmed() || !ConvProtection_ZeroReady() ||
       !ConvPhase_ArrFromKHz(PAN_FREQ_KHZ,&arr))
        return 0u;
    s_pan_restore_arr=(uint16_t)TIM1->ARR;
    s_pan_restore_phase=s_phase_deg;
    s_pan_arr=arr;
    if(!ConvPhase_Calculate(arr,PAN_PHASE,&phase) ||
       !ConvController_StageTimerPreloads(&phase,1u))
        return 0u;
    ConvZcd_Invalidate();
    for(i=0u;i<PAN_DMA_LEN;++i)s_pan_dma[i]=0xFFFFu;
    DMA1->IFCR=DMA_IFCR_CGIF3;
    /* Normal DMA1 Ch3, no IRQ / HAL callbacks: the existing ADC Ch2
     * SRAM ISR cannot afford an extra 150kHz interrupt source. */
    if(HAL_DMA_Start(&hdma_tim1_ch2,(uint32_t)(uintptr_t)&TIM1->CCR2,
                     (uint32_t)(uintptr_t)s_pan_dma,PAN_DMA_LEN)!=HAL_OK)
    {
        (void)pan_restore();
        return 0u;
    }
    s_pan_start_index=s_pan_off_index=0u;
    s_pan_left=s_pan_adc_guard=0u;
    s_pan_armed_ms=now_ms;
    s_pan_state=PAN_ARMED;
    TIM1->DIER |= TIM_DIER_CC2DE;
    /* Clear stale UIF. At the first natural update, the new preloads
     * become active; only then can the ISR raise the FD2204 enable. */
    TIM1->SR=(uint32_t)~TIM_SR_UIF;
    TIM1->DIER |= TIM_DIER_UIE;
    __DSB();
    return 1u;
}
/* Fast foreground poll: no UART or HAL inside TIM1/DMA IRQs. */
void ConvController_PanProbeService(uint32_t now_ms)
{
    HeaterSnapshot h;
    PD_InputContract contract;
    uint8_t valid_pd,why;
    uint16_t count,from,off,i,valid,wr;
    Heater_GetSnapshot(&h);
    if(!h.pan_detect_enabled || h.state!=HEATER_RUN_REQUESTED ||
       !h.off_minutes || !h.off_remaining_s)
    {
        if(s_pan_state!=PAN_IDLE || s_pan_session)
            ConvController_BenchPowerOff();
        s_pan_session=0u;
        return;
    }
    if(s_pan_state==PAN_ABORTED ||
       (s_pan_state!=PAN_IDLE &&
        ((TIM1->SR&TIM_SR_BIF)!=0u ||
         ConvProtection_FaultMask()!=0u ||
         ((s_pan_state==PAN_ARMED || s_pan_state==PAN_ACTIVE) &&
          (uint32_t)(now_ms-s_pan_armed_ms)>PAN_TIMEOUT_MS))))
    {
        ConvController_BenchPowerOff();
        Heater_Stop();
        s_bench_block=CONV_BENCH_FAULT;
        s_pan_session=0u;
        return;
    }
    if(s_pan_state==PAN_COLLECT &&
       (hdma_tim1_ch2.Instance->CNDTR==0u ||
        (uint32_t)(now_ms-s_pan_off_ms)>=PAN_CAPTURE_MS))
    {
        TIM1->DIER &= ~TIM_DIER_CC2DE;
        count=(uint16_t)(PAN_DMA_LEN-hdma_tim1_ch2.Instance->CNDTR);
        if(count>PAN_DMA_LEN)count=PAN_DMA_LEN;
        (void)HAL_DMA_Abort(&hdma_tim1_ch2);
        from=s_pan_start_index;
        off=s_pan_off_index;
        if(from>count)from=count;
        if(off>count)off=count;
        valid=0u;
        for(i=from;i<count;++i)
            if(s_pan_dma[i]<=s_pan_arr)++valid;
        /* Raw CCR2 capture only. No assumption of metal or empty coil.
         * ON_COUNT partitions captures from energized and ringdown phases. */
        if(valid)
        {
            int n=snprintf(s_pan_log,sizeof(s_pan_log),
                "[PAN] SEQ=%lu FREQ=150K PHASE=180 CYC=%u CAP=%u ON_COUNT=%u RAW=",
                (unsigned long)++s_pan_seq,(unsigned)PAN_CYCLES,
                (unsigned)valid,(unsigned)(off-from));
            wr=(n>0 && (uint32_t)n<sizeof(s_pan_log))?(uint16_t)n:0u;
            for(i=from;i<count && wr<sizeof(s_pan_log)-8u;++i)
            {
                int m;
                if(s_pan_dma[i]>s_pan_arr)continue;
                m=snprintf(s_pan_log+wr,sizeof(s_pan_log)-wr,
                           "%s%u",wr && s_pan_log[wr-1u]!='='?",":"",
                           (unsigned)s_pan_dma[i]);
                if(m<=0 || (uint32_t)m>=sizeof(s_pan_log)-wr)break;
                wr=(uint16_t)(wr+(uint16_t)m);
            }
            printf("%s\r\n",s_pan_log);
        }
        if(!pan_restore())
        {
            ConvController_TripISR();
            Heater_Stop();
            s_bench_block=CONV_BENCH_PWM;
        }
        s_pan_state=PAN_IDLE;
        return;
    }
    if(s_pan_state!=PAN_IDLE)return;
    if(!s_pan_session)
    {
        s_pan_session=1u;
        s_pan_due_ms=now_ms+PAN_PERIOD_MS;
        return;
    }
    if((int32_t)(now_ms-s_pan_due_ms)<0)return;
    valid_pd=PD_Input_GetActiveContract(&contract);
    why=bench_block_reason(&h,valid_pd,&contract,now_ms,
                           &s_last_bench_obs);
    if(!s_last_bench_obs_valid ||
       (uint32_t)(now_ms-s_last_bench_obs_ms)>200u ||
       why!=CONV_BENCH_OK)
    {
        s_bench_block=!s_last_bench_obs_valid?CONV_BENCH_AGE:why;
        ConvController_BenchPowerOff();
        Heater_Stop();
        return;
    }
    s_pan_due_ms=now_ms+PAN_PERIOD_MS;
    if(!pan_arm(now_ms))
    {
        s_bench_block=CONV_BENCH_PWM;
        ConvController_BenchPowerOff();
        Heater_Stop();
    }
}
