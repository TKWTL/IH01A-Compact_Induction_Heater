#ifndef IH01A_CONV_CONTROLLER_H
#define IH01A_CONV_CONTROLLER_H
#include <stdint.h>
typedef enum {
    CONV_STATE_STOP=0,
    CONV_STATE_ADC_MONITOR,
    CONV_STATE_FAULT
} ConvState;
void ConvController_Init(void);
ConvState ConvController_GetState(void);
uint32_t ConvController_ErrorCount(void);
void ConvController_FaultISR(void);
#endif
