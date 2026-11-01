# IH01A APP/algorithm — USB-PD application protocol policy

The board's STM32G071 **physical UCPD and ST USB-PD middleware** remain under
`Firmware/USBPD/` and `Firmware/Middlewares/`. This directory contains the
application-owned PD service, independent of MiaoUI and PM framework code.

- `pd_input.c/h` — attach/detach, RDO selection and single-attempt policy,
  manual refresh, contract snapshot and ADC-scaled VBUS.
- `pd_select.c/h` — standalone SPR and *future* EPR Fixed PDO/RDO
  helper functions, testable on a host. EPR helpers are not active on the
  presently linked MINSNK library.
- `pd_source_stats.c/h` — read-only Source PDO cache and formatting.

The MiaoUI menu glue lives in `../MiaoUI/pd_ui.c/h`. It calls only
`PD_Input_GetSnapshot()` and `PD_Input_Manual()`, and never directly
modifies protocol state.

The ST DPM callbacks invoke `PD_Input_*` and `PD_SourceStats_*`; they must
remain nonblocking and must not call printf or display code. The PD stack
timer remains on SysTick; hardware GoodCRC handling stays with ST's UCPD
driver. Do not port CH32X035 PHY register accesses to STM32G071.

**Working baseline:** Lenovo C140 20V SPR; EPR 28V not active. See
`../../../Docs/PD_C140_COMPARISON.md` and
`../../../Docs/USBPD_EPR_MIGRATION.md`.
