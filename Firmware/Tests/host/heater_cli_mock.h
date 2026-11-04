#ifndef IH01A_HEATER_CLI_MOCK_H
#define IH01A_HEATER_CLI_MOCK_H
#include <stdint.h>
#include "PowerControl/conv_controller.h"
#include "PowerControl/conv_phase.h"
#include "PowerControl/conv_zcd.h"
#include "PowerControl/conv_auto_freq.h"
#include "PowerControl/conv_adc.h"
#include "PowerControl/conv_protection.h"
#include "PowerControl/conv_current.h"
#include "PowerControl/heater_control.h"

typedef struct { uint32_t ARR; } HeaterCliMockTim;
extern HeaterCliMockTim *TIM1;
uint32_t HAL_GetTick(void);
void pm_api_refresh_idle(void);
int HeaterCLI_MockPrintf(const char *fmt,...);
#endif
