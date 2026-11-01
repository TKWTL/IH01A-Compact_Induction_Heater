# C140 5V -> 20V reset diagnosis: UART log bring-up

1. Connect PB6 (USART1 TX) to a 3.3V USB-UART adapter RX, PB7 to
   adapter TX, plus common GND. Do **not** connect adapter +5V/VCC to
   the board. Terminal: **115200 8N1**.
2. Pull `main`, perform a full Keil Rebuild and program the firmware.
   The first line prints captured RCC->CSR reset flags. RCC reset
   flags are cleared once at boot, so the *next* boot can distinguish
   POR/BOR from pin/software/watchdog reset.
3. Input `ping` followed by Enter. Expect `[CLI] pong`; this tests
   UART RX circular DMA, IDLE and TX DMA. Use `uart` to check
   dropped bytes and errors.
4. Connect the C140 with heater PWM disabled. Capture the
   `SPR_CAPS`, `REQUEST`, `NOTIFY`, `CONTRACT` and
   `HARD_RESET` lines, especially **before** any 1 second VBUS gap.
   `REQUEST.a` is mV, `REQUEST.b` is the RDO in hexadecimal;
   e.g. expected normal 20V/3A SPR Fixed request
   `REQUEST a=20000 b=0x5104B12C` when PDO5 is 20V.
   The 5V source PDO bit 23 advertises EPR support, which is **not**
   a sign that the currently linked MINSNK firmware has entered EPR.
5. If the board resets, compare first/second boot reset flags and
   observe external VBUS, 3.3V and NRST on an oscilloscope. A source
   Hard Reset vs local brownout is **not** distinguishable from UART
   reset messages alone.
6. Collect `pd` and `uart` outputs once 20V becomes stable.
   If UART drops/events are frequent, reduce other debug output;
   there is deliberately no blocking backpressure.

The board currently has nonblocking 512B TX/RX software rings and a
128B circular receive DMA buffer. DMA1 Channel4/5, PB6/PB7 and
USART1 IRQ are configured consistently in CubeMX .ioc and Core code.

This change is diagnostic only; it does not alter the 3A request
policy or enable EPR protocol negotiation.
