#ifndef PM_API_H
#define PM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Application-facing API of the power framework (UI_OFF level port).
 *
 * Activity sources that keep the idle timer reset (all call
 * pm_api_refresh_idle()):
 *   - any key press (thread_ui);
 *   - PD/VBUS or output-port plug/unplug edges (thread_pm);
 *   - total output power above 5 W (thread_pm).
 *
 * Block bits freeze AUTOMATIC idle countdown; they cannot prevent a
 * user-requested UI_OFF. UI_OFF does NOT stop the heater, MCU or PD tasks. */
void pm_api_init(void);
void pm_api_poll(void);                          /* thread_pm: advance the state machine */
void pm_api_refresh_idle(void);                  /* generic activity; ignored in manual UI_OFF */
void pm_api_wake_from_key(void);                 /* debounced physical key wakes UI_OFF */
void pm_api_force_sleep(void);                   /* manual UI_OFF; ignores automatic blocks */
void pm_api_set_sleep_timeout(int timeout_sec);  /* 0 = No Auto Sleep, >0 = seconds */
/* Heater PC6 HIGH: freeze AUTO sleep only; manual UI_OFF leaves PC6 intact. */
#define PM_BLOCK_HEATER (1u<<0)
void pm_api_set_sleep_block(uint8_t mask, uint8_t enable);
uint8_t pm_api_get_block_mask(void);
uint8_t pm_api_ui_should_block(void);            /* 1 = thread_ui must stop drawing */
uint8_t pm_api_is_sleeping(void);
void pm_api_set_unstable_wake(uint8_t unstable);
uint8_t pm_api_is_unstable_wake(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_API_H */
