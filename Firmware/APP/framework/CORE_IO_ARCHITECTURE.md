# IH01A Core I/O ownership & coroOS integration

## Ownership rule (CubeMX-compatible)

All concrete HAL I/O starts, DMA buffers and IRQ completion callbacks belong
in the matching CubeMX **USER CODE** region:

| Peripheral | Low-level implementation | Public declarations | Higher-level caller |
|---|---|---|---|
| USART1 PB6/PB7 DMA1 Ch5/4 | `Core/Src/usart.c` | `Core/Inc/usart.h` | `BSP/Console/console_cli.c`, `mini_stdio.c`, application logging |
| SPI1 PA15/PB3/PB5 DMA1 Ch1 | `Core/Src/spi.c` | `Core/Inc/spi.h` | `BSP/Display/ssd1315_display.c` |
| I2C1 PB8/PB9 (future) | `Core/Src/i2c.c` | `Core/Inc/i2c.h` | future EEPROM/sensor state machine |

`Core/Src/usart.c` now owns the **512B TX and 512B RX rings**,
128B circular DMA receive work buffer, UART HT/TC/IDLE callbacks,
TX DMA segment completion and nonblocking queue operations.
The former standalone `debug_uart.c/h` have been removed from the
repository and from Keil.

The SPI1 transport is a single-owner transaction primitive:
```c
uint8_t SPI1_DmaTxStart(const uint8_t *bytes, uint16_t count);
SPI1_DmaState SPI1_DmaTxState(void);
uint8_t SPI1_DmaTxRelease(void);
uint8_t SPI1_DmaTxAbortAsync(void);
```
The caller retains the source buffer until DONE, calls Release
before starting the next transfer. On ERROR/timeout, request an
asynchronous abort and wait until the state returns to IDLE.
Application code must never modify D/C or re-use a DMA source while BUSY.

## Nonblocking SSD1315

`SSD1315_Display_Begin(screen)` sets up a u8g2 framebuffer but does
**not** perform `u8g2_InitDisplay` (whose u8x8 byte/delay calls are
synchronous). Instead, `SSD1315_Display_Service()` starts and polls
one SPI DMA command/page at a time.

The exact init command sequence is retained:
`AE 8D 10 20 02 81 7F A1 C8 A8 3F D3 00 40 DA 12 D5 80 D9 22 DB 30 A4 A6 AF`.
The critical external-VCC setting `8D 10` keeps the charge pump
**disabled**. The OLED's RES# remains tied to MCU NRST.

The application coroutine uses:
```c
THRD_DELAY(250u); /* yield for external OLED VCC / reset stabilization */
if(!diapInit())
{
    /* report failure and yield, never spin */
}
THRD_UNTIL(SSD1315_Display_InitState() != SSD1315_INIT_PENDING);
if(SSD1315_Display_InitState() == SSD1315_INIT_READY)
    MiaoUi_Setup(&s_ui);
```

No `HAL_Delay()`, synchronous `HAL_SPI_Transmit()`, or SPI-DMA
busy-wait is allowed inside the coroutine or SSD1315 driver.
The one-off `U1OK` UART boot probe still uses a bounded HAL UART
polling call **before** the cooperative scheduler starts. The ST
USBPD middleware's `USBPD_DPM_WaitForTime` callback is a separate
vendor-stack delay hook, not a coroOS application routine.

## Scope and verification

- Existing SPR 20V/3A policy is unchanged; these transport changes
  do not enable EPR. Power PWM stays disabled.
- SPI driver moves only transport operations. Framebuffer, UI,
  paged SSD1315 command sequencing and OLED status belong outside Core.
- The `debug/5v-recovery` branch also retains 5V-only operation,
  UART boot markers and frame/status counters.
- Regenerating CubeMX should preserve all `USER CODE` sections;
  ensure the MDK project still includes exactly one each of
  `Core/Src/usart.c` and `Core/Src/spi.c`.
- Hardware validation and a full Keil ARMCLANG build are still
  required; no success on the user's board is assumed.
