# IH01A · Heater UART CLI: 2S2P / CT 0.075V/A

USART1 PB6/PB7, 115200 8N1. Send lowercase ASCII + LF; each command returns one line prefixed by `HCLI OK` or `HCLI ERR`. PD diagnostic lines may interleave.

## Commands

| Command | Behavior |
|---|---|
| `heater phase 180` | Set 0..180° phase (default **180°**) |
| `heater freq 200` | Manual frequency, **150..400 kHz** (integer 1 kHz request), only while auto is OFF |
| `heater auto off` | `HCLI OK AUTO_FREQ=OFF` |
| `heater auto on` | **`HCLI ERR CT_ZCD_RECAL_REQUIRED`** until CT_ZERO has been recalibrated for 0.075V/A |
| `heater on` | One guarded PC6 enable request; no bypass of PD/NTC/Break/ADC interlocks |
| `heater off` | Pull PC6 LOW immediately |
| `heater get` | Read telemetry, including physical frequency/phase, CT current and raw/compensated IC2 status |

Malformed frequency input returns `HCLI ERR SYNTAX`; outside 150..400 returns `HCLI ERR RANGE`. If auto mode is ever revalidated and enabled, manual frequency writes return `HCLI ERR AUTO_FREQ_ENABLED`. Frequency writes continue using the existing atomic TIM1 ARR+CCR1/3/4 preload transaction without stopping the PWM carrier. Never infer zero-voltage switching (ZVS) from a valid register write.

## Settings → Heater

- `Phase deg`: defaults to **180°**, 10° UI increments.
- `Auto Freq`: OFF at power-up. The loop remains compiled but **enabling is refused** until new comparator timing measurements establish the CT_ZERO model.
- `Freq kHz`: editable with **10 kHz** steps when Auto Freq is OFF; endpoint wrapping is rejected. When Auto Freq is ON, edits are reverted.
- `CT_RMS`, `IC2` and `LagR` remain available for bench characterization.

## Measurement calibration

The proposed CT_OUT hardware gain changes **0.100 → 0.075 V/A**. Assuming the existing downstream full-wave rectifier and RC filter gains stay unchanged, nominal CT_RMS transfer is **0.25253 × 0.75 = 0.1893975 V/A**, relative to the startup zero. This is an estimated display conversion until measured.

At 2.5V ADC reference, ±12A peak current corresponds to a CT_OUT swing ±0.900V and ±1474 DAC counts, rather than ±1966. The default comparator OCP window is updated to **preserve ±12A**; allowing ±16A requires separate hardware validation. **IBUS remains 0.2134 V/A** and its existing ADC watchdog settings are unchanged.

Because comparator overdrive and input slew change, the historical CT_ZERO delay regression (0.100V/A) must not be used as valid phase compensation. Until recalibration:
- `LAG_RAW_DEG` and `IC2` report the uncorrected capture for data collection.
- Within an otherwise eligible capture, `LAG_DEG=NA` and `LAG_QUAL=RECAL`.
- Outside the existing phase, current or frequency gates, statuses such as `PHASE`, `LOW_CT` and `FREQ` still apply.
- The 5ms ADC DMA auto-frequency control loop stays disabled. Its internal 50mA nominal CT threshold is updated from 21 to 16 codes for future use, but **no new tracking calibration is asserted**.

## Telemetry and host client

Illustrative telemetry:
```text
HCLI OK HEATER=ON PHASE_DEG=180 FREQ_KHZ=200 FREQ_HZ=200000 IC2=444 LAG_DEG=NA LAG_RAW_DEG=+114.9 LAG_QUAL=RECAL CT_RMS_A=0.932 AUTO_FREQ=OFF AF_STATE=OFF IBUS_DELTA=0
```

Run:
```powershell
py Tools/heater_cli_client.py --port COM4 phase 180
py Tools/heater_cli_client.py --port COM4 auto off
py Tools/heater_cli_client.py --port COM4 freq 200
py Tools/heater_cli_client.py --port COM4 get
py Tools/heater_cli_client.py --port COM4 off
```

`heater freq` reports **realized rounded kHz**, as actual TIM1 timer ARR is integer. The manual frequency setpoint is not persisted over reset; boot baseline remains **300 kHz**. If connected to a load, test VDS/VGS and the tank current against the actual switching voltage phase, and qualify the modified winding's impedance, capacitor current, current transformer polarity, and hardware comparator trip at low voltage and limited supply current before increasing input power.

Run `bash Firmware/Tests/host/run.sh` for portable C99 tests. Keil target rebuild and hardware measurement are separately required.
