#ifndef IH01A_PD_INPUT_H
#define IH01A_PD_INPUT_H
#include <stdint.h>
#include "ui_conf.h"
#include "framework/pd_select.h"
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
uint8_t PD_Input_Evaluate(uint32_t *rdo, uint32_t *type);
uint8_t PD_Input_Manual(void);
void PD_Input_GetSnapshot(PD_InputSnapshot *out);
uint32_t PD_Input_VbusMillivolts(void);
void PD_Input_AddSettings(ui_page_t *settings);
void PD_Input_Poll(void);
#endif
