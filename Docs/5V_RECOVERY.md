# IH01A independent 5V recovery profile

This is a **temporary diagnostic branch** `debug/5v-recovery`, NOT
the default production `main`. Never turn on the induction heater
while using this firmware. Keep external power stage disconnected from
its load while debugging if possible.

Purpose: recover USB-UART and SSD1315 display debugging when USB-PD
Type-C VBUS resets during Keil stepping/halt.

## What is intentionally disabled

- `MX_UCPD2_Init()`, `MX_USBPD_Init()` and `USBPD_DPM_Run()`:
  no Sink Request is sent; the passive external CC Rd remains present.
- `MX_ADC1_Init`, `MX_TIM6_Init`, `ConvController_Init`:
  20kHz ADC/ISR disabled, temperature dashboard shows waiting/fault.
- **LSE oscillator wait**, `MX_RTC_Init()`, and `Time_Init()`:
  RTC/calendar omitted entirely to eliminate an unbounded LSE-ready
  wait before UART initialization. `TIME_Millis()` still uses SysTick.
  RTC-based uptime/calendar in Settings may show unavailable.
- `MX_TIM1_Init`, `MX_TIM15_Init`, `MX_COMP1_Init`,
  `MX_COMP2_Init`, `MX_DAC1_Init`, `MX_I2C1_Init`:
  unnecessary stages skipped during early recovery. PWM_EN remains low.

Enabled: GPIO, DMA1, RTC, SPI1, USART1 RX/TX DMA, CRC, TIM14, Time,
APP coroutines and SSD1315/MiaoUI. APP_Tasks_RunOnce() is invoked from
the local `while(1)` instead of USBPD_DPM_UserExecute().

The USB-C source *should* provide default 5V using the external Rd;
**measure VBUS**. This firmware never requests 20V or 28V, and does
not supply its own 3.3V. If source had previously established a 20V
contract, disconnect the cable and let VBUS discharge before reconnecting.

## Quick test

1. Disconnect the C140 and allow VBUS to discharge. Use a USB
   current-limited 5V supply or reconnect C140 once under 5V default
   with the passive Rd. Do not feed arbitrary 3.3V into an active
   regulator rail.
2. Fetch and build branch `debug/5v-recovery` in Keil and **Rebuild**;
   program over SWD using Connect Under Reset if necessary.
3. Power on; observe PB6 with scope/logic analyzer.
   Expected `U1OK\r\n` and `[RECOVERY] USBPD OFF`.
   USART1 PB6 TX and PB7 RX, 115200 8N1; use separately powered
   USB-UART 3.3V logic and common GND; do NOT connect UART VCC.
4. The OLED should appear after a 250ms software delay, assuming its
   external VCC is present and its reset is released. If not, halt
   under stable 5V and read `g_recovery_ui_stage`:
   0=UI coroutine not started; 1=waiting 250ms; 2=inside
   `diapInit()/SSD1315_Display_Init`; 3=display init returned;
   4=MiaoUI setup returned.
5. UART initializes **immediately after GPIO/DMA**, before RTC/SPI,
   so a hang during later peripheral setup will not suppress `U1OK`.
   If PB6 stays high, inspect `g_uart_boot_stage`:
   0=before USART setup; 1=enter USART init;
   2=USART init returned; 3=DebugUART_Init returned;
   4=enter main loop; 5=APP_Tasks_RunOnce runs.
   Inspect `g_uart_probe_result` (0 HAL_OK, 0xffffffff not reached).
   Also inspect `g_recovery_init_stage`: 0=before recovery init;
   1=UART complete; 2=SPI initialized; 3=CRC/TIM14 initialized;
   4=RTC/Time_Init intentionally skipped; 5=APP_Tasks_Init returned.
6. If both OLED and UART work in this profile but fail in main,
   investigate the USB-PD negotiation and the **blocking SSD1315
   u8g2 initialization during USBPD_DPM_UserExecute**. This code
   performs HAL_Delay and SPI DMA completion waits inside the
   cooperative PD execution context.

Once baseline is stable, make ONE change at a time. Do not treat
the 5V diagnostic firmware as a working PD implementation.

## Second recovery update: cold start and OLED throughput

- Removed the unbounded LSE oscillator-ready wait from the recovery
  `SystemClock_Config()`, and skipped RTC initialization entirely.
  A cold-start LSE problem can prevent USART1 from being reached at all.
