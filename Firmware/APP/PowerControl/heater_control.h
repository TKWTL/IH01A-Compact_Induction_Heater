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
    /* No reservation/countdown start: RUN is the only way into RUN_REQUESTED,
     * and it takes effect immediately. */
    HEATER_OFF=0, HEATER_RUN_REQUESTED, HEATER_FAULT
} HeaterState;
typedef enum {
    HEATER_INPUT_NONE=0, HEATER_INPUT_WAIT, HEATER_INPUT_DCIN,
    HEATER_INPUT_PD
} HeaterInput;
/* Local policy faults: upper bits intentionally separate from all
 * ConvProtection faults (bits 0..17). Displayed in the same hex field. */
#define HEATER_FAULT_NO_BUDGET     (1u<<24)
#define HEATER_FAULT_NTC_INVALID   (1u<<25)
#define HEATER_FAULT_TEMP_STALE    (1u<<26)
#define HEATER_FAULT_SYS_OVERHEAT  (1u<<27)
#define HEATER_FAULT_POT_OVERHEAT  (1u<<28)
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
    uint32_t fault_mask;         /* retained until explicit manual acknowledgment */
    uint16_t target_temp_deci_c;
    uint16_t target_phase_deg; /* manual 0..180deg, not watts */
    int16_t pot_deci_c,sys_deci_c;
    uint8_t pot_valid,sys_valid;
    uint16_t off_minutes;  /* automatic stop delay for the current run */
    uint32_t off_remaining_s;
    /* Explicit, nonpersistent pulse-only pan detection. Never auto-starts
     * output: needs a manual RUN and nonzero off_minutes countdown. */
    uint8_t pan_detect_enabled;
} HeaterSnapshot;

void Heater_Init(void);
void Heater_SetMode(HeaterMode mode);
void Heater_SetTargetTemp(uint16_t deci_c);
void Heater_SetPhaseDegrees(uint16_t degrees);
void Heater_SetOffMinutes(uint16_t minutes,uint32_t now_ms);
/* Only change while HEATER_OFF; independent of normal RUN enable. */
uint8_t Heater_SetPanDetectEnabled(uint8_t enabled);
void Heater_StartConfirmed(uint32_t now_ms);
void Heater_Stop(void);
void Heater_LatchFault(uint32_t reason_mask);
/* Only after controller verifies live fault-source recovery.
 * This does not itself arm hardware or authorize PC6. */
void Heater_AcknowledgeFault(void);
void Heater_Poll(uint32_t now_ms,const HeaterObservation *obs);
void Heater_GetSnapshot(HeaterSnapshot *out);
/* Atomic 32-bit proposal mailbox readable by the future electrical ISR.
 * A positive proposal is NOT permission to energize the half bridge. */
uint32_t Heater_GetProposedTargetMw(void);
/* Reserved ISR-facing enable-gated API. Zero until HW safety validation. */
uint32_t Heater_GetIsrTargetMw(void);
#endif
