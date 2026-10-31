#ifndef IH_TIME_API_H
#define IH_TIME_API_H
#include <stdint.h>
/* 1ms HAL tick; SysTick additionally drives STM32 USBPD timing. */
uint32_t TIME_Millis(void);
#endif