- Replaced `HAL_Delay(1)` after every scheduler pass with `__WFI()`:
  the loop now wakes on DMA/SysTick interrupts, permitting the next
  SSD1315 page transition as soon as the interrupt completes.
- A heartbeat every 3s reports cumulative UI iterations,
  display frame count and display flush requests.
- Terminal command `status` reports the same counters plus init phases.
  A 128x64 SSD1315 frame is 8 pages, 16 DMA transactions. If
  `flush` increases but `frames` remains fixed, investigate
  DMA1 Ch1/SPI1 completion IRQs and D/C/CS. If `frames` increases
  regularly but no pixels appear, inspect external OLED VCC and
  reset wiring. If UI updates stop, inspect PM state and OLED init.

No conclusion has yet been drawn about the charger or the real board:
these changes are instrumented experiments, not a verified hardware fix.

## New OLED transport counters and reset interpretation

Terminal commands: `help`, `ping`, `uart`, `status`.
`status` prints `oled_ready`, cumulative `frames` and `flush`, plus:
- `SPI init_err`: failed startup DMA transfers/aborts;
- `start_err`: HAL_SPI_Transmit_DMA errors;
- `busy`: driver tried to start a DMA while busy;
- `irq_err`: HAL_SPI_ErrorCallback count;
- `timeout`: no DMA completion within the 50ms runtime watchdog.

Two `status` readings about 5 seconds apart give actual
`(frame_count_2-frame_count_1)/5` FPS. The 3s recovery heartbeat also
shows the count without any UART RX input.

STM32G071 CMSIS maps RCC->CSR `0x1C000000` to
`PINRSTF | PWRRSTF | SFTRSTF`: pin, power and software reset flags.
These may accumulate until RMVF clears them, so they do NOT by
themselves establish USB-PD Hard Reset or a 3.3V brownout.

Power-up test sequence: physically disconnect the source so a
previous high-voltage contract is cleared, check default VBUS ~5V,
MCU 3.3V and **external** OLED VCC, then power on without entering
Keil Debug. Observe whether `U1OK` and the 3s heartbeat appear.
If cold startup remains silent even after skipping LSE, use
SWD "Connect under Reset" and inspect PC/HardFault and rails.
Keil's Debug "Run" may resume a halted CPU that is not configured
to run after flash download; it is not equivalent to an independent
power-on run. Confirm flash configuration "Reset and Run" separately.

The 5V branch is not a fix for 20V/EPR PD negotiation: the main branch
still needs a nonblocking OLED startup design for reliable PD deadlines.

## 2026-11-01 CubeMX transport & coroutine refactor

This branch incorporates the Core I/O ownership refactor from main:
- UART DMA, 512B TX and RX software rings and USART IRQ callbacks reside
  in `Core/Src/usart.c` USER CODE; the old standalone `debug_uart.c/h`
  are removed. Public UART API lives in `Core/Inc/usart.h`.
- SPI HAL TxDMA, completion/error/async-abort callbacks reside in
  `Core/Src/spi.c` USER CODE. `ssd1315_display.c` contains only
  SSD1315 command sequencing and application state.
- `diapInit()` begins SSD1315 initialization but **returns immediately**;
  the UI coroutine yields until `SSD1315_Display_InitState()` reports
  READY (or FAILED). No `HAL_Delay` or busy-wait is used in OLED startup.

Recovery variable `g_recovery_ui_stage` meanings are now:
1=entered coroutine; 2=async SSD1315 initialization pending;
3=SSD1315 initialization READY, preparing MiaoUI;
4=MiaoUI setup completed; 6=OLED initialization failed.
For an OLED failure, `status` prints SPI `init_err`, `start_err`,
`busy`, `irq_err` and `timeout` counts.

This remains 5V-only diagnostics; it is **not** production firmware
and does not request USB PD 20V or 28V. Source power, OLED external
VCC, MCU 3.3V and NRST must be checked on the actual board.

## Reset-only failure: diagnostic HSI16 firmware

Observed 2026-11-01: when manually stepping after debugger reset,
UART `U1OK` and 3-second OLED heartbeat operate correctly for
at least 24s. Direct hardware/software reset may show no UART or OLED.
This points to a pre-UART boot failure or debugger/power-up interaction;
SPI DMA/UI cannot be blamed on the successful run alone.

