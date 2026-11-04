# IH01A · Heater UART CLI (agent automation)

## Hardware / framing

- UART: USART1 PB6 TX / PB7 RX, **115200, 8N1**, USB-UART serial adapter.
- RX: existing DMA circular + IDLE, 512B ring. Send ASCII **one command per line**, CR, LF or CRLF accepted. No local echo.
- All heater replies are **one line** starting with `HCLI OK` or `HCLI ERR` followed by `\r\n`.
  The existing PD debug log can appear at arbitrary times; host must **filter by `HCLI ` prefix**, send only one in-flight command, and wait for a corresponding response.
- No automatic periodical telemetry: measurements are emitted only on explicit `heater get`. Existing non-heater CLI commands remain unchanged.
- Commands exceeding the 47-character RX line buffer are discarded entirely until the next terminator and return `HCLI ERR LINE_TOO_LONG`, never a partially interpreted command. A `heater get` actively polls TIM1_CH2/CCR2 in the same foreground call; no need to wait for the 100ms heater thread. At 150..400kHz the latest capture may represent an arbitrary one of the recent PWM cycles (no DMA, no jitter distribution). UART throughput limits the sustained query rate, so the host should pace requests and wait for one complete reply before sending another.

## Commands

| Host writes (ASCII + LF) | Response on success | Failure |
|---|---|---|
| `heater phase 90` | `HCLI OK PHASE_DEG=90` | `HCLI ERR RANGE` / `HCLI ERR APPLY` |
| `heater freq 200` | `HCLI OK FREQ_KHZ=200` | `HCLI ERR RANGE` / `HCLI ERR APPLY` |
| `heater on` | `HCLI OK HEATER=ON` **only after physical PC6 readback HIGH** | `HCLI ERR HEATER_BLOCK=<reason>` |
| `heater off` | `HCLI OK HEATER=OFF` **only after PC6 LOW** | `HCLI ERR HEATER_OFF_FAILED` |
| `heater get` | One machine-readable telemetry line | none normally |
| Invalid `heater ...` | — | `HCLI ERR SYNTAX` |

- **Frequency**: 150..400 **kHz integer setpoints**, 1kHz resolution for CLI; STM32 timer is integer ARR, so actual Hz may differ slightly. Reply reports the rounded realized kHz. `heater get` also returns `FREQ_HZ` from `128000000 / (ARR+1)`.
- **Phase**: 0..180 **degrees integer**, 1° CLI setpoints; Settings and Main retain 10° key increments.
- **Live tuning**: existing ARR/CCR1/CCR3/CCR4 preloads are staged under UDIS and transferred on the next natural TIM1 update. The successful phase/frequency commands do not disable PC6, force UG, stop TIM1 or restart RUN.
- **ON gate**: only the existing 12V Fixed PD contract <=3A, voltage, calibrated zero, fresh temperatures, ADC, COMP and hardware Break prerequisites can enable PC6. The CLI does not supply a raw GPIO override. If recent heater observations are missing/stale, it returns `HEATER_BLOCK=STALE`; retry after foreground monitoring is ready.
- **OFF gate**: always issues the immediate safe PC6 low / Heater_Stop sequence. A FAULT state remains latched until separate recovery.
- These setpoint limits are **not safe operating limits**. 150kHz is near the unloaded LC resonance (~148.6kHz); further approaching a workpiece may move resonance above the operating frequency and cause capacitive hard switching. Use oscilloscope, external cut-off and conservative sweeps; CLI does not implement ZVS tracking.

## `heater get` telemetry schema

Example (illustrative values, not measured):

```text
HCLI OK HEATER=ON PHASE_DEG=180 FREQ_KHZ=200 FREQ_HZ=200000 IC2=515 LAG_DEG=+90.3 LAG_RAW_DEG=+110.1 LAG_QUAL=FIT CT_RMS_A=0.700
```

| Field | Meaning |
|---|---|
| `HEATER` | `ON` (PC6 output/readback high), `OFF`, `REQUESTED`, or `FAULT` |
| `PHASE_DEG` | applied/staged phase angle as integer |
| `FREQ_KHZ` | realized frequency rounded to nearest kHz |
| `FREQ_HZ` | frequency from TIM1 ARR, rounded to nearest Hz |
| `IC2` | latest raw TIM1 CCR2 input capture or `NA` |
| `LAG_DEG` | signed, **piecewise-model corrected** lag (degrees, one decimal), or `NA` when outside the accepted fit domain |
| `LAG_RAW_DEG` | original uncompensated zero-cross angle from `ARR/4 + D/2`, or `NA` when capture is missing |
| `LAG_QUAL` | `FIT`, `FREQ`, `PHASE`, `LOW_CT`, `ADC` or `NO_IC2`. `FIT` only indicates availability within the *empty-load* model fit, **not verified ZVS** |
| `CT_RMS_A` | ADC CT_RMS low-pass minus startup zero, 0.25253V/A, formatted amps to three decimals; `NA` while invalid, `SAT` at ADC saturation |

The phase and frequency values are register-setting readbacks, not a promise of instantaneous ZVS; the IC2/LAG capture can report `NA` until a fresh captured edge is available. `LAG_DEG` is **now corrected** using the 2026-11-04 count-domain, three-segment model, but **only** for 160..330kHz, 120..180deg phase and zero-corrected CT_RMS >=0.050A with a valid ADC sample. Outside that domain it is `NA`, while `IC2` and `LAG_RAW_DEG` remain available. Loaded workpieces can change measurement error independently of this empty-coil fit; `LAG_QUAL=FIT` must not be interpreted as a validated safe PLL/ZVS phase. See [ZCD_PHASE_COMPENSATION.md](ZCD_PHASE_COMPENSATION.md). CT_RMS is an ADC-derived estimate, not a calibrated true-RMS wattmeter.

A host agent should parse only keys it needs, treat missing/NA/SAT as invalid rather than as zero, and keep source timestamps or a local test-run index for each poll. In particular, never use `LAG_RAW_DEG` as a fallback for `LAG_DEG=NA` in closed-loop frequency control.

## Python host example

```powershell
py -m pip install pyserial
py Tools/heater_cli_client.py --port COM4 get
py Tools/heater_cli_client.py --port COM4 phase 90
py Tools/heater_cli_client.py --port COM4 freq 200
py Tools/heater_cli_client.py --port COM4 off
```

`on` is also supported but **no example script automatically enables power on connection**.
The client opens the port, sends exactly one requested command, filters legacy logs and exits after a single `HCLI ` reply. For a longer sweep, maintain one serial session and sequence each command/reply before issuing the next command. If you see `HCLI ERR`, do not assume the requested state took effect; issue `heater get` or stop.

## Verification

- `Firmware/Tests/host/test_heater_cli_parse.c` covers exact commands, bounds and malformed inputs.
- `Firmware/Tests/host/test_conv_phase.c` covers integer ARR mapping 150..400kHz and 0..180° compare bounds.
- `Firmware/Tests/host/test_heater_cli_commands.c` uses mock ADC/timer/controller to validate complete one-line response formats, guarded start refusal, corrected/raw lag and invalidity statuses.
- `Firmware/Tests/host/test_conv_zcd.c` covers the signed three-segment correction, 180deg empty-coil sweep, CT and phase gating, and rejection outside 160..330kHz.
- On target: confirm standard 115200 command ACK/ERR, verify ON refuses absent PD, verify OFF removes PC6 immediately, and compare `get` with MiaoUI and oscilloscope.
- Full Keil Rebuild and physical ZVS/electrical validation are still required.
