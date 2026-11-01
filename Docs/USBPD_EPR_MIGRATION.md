# IH01A USB-PD 3.1 EPR migration notes (2026-11-01)

## Current status

**SPR works at 20 V. The main firmware is NOT YET EPR enabled.** This distinction is
deliberate: the current Keil target links
`USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil.lib` and defines
`USBPDCORE_LIB_PD3_CONFIG_MINSNK`. The bundled older
`stm32-mw-usbpd-core` headers do not enable EPR. Setting an EPR flag or
constructing a 28 V request does not add EPR Mode Entry to this binary.

The host-testable helpers `PD_Input_BestEPRFixed` and
`PD_Input_EPRFixedRDO` can select a 28 V fixed EPR PDO from the *EPR*
capabilities array, clamp the requested current to 3 A and produce the first
32-bit RDO. They are not currently invoked by the active SPR DPM. An EPR
request also needs the selected PDO copy and a complete EPR capable stack.

## Evidence and working STM32G071 example

- ST USBPD Core (supports EPR when FULL configuration is linked):
  https://github.com/STMicroelectronics/stm32-mw-usbpd-core
- ST official middleware release notes (v5.4.1 2026-05-08):
  https://github.com/STMicroelectronics/stm32-mw-usbpd-core/blob/main/Release_Notes.md
- STM32G071 EPR Sink example: a community user posted successful 24 V
  EPR AVS traces in August 2026, including chunked capabilities and
  EPR keepalives:
  https://community.st.com/stm32-mcus-embedded-software-32/epr-request-in-stm32g071-164346
- X-CUBE-TCPP reference projects:
  https://github.com/STMicroelectronics/x-cube-tcpp

## Required implementation before enabling 28 V

1. **HW audit:** Check the entire Type-C VBUS path (TVS/clamp and OVP
   thresholds, FETs, ESD protector, MLCCs, connectors, PCB spacing and
   conversion stages) is safe for 28 V and transients. The firmware's
   32 V ceiling alone is not a hardware rating. Check 5 A e-marked EPR
   cable; the Lenovo C140 may restrict 28 V based on AC input voltage.
2. **Matched middleware upgrade:** Replace the old core headers and
   library *together*, using a single ST release, with a Cortex-M0+
   `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib` (not the SPR-only or MINSNK
   library). Update Keil preprocessor macro to `USBPDCORE_LIB_PD3_FULL`,
   the library path, and adjust DPM/Device source API compatibility.
   Rebuild and check ROM/RAM fit on STM32G071CB.
3. **DPM configuration:** Leave the port Sink-only; set
   `DPM_Settings[0].PE_PD3_Support.d.Is_EPR_Supported_SNK =
   USBPD_TRUE` only with a matching EPR FULL core. Preserve
   `PE_SpecRevision = USBPD_SPECIFICATION_REV3`; do not change this
   to a made-up revision 3.1 enum. Expose EPR Sink Operational PDP when
   requested by the library.
4. **SPR entry RDO:** Confirm the selected SPR RDO has the EPR Capable
   bit (bit 22) set when a supporting Source advertises EPR capability
   in its 5 V Source PDO. First complete a normal SPR explicit contract.
5. **EPR entry / chunking / keepalive:** Let the FULL policy engine send
   EPR_MODE and assemble chunked EPR_Source_Capabilities. The existing
   `SysTick_Handler` already calls `USBPD_DPM_TimerCounter()`; keep it
   running at the configured 1 ms interval. Do not fake EPR by extending
   the SPR Source_Capabilities array or issuing a plain SPR Request.
6. **EPR capability selection:** Handle
   `USBPD_CORE_DATATYPE_RCV_SRC_PDO_EPR` in
   `USBPD_DPM_SetDataInfo`. The EPR-only list contains up to six PDOs,
   starting at *combined* object position 8. Filter fixed PDOs to
   `20 V < voltage < 32 V`; prefer the highest voltage and then current,
   clamp request to 3 A, and ignore AVS/PPS for the initial upgrade.
   Use the selected object position and EPR-capable RDO; provide a
   *copy* of the matching source PDO via the FULL library's
   `USBPD_CORE_DATATYPE_REQ_COPYPDO` callback.
7. **Attempt policy:** Once per physical attach, automatically attempt
   the highest permitted fixed voltage. Do not restart negotiations on
   repeated capability announcements; only Settings > PD may retry.
   Handle EPR entry failure and Hard Reset without rapid retries, and
   retain 20 V SPR or DC input fallback as appropriate.
8. **Verify:** Start with an EPR-capable 5 A e-marked USB-C cable and
   a power path disconnected from the induction-heater switching stage.
   Capture Source_Capabilities (SPR), EPR_MODE Enter / ACK / Succeeded,
   chunked EPR_Source_Capabilities, EPR_REQUEST with copied PDO, Accept,
   PS_RDY, and periodic KeepAlive. Check VBUS with calibrated external
   instrumentation before enabling any load.

## Lenovo Legion C140

Available third-party testing reports SPR fixed 5/9/12/15/20 V,
EPR fixed **28 V / 5 A**, PPS 5-21 V and AVS 15-28 V. **20 V / 7 A is
Lenovo proprietary**; IH01A must not use a 7 A ordinary PD RDO.
Some C140 versions require 200-240 Vac input for 28 V mode.

## Explicit non-goal of the current commit

The existing 20 V working firmware remains the build default. The
EPR selector alone is not a deployable 28 V upgrade. Do not enable
28 V solely by defining `USBPDCORE_EPR`, replacing `PD_INPUT_SPR_MAX_MV`,
or modifying the SPR PDO array.
