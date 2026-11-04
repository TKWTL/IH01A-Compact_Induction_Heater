# Host checks

Run `sh run.sh` from this directory with a C99 compiler.
Checks: PM timer, PB02B mini_format and real USB-PD Source PDO
formatting (Fixed/Variable/Battery/PPS, bounds, no-source reset).
No MCU, USBPD middleware or OLED hardware is required for these tests.
They do **not** substitute for the Keil ARMClang build or PD analyzer.

RTC time-service regression: tests leap-day/month rollovers, 2000-based
calendar-to-seconds conversion, uptime, formatting and mocked RTC read
failures. The mock is under `Tests/host/rtc_mock` and is used only by
host tests; it does not alter the STM32 HAL or the Keil project.

`test_pd_select.c` verifies highest eligible Fixed SPR PDO, current
clamp, RDO object position/units and rejection of EPR/PPS/invalid bytes in SPR, plus full combined EPR capabilities parsing (28V Fixed position 8; AVS and >=32V rejected).


2026-11-01: `test_ntc_lut.c` checks NTC table anchor points, monotonic
interpolation and disconnected-sensor bounds; `test_temp_service.c`
uses mocked time/ADC/control to ensure 100ms cadence, filtered samples,
ADC fault invalidation and independent sensor validity.

2026-11-04: `test_heater_cli_parse.c` validates exact command grammar,
invalid/range handling, and `test_heater_cli_commands.c` links the real
heater CLI with simulated controller, ADC and timer to verify one-line
ACK/ERR, protected start refusal, ON/OFF transitions, stale IC2 and CT_RMS
saturation. `test_conv_phase.c` covers integer ARR mapping 150..400kHz.
None of the host tests proves PWM waveforms, ZVS, or MCU-in-target build.
