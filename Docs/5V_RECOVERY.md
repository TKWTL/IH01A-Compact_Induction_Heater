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
   1=UART complete; 2=RTC initialized; 3=SPI initialized;
   4=CRC/TIM14 initialized; 5=Time_Init returned;
   6=APP_Tasks_Init returned.
6. If both OLED and UART work in this profile but fail in main,
   investigate the USB-PD negotiation and the **blocking SSD1315
   u8g2 initialization during USBPD_DPM_UserExecute**. This code
   performs HAL_Delay and SPI DMA completion waits inside the
   cooperative PD execution context.

Once baseline is stable, make ONE change at a time. Do not treat
the 5V diagnostic firmware as a working PD implementation.
