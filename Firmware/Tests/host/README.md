# Host checks

Run `sh run.sh` from this directory with a C99 compiler.
Checks: PM timer, PB02B mini_format and real USB-PD Source PDO
formatting (Fixed/Variable/Battery/PPS, bounds, no-source reset).
No MCU, USBPD middleware or OLED hardware is required for these tests.
They do **not** substitute for the Keil ARMClang build or PD analyzer.
