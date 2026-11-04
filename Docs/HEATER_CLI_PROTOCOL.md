# IH01A · Heater UART CLI — auto frequency ISR (2026-11-04)

USART1 PB6/PB7: 115200 8N1. ASCII lines terminated by CR/LF. Heater replies are
one line prefixed by `HCLI OK` or `HCLI ERR`; PD logs can interleave.

## Commands

| Input | Response |
|---|---|
| `heater auto on` | `HCLI OK AUTO_FREQ=ON` |
| `heater auto off` | `HCLI OK AUTO_FREQ=OFF` |
| `heater phase 160` | `HCLI OK PHASE_DEG=160` |
| `heater on` | `HCLI OK HEATER=ON` only after the guarded PC6 enable succeeds |
| `heater off` | `HCLI OK HEATER=OFF` after PC6 reads LOW |
| `heater get` | Machine-readable telemetry |
| `heater freq 200` | `HCLI ERR MANUAL_FREQ_DISABLED` |

Manual frequency editing was removed from Settings → Heater and the CLI.
Settings now has an Auto Freq switch, read-only actual frequency and AF state.
Auto starts **OFF**; fixed bench baseline is **300kHz**. A change of auto mode
while PC6 is high first **stops heating**, returns to 300kHz, and requires
a fresh `heater on`. Power is never implicitly resumed. Auto needs
phase 120..180deg because the correction fit is not valid below 120deg.

Host usage:

```powershell
py Tools/heater_cli_client.py --port COM4 phase 160
py Tools/heater_cli_client.py --port COM4 auto on
py Tools/heater_cli_client.py --port COM4 get
py Tools/heater_cli_client.py --port COM4 on
py Tools/heater_cli_client.py --port COM4 off
```

## GET telemetry

Example (illustrative, not a measurement):

```text
HCLI OK HEATER=ON PHASE_DEG=160 FREQ_KHZ=285 FREQ_HZ=285078 IC2=300 LAG_DEG=+23.1 LAG_RAW_DEG=+60.1 LAG_QUAL=FIT CT_RMS_A=0.700 AUTO_FREQ=ON AF_STATE=TRACK IBUS_DELTA=190
```

- `HEATER` is ON/OFF/REQUESTED/FAULT; ON checks physical PC6.
- `PHASE_DEG` is the requested/staged 0..180deg angle.
- `FREQ_KHZ` and `FREQ_HZ` report actual TIM1 ARR-derived frequency.
- `IC2` is the raw TIM1 CH2 capture or NA.
- `LAG_DEG` is the corrected one-decimal signed angle, NA outside the fit.
- `LAG_RAW_DEG` is the uncorrected angle, NEVER a fallback for auto control.
- `LAG_QUAL`: FIT/FREQ/PHASE/LOW_CT/ADC/NO_IC2. FIT only means inside the *empty-coil* fit region, not ZVS verified with a load.
- `CT_RMS_A` is an estimated low-pass current after zero subtraction, NA/SAT when invalid.
- `AUTO_FREQ` is ON/OFF; `AF_STATE` is OFF/SAFE/ACQUIRE/TRACK/RECOVER/FAULT.
- `IBUS_DELTA` is filtered ADC IBUS minus zero code (counts).

The host must accept NA/SAT and partial invalid feedback, and should never
treat numerical counts as a guarantee of MOSFET soft switching.

## Closed-loop control timing and quantization

TIM6 → ADC1 eight-rank DMA generates approximately 20k completed frames/s.
`ConvAutoFreq_FrameISR` is called immediately after `ConvADC_ProcessFrameISR`.
Every **100 complete frames** its control logic runs once (**nominal 5ms**).
The other 99 frames only increment/check a counter; there is no coroOS
scheduling latency, floating point or heap allocation in this controller.

With TIM1 kernel clock 128MHz, PSC0, automatic frequency is **170..300kHz**
(ARR 752..426). Target corrected CT lag is **+20deg**, deadband **±2deg**.
The exact quantized P-only update law every 5ms is:

```text
error_deci = corrected_phase_deci - 200
if abs(error_deci) <= 20: deltaARR = 0
else: deltaARR = clamp(round(error_deci / 20), -8, +4)
nextARR = clamp(ARR + deltaARR, 426, 752)
```

That is 0.5 ARR count per degree per 5ms, faster increasing frequency
(ARR -8) than decreasing frequency (ARR +4), and **no integral windup**.
Loss of phase feedback or low load current returns toward 300kHz, but
energized transitions remain slew-limited to -8/+4 ARR every 5ms.

IBUS lock requires ≥175 ADC codes above calibrated zero (~0.5A),
releasing at ≤140 (~0.4A). CT phase requires at least 21 post-zero
CT_RMS ADC counts (~0.05A), unsaturated ADC, phase 120..180deg and
fresh CH2 input-capture activity. Corrected phase is calculated using
precomputed quarter-tick delay compensation and Q12 angle multipliers
for the 327 allowed ARR settings. Host reference comparisons bound the
difference from the original count-domain formula to <0.3deg.

Persistent missing feedback at nontrivial IBUS current faults after
20 control ticks (100ms). Non-positive lag trips immediately; persistent
lag <+5deg trips after 6 ticks (30ms). Failure latches
`CONV_PROT_AUTO_FREQ` and pulls PC6 LOW. The original independent
analog comparator TIM1 Break, ADC watchdog, source and NTC interlocks
are still required, including the explicit 12V / ≤3A bench gate.

TIM1 ARR/CCR1/CCR3/CCR4 preload updates use UDIS to transfer coherently
on the next **natural update event**. No UG, counter restart or PC6
enable is issued by the frequency loop. Foreground phase edits and
ISR frequency updates use a short interrupt-safe critical section;
the physical hardware Break path remains asynchronous.

## Qualification and tests

**This is experimental 12V bench control, not a commissioned loaded
ZVS controller.** CT comparator offset was fitted on the unloaded tank,
primarily 160..330kHz and phase 120..180deg. A workpiece can change
current waveform, comparator overdrive, effective impedance and detector
delay. FIT status is NOT proof of safe switching. Verify bridge VDS,
real voltage/current phase, external current limiting, and loaded
temperature before increasing power.

Run `bash Firmware/Tests/host/run.sh` for the C99 tests, including
quantized Q12 phase versus `ConvZcd_Calculate`, current lock/recovery,
UART protocol, and ARR slew limiting. Full Keil target rebuild and
the maximum ISR_TIME pulse versus the **50µs ADC frame budget** have
not been demonstrated in this response; both are mandatory before
energizing a real workpiece.
