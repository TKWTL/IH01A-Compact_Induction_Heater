#include "time_api.h"
#include "stm32g0xx_hal.h"
uint32_t TIME_Millis(void) { return HAL_GetTick(); }
