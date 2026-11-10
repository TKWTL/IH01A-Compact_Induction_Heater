#ifndef IH01A_PAN_PROBE_CONFIG_H
#define IH01A_PAN_PROBE_CONFIG_H
/* Experimental pan probe only. NOT a guarantee of ZVS or permissible
 * peak current. 0x00004000 = TIM1 hardware Break: never bypass protection.
 *
 * After 12 V tank waveform / Break in the six-cycle 150 kHz experiment,
 * reduce energy injection by using only two periods at requested 160 kHz.
 * This frequency remains close to the observed 158 kHz ring frequency:
 * electrical safety and actual PC6 duration require oscilloscope checks.
 */
#define PAN_FREQ_KHZ 160u
#define PAN_PHASE 180u
#define PAN_CYCLES 2u
#endif
