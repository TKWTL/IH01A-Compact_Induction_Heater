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

/* EPR policy numbers. The three values must stay consistent: the Sink
 * advertises PDP = 28V x current and asks for exactly that current, so a
 * 84W PDP together with a 5A Sink PDO / 5A request would be a spec
 * mismatch that a Source is entitled to reject.
 *
 *   28 V x 5 A = 140 W  (current branch policy, 240W e-marked cable)
 *   28 V x 3 A =  84 W  (fallback policy if the cable/input path cannot
 *                        be certified for 5 A: also set the Sink PDO
 *                        current and the 28V/5A acceptance in pd_input.c)
 */
#define IH01A_EPR_SINK_PDP_W       140u
#define IH01A_EPR_SINK_CURRENT_MA  5000u
#endif
