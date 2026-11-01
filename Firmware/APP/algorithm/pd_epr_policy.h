#ifndef IH01A_PD_EPR_POLICY_H
#define IH01A_PD_EPR_POLICY_H
/* IMPORTANT: Enable ONLY after verifying complete 28V/5A input path,
 * independent OVP and a certified 240W EPR e-marked cable.
 * Setting this to 1 authorizes an actual 28V request; leave OFF until then.
 */
/* 28V input path reviewed by hardware owner on 2026-11-02. Test mode
 * permits explicit user-triggered EPR only; no automatic 28V at boot.
 * The FULL stack/source must verify EPR 5A e-marker cable eligibility.
 * Never energize heater PWM until an independent power-stage review.
 */
#define IH01A_EPR_28V5A_HARDWARE_APPROVED 1
#define IH01A_EPR_28V5A_AUTO_ENTER 0
#endif
