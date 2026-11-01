#ifndef IH01A_PD_INPUT_H
#define IH01A_PD_INPUT_H
#include <stdint.h>
#include "algorithm/pd_select.h"
typedef enum { PD_INPUT_NONE, PD_INPUT_DCIN, PD_INPUT_WAIT, PD_INPUT_PD } PD_InputKind;
typedef struct {
    uint32_t bus_mv, selected_mv, requested_ma;
    uint8_t attached, explicit_contract, attempted, position, mode;
} PD_InputSnapshot;
void PD_Input_Init(void);
void PD_Input_Attach(uint8_t attached);
void PD_Input_Reset(void);
void PD_Input_Caps(const uint8_t *data, uint32_t bytes);
void PD_Input_Contract(uint8_t ready);
void PD_Input_EPRCaps(const uint8_t *data, uint32_t bytes);
void PD_Input_EPR_Notify(uint32_t event);
void PD_Input_EPR_RequestRejected(void);
void PD_Input_EPR_Poll(void);
uint8_t PD_Input_EPR_CopyPDO(uint32_t *out);
uint8_t PD_Input_EPR_Active(void);
uint8_t PD_Input_Evaluate(uint32_t *rdo, uint32_t *type);
uint8_t PD_Input_Manual(void);
uint8_t PD_Input_ManualEPR(void);
void PD_Input_GetSnapshot(PD_InputSnapshot *out);
uint32_t PD_Input_VbusMillivolts(void);
#endif
