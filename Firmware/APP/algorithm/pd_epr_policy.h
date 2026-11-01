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

/* SPR current ceiling. The reference EPR-capable sink baseline opens with a
 * 20 V/5 A EPR-capable Request before EPR_Mode, so the SPR policy now asks
 * for what the Source PDO offers up to 5 A. Safety comes from the Source
 * side: a 3 A (non e-marked) cable makes the Source advertise a 3 A PDO, and
 * the request is clamped to the PDO current, so 5 A can only happen on a
 * 5 A e-marked cable. */
#define IH01A_PD_SPR_MAX_MA        5000u
#endif
