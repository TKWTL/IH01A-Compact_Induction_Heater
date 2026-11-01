# IH01A / USB-PD EPR 28V / 5A migration plan

## Current verified baseline (main)

- STM32G071CBU6, UCPD2; software-free-running sink, permanent external Rd on both CC pins.
- Existing MINSNK binary `USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil.lib`: one automatic SPR Fixed PDO request, at most 20V/3A, followed by optional manual `Settings → PD → Retry`.
- Actual bench evidence: received 6 SPR PDOs; first PDO `0x0A81912C` (5V/3A, bit23 EPR capable), requested fifth SPR PDO 20V/3A; negotiated and sampled ~20.08V.
- Existing ADC multiplier 16; no direct current measurement. Heater PWM remains OFF.

## What this branch implements NOW

- Preserve the existing SPR 3A ceiling, RDO, and MINSNK behavior.
- Update the *offline / pure* extended-capability selector and EPR Fixed RDO to allow a full 5A (28V/5A -> 140W) **only if called after validated EPR mode**.
- The old hand-built EPR RDO helpers have been removed. All SPR/EPR RDOs are encoded from the matching FULL stack's typed `USBPD_SNKRDO_TypeDef` in `pd_input.c`.
- Host regression tests verify SPR selector remains 3A and EPR selector chooses 28V/5A position 8. RDO encoding must be checked during ArmClang rebuild and with a PD analyzer.

**The branch now links the FULL EPR-capable stack and includes an EPR policy path, but 28V negotiation is intentionally disabled at runtime by `IH01A_EPR_28V5A_HARDWARE_APPROVED=0`. Keil compilation, 20V regression and 140W hardware clearance remain required.**

## Next firmware migration: must be done as one matched middleware upgrade

1. Get official ST `stm32-mw-usbpd-core` `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib` together with its matching `inc/*.h` and a compatible version of `stm32-mw-usbpd-device-g0` / application interface. Do not mix binary version and structures/header versions (especially `USBPD_SettingsTypeDef`, PD3 support flags, and EPR message/data-type enum positions). Keep 20V working configuration as fallback.
2. Use the `USBPDCORE_LIB_PD3_FULL` compile define, remove the `USBPDCORE_LIB_PD3_CONFIG_MINSNK` define and link the matching FULL binary. Validate `USBPD_PE_CheckLIB(LIB_ID)`, code/RO/RW/ZI memory sizes and USBPD ISR timer behavior. STM32G071 has 128KiB flash and 36KiB RAM; full .lib archive size is NOT the linked Flash consumption.
3. Set sink EPR-support flag (`PE_PD3_Support.d.Is_EPR_Supported_SNK` on matching current middleware), implement sink EPR capability and operational PDP information, set EPR-capable bit in initial SPR RDO, and expose EPR-specific data-info/callback types.
4. First establish an SPR explicit contract. Once eligible, enter EPR with the FULL stack's `USBPD_PE_Request_EPRModeEnter(port)` API, handle EPR-enter success/failure and extended EPR Source Capabilities. Choose **EPR Fixed 28V/5A**, reject 36V/48V as the hardware policy upper limit is strictly <32V, and send the full EPR Request payload (both RDO and the matching 32-bit CopyPDO, not an ordinary SPR Request).
5. Preserve FULL policy engine's required EPR keepalive and recovery/exit behavior; never brute-force a raw EPR RDO into MINSNK or silently skip the cable check. On invalid cable, no EPR source PDO, refusal, timeouts or restart, fail safely to ordinary SPR ≤20V. Never automatically keep retrying EPR; only the explicit UI menu Retry can request a new evaluation, while normal PD-required stack protocol recovery is still allowed.
6. Use the existing `PD_Input_VbusMillivolts()` measurement to cross-check voltage after contract; gate heater PWM until voltage stable and OVP/UVP thresholds are met.

## 140W hardware gate before setting EPR-capable

- Confirm every input-path device and capacitor withstands 28V (including negotiated transition overshoot), not merely 20V. Verify FET safe operating area, 5A continuity, XT30/shared path, reverse-current isolation, connector, fuse/eFuse and transient/ESD protections. Use appropriate 50V/63V-class components where supported by real measured spikes and derating; do not infer hardware rating from an ADC range.
- Use a certified 240W / EPR 5A electronically marked cable; a marked 100W / 5A cable is not automatically EPR rated. Ensure Source *actually offers* 28V/5A in EPR capabilities.
- Limit initial bench test to no load with PWM disabled and a current-limited supply, monitor VBUS with a meter/scope, then progress with a protected electronic load only after contract state and voltage are confirmed.

## Diagnostic milestones

```
ATTACH
SPR_CAPS ... EPR-capable source PDO
SPR REQUEST ... EPR-capable RDO bit22
SPR CONTRACT (20V or less)
EPR ENTER -> EPRMODE_SUCCEEDED
EPR SOURCE CAPABILITIES -> EPR Fixed 28V/5A PDO
EPR REQUEST -> accept -> PS_RDY -> EPR CONTRACT
VBUS ≈ 28V, stable; UART/ADC and PD state remain alive
```

Official ST references:
- https://github.com/STMicroelectronics/stm32-mw-usbpd-core
- https://github.com/STMicroelectronics/x-cube-tcpp
- https://www.st.com/content/st_com/en/ecosystems/stm32-usb-c.html
