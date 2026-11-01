# IH01A USART1 DMA diagnostics (2026-11-01)

Reference: PB02B `firmware/Bootstrap/src/bsp_usart.c` (DMA TX ring),
adapted for the nonblocking STM32G071 USB-PD scheduler. PB02B's
busy-wait-on-full behavior is deliberately NOT imported.

## Resources

- USART1 **PB6 TX / PB7 RX**, 115200 baud, 8 data bits, no parity, one stop bit.
- DMA1 **Channel 5 TX normal**, DMA1 **Channel 4 RX circular**.
- Static **512 byte software TX ring** + **512 byte software RX ring**.
  One byte is reserved by the empty/full convention (511 byte payload
  capacity). **128 byte staging DMA RX buffer**, used only by HAL's
  circular ReceiveToIdle acquisition. No heap allocations.
- UART **IDLE**, DMA half-complete and DMA transfer-complete callbacks
  each commit only the newly received segment; the full-length
  callback position 128 is retained distinctly to avoid TC/IDLE
  duplicate copying.
- USART1 IRQ priority 1 == DMA1_Ch4_7 IRQ priority 1; prevents RX IDLE
  event code being preempted by DMA HT/TC on this Cortex-M0+ while
  preserving higher priority UCPD events. Keep them equal on
  CubeMX regeneration.
- `DebugUART_WriteByte` never waits on a full ring: it increments
  `tx_dropped`. An RX overflow increments `rx_dropped`.
- One DMA TX segment starts from `DebugUART_Service`; the HAL UART
  TX-complete interrupt only releases the completed segment, and the
  next foreground pass initiates the next one.
- `HAL_UART_ErrorCallback` records errors; foreground service restarts
  the ReceiveToIdle engine when the HAL RxState is READY.

## Logs and commands

Runtime `printf`/`puts`/fputc are mirrored to the existing 128-byte
debug RAM history **and** the UART TX queue. Use the installed lightweight
integer formatter; floating point and precision formats are unsupported.
Connect a **3.3V-level USB-UART adapter**, share GND, cross TX/RX,
and do not attach its 5V output to the board.

Startup example:
```text
[BOOT 0] IH01A USART1 DMA+IDLE 115200 8N1 flags=0x... 
[UART] TX ring=512 RX ring=512 DMA RX staging=128; type help
[PD 123] ATTACH a=1 b=0x00000000
[PD 128] SPR_CAPS a=6 b=0x???????? 
[PD 130] REQUEST a=20000 b=0x5104B12C
[PD 142] NOTIFY a=<USBPD_NOTIFY_* enum value> b=0x00000000
[PD 175] CONTRACT a=1 b=0x...
```
Event names: ATTACH / DETACH / SPR_CAPS / REQUEST /
CONTRACT / HARD_RESET / NOTIFY / MANUAL. Fields are numeric;
`SPR_CAPS.b` is the raw **first (5V) PDO** (EPR-capability bit 23);
`REQUEST.a` is requested mV, `REQUEST.b` is raw RDO;
`HARD_RESET.a` is the ST hard-reset status;
`NOTIFY.a` is the numeric ST `USBPD_NOTIFY_*` event.
Exact enumeration values depend on the ST header in this repository.

Type commands terminated with Enter:
- `help`: command list
- `ping`: verify DMA RX + IDLE and DMA TX
- `pd`: contract state, current ADC-measured VBUS and RDO selection
- `uart`: TX/RX dropped bytes, RX error count and IDLE count

The CLI consumes at most 16 received bytes per application pass to keep
the PD policy engine responsive. PD callbacks only record compact
binary diagnostics; formatting/transmission occur in
`APP_Tasks_RunOnce()`. Logs may be truncated when VBUS power is lost.
Keep the USB-UART bridge powered independently so it captures characters
sent **before** the board resets.

**This change does not enable EPR.** The project remains linked to the
PD3 `MINSNK` stack; it selects 20V SPR and clamps current to 3A.

