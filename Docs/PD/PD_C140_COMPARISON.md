# C140 EPR and VBUS reset diagnostics — IH01A / DPC27A / CH32X035

## Confirmed repository evidence

1. [CH32X035_DemoBoard PD debug log](https://github.com/TKWTL/CH32X035_DemoBoard/blob/main/Firmware/Peripheral/PD/pd_snk_debug.md):
   same Lenovo C140 successfully negotiated SPR 20V/5A, EPR Mode and
   EPR Fixed 28V/5A; INA226 measured ~28.00V. EPR keepalive: 375ms.
   One observed failure was rearming RX while auto-GoodCRC TX had not
   completed. Another was UART debug output inserted between Source
   Capabilities GoodCRC and Sink Request; Source stopped acknowledging
   the delayed Request and VBUS fell, resetting the VBUS-powered MCU.
   After removing critical-path logging the first Request TX was about
   5.1ms after GoodCRC (measurement, not a guaranteed spec constant).
2. [DPC27A PD service](https://github.com/TKWTL/DPC27A-Desktop_PD_Power_Converter/tree/main/Firmware/Peripheral/PD):
   application `pd.c` state machine is separate from `pd_port.c` WCH
   PHY; the verified flow uses normal SPR contract, source-capabilities
   extended, EPR Mode Enter, chunked EPR Source Capabilities,
   EPR Request, PS_RDY and periodic 375ms EPR KeepAlive.
   Its local VBUS-powered recovery path suppresses self-initiated
   Hard Reset. These are reference behaviors, **not** compatible
   STM32G071 register drivers.
3. [IH01A](https://github.com/TKWTL/IH01A-Compact_Induction_Heater):
   currently links `USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil.lib`.
   `DPM_Settings` advertises PD revision 3 but has no EPR Sink enable;
   the 20V RDO does not set EPR Capable (bit 22). The
   `USBPD_DPM_SetDataInfo()` mirror only records
   `USBPD_CORE_DATATYPE_RCV_SRC_PDO` (ordinary SPR source caps), not
   `USBPD_CORE_DATATYPE_RCV_SRC_PDO_EPR`. The UI therefore displays
   five Fixed + PPS, even if the source can supply 28V. The mere
   presence of these six advertised SPR objects is not evidence of
   charger incompatibility.

## Why the reset during 5V -> 20V matters

A *valid* SPR voltage change does not normally require discharging the
input to vSafe0V. If the board actually drops to ~0V and reboots
between 5V and 20V, distinguish:

- **Source Hard Reset:** CC shows HRST and VBUS drops; the VBUS-fed MCU
  loses power. Check timing, GoodCRC, stack event notifications and
  excessive work inside callbacks.
- **Power path brownout:** CC protocol succeeds and VBUS stays up, but
  downstream regulator/NRST collapses. Check regulator UVLO, input
  switch, startup transient and local rail hold-up.
- **Software reset / watchdog:** VBUS and regulated rail remain stable;
  inspect RCC reset flags on next boot.

Use a scope with at least VBUS, MCU VDD and NRST, plus a CC PD analyzer
or software UCPD trace. Do not conclude which cause occurred from an
on-screen restart alone.

## Implementation / test priorities

1. Keep the known-good 20V SPR firmware available; do not forcibly
   rewrite EPR flags against MINSNK. Disable heater PWM during tests.
2. Record initial SPR Source PDO #1 EPR-support bit 23, Sink Request
   bit 22, Source response (Accept/Wait/Reject), PS_RDY and Hard Reset.
   The current RDO bit 22 is zero by design because the old stack
   does not support EPR entry.
3. In the eventual matched ST FULL EPR stack, enable EPR Sink support,
   obtain source/cable prerequisites and send EPR Mode Enter only after
   a successful SPR contract with EPR-capable RDO. Source may need to
   supply VCONN for cable discovery; passive Rd alone is not proof of
   a 5A/EPR-capable cable.
4. Log EPR Mode Enter ACK/Success/Failure reason, receive complete
   *combined* EPR Source Capabilities including positions 1..7 SPR and
   positions 8..13 EPR. A source may send it in chunks. Only then
   select 28V fixed (<32V ceiling). EPR Request needs a copy of the
   selected EPR PDO. Preserve KeepAlive and fail-safe fallback.
5. Prefer trace ring buffers or GPIO timestamps to blocking UART in
   the PD sender-response path. Do not use critical-path printf,
   screen refresh, ADC synchronous waits or blocking delays.
6. Repeat >=10 hotplug tests and observe stable VBUS and EPR keepalive
   for minutes, using an EPR-capable e-marked cable and audited 28V
   power path. C140 high-AC-voltage input may be needed on some units.

## Source-side hardware / software differences

The CH32X035 `pd_port.c` uses WCH USBPD registers and auto GoodCRC.
IH01A uses STM32G071 UCPD + ST middleware: **do not copy the WCH
PD_ALL_CLR/RX/IRQ sequence**. Reuse only the layered API and test
discipline. DPC27A includes 20V/7A Lenovo proprietary support; IH01A
retains its independent **3A request limit** and must not infer 7A
approval from the source's advertisement.

## Current code layout

- `Firmware/APP/algorithm`: request policies and PDO snapshots, no UI.
- `Firmware/APP/MiaoUI/pd_ui.c`: Settings/PD and status display.
- `Firmware/USBPD`: ST middleware glue and UCPD callbacks.
- `Firmware/APP/framework`: general PM and system services only.
