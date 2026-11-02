#ifndef IH01A_FRAMEWORK_BEEP_HW_H
#define IH01A_FRAMEWORK_BEEP_HW_H
#include <stdint.h>
/* PWM backend for TIM14_CH1/PF0. Host tests mock these calls. */
void beep_hw_init(void);
void beep_hw_set_arr(uint16_t arr); /* zero mutes output */
void beep_hw_stop(void);
#endif
