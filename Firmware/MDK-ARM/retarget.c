/* IH01A / STM32G071: Arm Compiler 6 C library without semihosting.
 *
 * The Cortex-M0+ product has no host console. Never let the runtime emit
 * BKPT 0xAB (_ttywrch) or depend on the debug probe for its startup.
 *
 * Keil ARMCLANG 6.24 / standard library (not MicroLIB):
 *   weak __ARM_use_no_argv avoids startup argc/argv queries at -O0,\n *   while allowing the compiler's strong definition at -O1 and above.
 *   __use_no_semihosting causes a LINK ERROR if a semihosted libc path
 *   remains (prefer a clear L6915E to a mysterious target BKPT).
 *
 * Console output is mirrored to RAM and the nonblocking USART1 DMA queue.
 * The UART may be unavailable during early boot or power loss; RAM remains
 * inspectable via the Keil Watch/Memory window. _sys_exit is a fatal exit path,
 * deliberately halted with its code preserved, NOT quietly resumed.
 */
#include <stdint.h>
#include <stdio.h>
#include "usart.h"

#if !defined(__ARMCC_VERSION) || (__ARMCC_VERSION < 6000000)
#error "IH01A retarget.c requires ARMCLANG 6 (the configured Keil toolchain)."
#endif

#if !defined(__MICROLIB)
__asm(
    ".weak __ARM_use_no_argv\n"
    "__ARM_use_no_argv:\n"
    ".global __use_no_semihosting\n"
    "__use_no_semihosting:\n"
);
#endif

/* Recent console output is a ring: read 0..g_runtime_log_count-1,
 * or for an overflow start at g_runtime_log_head (mod 128).
 * Volatile keeps it visible when paused in the debugger. */
#define IH_RUNTIME_LOG_CAPACITY (128u)
volatile uint8_t  g_runtime_log[IH_RUNTIME_LOG_CAPACITY];
volatile uint32_t g_runtime_log_head;
volatile uint32_t g_runtime_log_count;
volatile uint32_t g_runtime_exit_seen;
volatile int32_t  g_runtime_exit_code;

void IH_RuntimePutChar(char ch)
{
    uint32_t head = g_runtime_log_head;
    g_runtime_log[head] = (uint8_t)ch;
    g_runtime_log_head = (head + 1u) & (IH_RUNTIME_LOG_CAPACITY - 1u);
    if(g_runtime_log_count < IH_RUNTIME_LOG_CAPACITY)
        ++g_runtime_log_count;
    DebugUART_WriteByte((uint8_t)ch);
}

/* Runtime diagnostics from assert/exit may reach _ttywrch directly. */
void _ttywrch(int ch)
{
    IH_RuntimePutChar((char)ch);
}

/* A printf/putchar future call records the characters without transmitting
 * them or blocking the USB-PD cooperative scheduler. */
int fputc(int ch, FILE *stream)
{
    (void)stream;
    IH_RuntimePutChar((char)ch);
    return ch;
}

/* Termination must stay observable and safe. Do not automatically restart
 * the heater after an unexpected C runtime fatal exit. */
void _sys_exit(int return_code)
{
    g_runtime_exit_code = (int32_t)return_code;
    g_runtime_exit_seen = 1u;
    for(;;) { /* inspect g_runtime_* and caller stack in the debugger */ }
}
