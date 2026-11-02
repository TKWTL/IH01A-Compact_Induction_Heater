/* TIM14 CH1 -> PF0 (BEEP): GPIO AF2 is already set by CubeMX.
 * Board clock: SYSCLK/HCLK=64 MHz, APB1=32 MHz (prescaler /2),
 * therefore TIM14 input=64 MHz and PSC=63 gives an exact 1 MHz tick.
 * The scheduler passes precomputed ARR values so each note update does
 * not need to divide or consult the clock tree.
 */
#include "framework/beep_hw.h"
#include "main.h"
#include "tim.h"

static uint8_t s_running;

void beep_hw_init(void)
{
    s_running=0u;
    __HAL_TIM_SET_PRESCALER(&htim14,63u);
    __HAL_TIM_SET_AUTORELOAD(&htim14,999u);
    __HAL_TIM_SET_COMPARE(&htim14,TIM_CHANNEL_1,0u);
    __HAL_TIM_SET_COUNTER(&htim14,0u);
    htim14.Instance->EGR=TIM_EGR_UG;
}
void beep_hw_stop(void)
{
    __HAL_TIM_SET_COMPARE(&htim14,TIM_CHANNEL_1,0u);
    if(s_running)
    {
        (void)HAL_TIM_PWM_Stop(&htim14,TIM_CHANNEL_1);
        s_running=0u;
    }
}
void beep_hw_set_arr(uint16_t arr)
{
    if(arr==0u){beep_hw_stop();return;}
    __HAL_TIM_SET_AUTORELOAD(&htim14,(uint32_t)arr);
    __HAL_TIM_SET_COMPARE(&htim14,TIM_CHANNEL_1,((uint32_t)arr+1u)/2u);
    __HAL_TIM_SET_COUNTER(&htim14,0u);
    htim14.Instance->EGR=TIM_EGR_UG;
    if(!s_running)
    {
        if(HAL_TIM_PWM_Start(&htim14,TIM_CHANNEL_1)==HAL_OK)
            s_running=1u;
        else
            beep_hw_stop();
    }
}
