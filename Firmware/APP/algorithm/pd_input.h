#ifndef IH01A_PD_INPUT_H
#define IH01A_PD_INPUT_H
#include <stdint.h>
#include "algorithm/pd_select.h"
typedef enum { PD_INPUT_NONE, PD_INPUT_DCIN, PD_INPUT_WAIT, PD_INPUT_PD } PD_InputKind;
/* A completed Fixed PDO contract, not instantaneous measured VBUS.
 * Current is the RDO operating-current allowance, not actual draw. */
typedef struct {
    uint32_t voltage_mv;
    uint32_t current_ma;
    uint8_t position;  /* 1..13 */
    uint8_t epr;
} PD_InputContract;
typedef struct {
    uint32_t bus_mv, selected_mv, requested_ma;
    uint8_t attached, explicit_contract, attempted, position, mode;
} PD_InputSnapshot;
void PD_Input_Init(void);
void PD_Input_Attach(uint8_t attached);
void PD_Input_Reset(void);
void PD_Input_Caps(const uint8_t *data, uint32_t bytes);
void PD_Input_Contract(uint8_t ready);
void PD_Input_RequestRejected(void);
/* enabled=0 requests only 5V Fixed; Sink and external Rd remain active.
 * Policy changes are asynchronous: check GetActiveContract for completion. */
void PD_Input_SetRequestEnabled(uint8_t enabled);
uint8_t PD_Input_GetRequestEnabled(void);
/* Fixed PDO upper bound, integer volts. Valid 5..31; only 28V EPR Fixed
 * is currently supported above 20V. Returns 0 for invalid input. */
uint8_t PD_Input_SetMaxFixedVoltage(uint8_t voltage_v);
uint8_t PD_Input_GetMaxFixedVoltage(void);
/* Returns 1 only for an explicitly confirmed, stable contract. On failure,
 * *out is zero-filled. During renegotiation or EPR exit returns 0. */
uint8_t PD_Input_GetActiveContract(PD_InputContract *out);
void PD_Input_EPRCaps(const uint8_t *data, uint32_t bytes);
void PD_Input_EPR_Notify(uint32_t event);
void PD_Input_EPR_RequestRejected(void);
void PD_Input_EPR_Poll(void);
uint8_t PD_Input_EPR_CopyPDO(uint32_t *out);
uint8_t PD_Input_EPR_Active(void);
uint8_t PD_Input_Evaluate(uint32_t *rdo, uint32_t *type);
uint8_t PD_Input_Manual(void);
uint8_t PD_Input_ManualEPR(void);
void PD_Input_SetEPRObjectPosition(uint8_t absolute);   /* 0 = 1-based local, 1 = absolute 8..13 */
void PD_Input_SetEPRAuto(uint8_t on);
uint8_t PD_Input_GetEPRAuto(void);
void PD_Input_GetSnapshot(PD_InputSnapshot *out);
uint32_t PD_Input_VbusMillivolts(void);
#endif