## 2026-11-01 无输出的启动诊断

先前 `DebugUART_Init()` 只把启动字符串入队，TX DMA 要等到由 PD 主循环回调的 `DebugUART_Service()` 才启动；如果主循环未运行，PB6 会持续保持 UART 空闲高电平。

现在 `DebugUART_Init()` 在 `MX_USART1_UART_Init()` 之后、`MX_USBPD_Init()` 之前，先用 HAL **短暂阻塞地直接发送** `U1OK\\r\\n`（6 字节，理想线路时间约 0.52ms；只在启动阶段），然后入队原启动日志，并**立刻 kick TX DMA**。此操作不发生在任何 PD 通信回调内。若没有任何启动脉冲，请先用示波器在 MCU PB6 实际引脚测量，确认固件确实烧录、USART 初始化运行到该位置，并检查 GPIO 复用及供电。串口空闲时 PB6 为高是正常的。

Keil Watch 调试变量：
- `g_uart_boot_stage`：1=即将初始化 USART1，2=初始化结束，3=DebugUART_Init 返回，4=即将进入 USBPD_DPM_Run，5=执行到应用主循环。
- `g_uart_probe_result`：早期直接发送 HAL 状态（0=HAL_OK，其他值表示失败；初值 0xFFFFFFFF 表示尚未执行）。
- `uart` 命令现在提供 `tx_start`、`tx_done`、`tx_fail`、`boot`、`probe`，可区分 DMA 未启动、完成回调未触发、环满和 RX 错误。

如果仅有 `U1OK` 而没有后续 `[BOOT]`，检查 DMA1 Channel5、共享 Ch4-7 DMA IRQ、USART1 TC 中断与 DebugUART_Service 的运行。如果两者都有而 `ping` 无响应，则重点查 PB7 RX、USART1 IDLE、DMA1 Channel4、是否发送 CR/LF。

## CubeMX ownership and coroutine discipline (2026-11-01)

**USART1**: All HAL UART RX/TX, DMA callbacks, 512-byte TX/RX
ring buffers, startup probe, and error counters live exclusively in
`Core/Src/usart.c` USER CODE regions. Declarations are in
`Core/Inc/usart.h` USER CODE regions. There is no separate
`BSP/Console/debug_uart.c/h` source or project entry.
`BSP/Console/console_cli.c` remains application-side command parsing,
and `mini_stdio.c` remains formatting. Their common transport API is
`DebugUART_*` (names preserved for compatibility).

**SPI1**: DMA HAL calls and HAL_SPI completion/error/abort callbacks
live exclusively in `Core/Src/spi.c` USER CODE regions, using
`SPI1_DmaTxStart`, `SPI1_DmaTxState`,
`SPI1_DmaTxRelease` and `SPI1_DmaTxAbortAsync`.
The SSD1315 application driver only performs D/C transitions,
command/page sequencing, status polling and framebuffer management.

**I2C1**: When asynchronous sensor/EEPROM operations are added,
HAL_I2C_..._IT/DMA starts and HAL callbacks must be implemented in
`Core/Src/i2c.c` USER CODE; expose state/results in
`Core/Inc/i2c.h` USER CODE. Do not add another transport source.
No I2C data transactions are introduced by this refactor.

**CoroOS**: SSD1315 startup is now asynchronous and requires
`SSD1315_Display_Begin(screen)` followed by
`SSD1315_Display_InitState()` polling. No application coroutine
calls HAL_Delay or waits in a DMA spin-loop. Only the one-shot
UART `U1OK` boot probe is intentionally polling, before the scheduler
and PD protocol loops start. The ST USBPD library's separate
`USBPD_DPM_WaitForTime` callback is unrelated to the application
coroutine design and has not been altered.

Keep source changes inside the marked CubeMX USER CODE regions;
CubeMX may regenerate all other parts.
