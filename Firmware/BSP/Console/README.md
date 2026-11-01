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
