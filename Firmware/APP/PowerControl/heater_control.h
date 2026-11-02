#ifndef IH01A_HEATER_CONTROL_H
#define IH01A_HEATER_CONTROL_H
#include <stdint.h>
/* UI requests and timekeeping stay in the foreground, never in DMA/ADC ISR.
 * Physical power stage remains DISABLED until a separate, tested safety
 * interlock and electrical control loop are commissioned. */
typedef enum {
    HEATER_MODE_TEMP=0, HEATER_MODE_POWER=1
} HeaterMode;
typedef enum {
    HEATER_OFF=0, HEATER_WAIT_START, HEATER_WAIT_CONFIRM,
    HEATER_RUN_REQUESTED, HEATER_FAULT
} HeaterState;
typedef enum {
    HEATER_INPUT_NONE=0, HEATER_INPUT_WAIT, HEATER_INPUT_DCIN,
    HEATER_INPUT_PD
} HeaterInput;
typedef struct {
    HeaterInput input;
    uint32_t bus_mv;
    uint32_t budget_mw; /* nominal input ceiling, NOT measured power */
    int16_t pot_deci_c;
    int16_t sys_deci_c;
    uint8_t pot_valid,sys_valid;
    uint8_t zero_calibrated; /* mandatory even for proposed power */
    uint32_t temperature_updated_ms;
} HeaterObservation;
typedef struct {
    HeaterMode mode;
    HeaterState state;
    HeaterInput input;
    uint32_t bus_mv;
    uint32_t budget_mw;
    uint32_t requested_power_mw; /* supervisory controller proposal only */
    uint32_t isr_target_mw;      /* hard-gated to zero during bring-up */
    uint32_t fault_mask;         /* retained until reset / explicit recovery */
    uint16_t target_temp_deci_c;
    uint16_t target_phase_deg; /* manual 0..180deg, not watts */
    int16_t pot_deci_c,sys_deci_c;
    uint8_t pot_valid,sys_valid;
    uint16_t off_minutes;  /* configuration for next/current run */
    uint16_t on_minutes;   /* selected reservation delay */
    uint32_t off_remaining_s,on_remaining_s;
} HeaterSnapshot;

void Heater_Init(void);
void Heater_SetMode(HeaterMode mode);
void Heater_SetTargetTemp(uint16_t deci_c);
void Heater_SetPhaseDegrees(uint16_t degrees);
void Heater_SetOffMinutes(uint16_t minutes,uint32_t now_ms);
void Heater_ScheduleMinutes(uint16_t minutes,uint32_t now_ms);
void Heater_StartConfirmed(uint32_t now_ms);
void Heater_Stop(void);
void Heater_LatchFault(uint32_t reason_mask);
void Heater_Poll(uint32_t now_ms,const HeaterObservation *obs);
void Heater_GetSnapshot(HeaterSnapshot *out);
/* Atomic 32-bit proposal mailbox readable by the future electrical ISR.
 * A positive proposal is NOT permission to energize the half bridge. */
uint32_t Heater_GetProposedTargetMw(void);
/* Reserved ISR-facing enable-gated API. Zero until HW safety validation. */
uint32_t Heater_GetIsrTargetMw(void);
#endif
