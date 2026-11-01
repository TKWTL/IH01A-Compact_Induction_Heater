#ifndef IH01A_RECOVERY_PROFILE_H
#define IH01A_RECOVERY_PROFILE_H
/* Debug-only recovery branch. Type-C external Rd remains connected.
 * 1: never start UCPD CAD/PE, negotiate or run the 20kHz ADC.
 * 0: compile normal IH01A firmware (same behavior as main).
 *
 * Do NOT use 5V recovery when relying on a previous 20V contract:
 * unplug the PD source and let VBUS discharge before reconnecting.
 */
#define IH01A_RECOVERY_5V_ONLY 1
#endif