This diagnostic revision introduces
`IH01A_RECOVERY_CLOCK_HSI_ONLY=1` in
`Core/Inc/ih01a_recovery_profile.h`.
**Only in the 5V recovery build**, use reset-default 16MHz HSI
instead of setting Flash 2-wait-state and the 64MHz PLL.
LSE/RTC, USBPD and high-rate ADC remain OFF. USART peripheral
prescalers adapt to 16MHz via HAL; SPI throughput may fall during
this experiment. Do NOT use this diagnostic setting for performance
comparison or production use.

If a cold-reset works reliably on HSI16, while the prior PLL version
does not, inspect oscillator/Flash latency switching and power
startup before concluding about any USB-PD or OLED fault. To
recheck the hypothesis, flip only this macro to 0 and rebuild.

The globally observable `g_recovery_early_stage` Watch variable
is set at:
1 main entered; 2 HAL_Init returned; 20 HSI setup entry;
21 HSI ready timeout; 22 SYSCLK-switch timeout; 23 SysTick failure;
3 SystemClock_Config returned; 4 SRAM vector relocation complete;
5 GPIO done; 6 DMA/NVIC ready; 7 UART probe returned.
The variable can remain 0 if startup stalls in assembly/C runtime
before `main`; it can be inspected ONLY if SRAM contents are
preserved when the debugger attaches (do not trigger an extra reset).

On exception, `g_recovery_fault_code`: E1=HardFault,
E2=Error_Handler, E3=NMI. `g_recovery_fault_from_stage`
captures the early phase as of the exception. Breakpointing a
fault handler changes timing and should be performed after the
initial reset-only trial.

**Test order:**
1. Record current C140 5V VBUS, MCU 3.3V, and NRST while toggling
   power. Check external OLED VCC separately; power-source
   detach/reattach is not equivalent to a Keil software reset.
2. Rebuild/download HSI-only recovery firmware; exit Keil debug
   session and physically unplug/replug to get a true cold start.
   Also try reset-button/NRST without power loss.
3. For a failed reset, **Connect without Reset**, halt immediately,
   read PC/SP, `g_recovery_early_stage`,
   `g_recovery_fault_code`, `g_recovery_fault_from_stage`,
   `RCC->CR`, `RCC->CFGR`, `SCB->VTOR`. Do not step or
   rerun before taking the snapshot.
4. If HSI-only also fails, prioritize BOOT0/option bytes,
   POR/BOR, NRST rise time, supply loading/backfeeding, flash
   execution and flash-vector/stack integrity rather than PLL.
5. This branch is based on the remote recovery head;
   a user-owned local .uvoptx commit may need rebase after fetch,
   but must never be dropped or overwritten.

## 2026-11-01: restore PLL64 + no-reset debug attachment preset

- 5V recovery now sets `IH01A_RECOVERY_CLOCK_HSI_ONLY=0`:
  normal HSI->PLL 64MHz system clock is restored, while RTC/LSE,
  USBPD CAD/PE, TIM6/ADC and heater PWM remain disabled.
- `uvprojx` disables Utilities -> **Update Target before Debugging**.
- `uvoptx` disables **Load Application at Startup** and **Run to main()**
  for target and simulator. These avoid auto-download and automatically
  running to main, which would destroy the failure state.
- **Driver-specific connection policy still requires one manual check**:
  with Keil closed before updating XML, open
  Project -> Options for Target -> Debug -> Settings -> Debug;
  select **Connect: Normal**, uncheck **Reset after Connect**.
  **Do not select Under Reset or with Pre-reset**. Save the dialog.
  Keil persists this policy as opaque `CMSIS_AGDI` (or ST-Link) registry
  command parameters in `uvoptx`; they are deliberately not guessed/
  altered by an external editor. Entering the debugger is not proven
  no-reset until this checkbox is verified in the IDE.
- No-debug-entry-download means the new firmware must be built and
  flashed separately *before* reproducing the cold-boot failure. Any
  explicit flash download can reset or disturb the MCU. To capture a
  failed reset, do not reflash first; connect normally, halt, read PC,
  SP, g_recovery_early_stage and g_recovery_fault_code.
- The existing Keil target cannot be assumed to have code-symbol
  association on attach because automatic Load Application is disabled.
  Keep .axf from the exact flashed build. Restore symbol loading through
  a non-programming debugger method only after checking Keil's behavior;
  avoid `LOAD` commands that may program target Flash.
