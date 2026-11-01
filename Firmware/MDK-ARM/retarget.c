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
#include <rt_sys.h>  /* Arm Compiler 6 FILEHANDLE and _sys_* ABI declarations */
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

/*
 * Arm C library (non-MicroLIB) target-dependent system I/O.
 *
 * The ordinary Arm library falls back to semihosting unless these
 * functions are retargeted. In particular, __main -> _initio ->
 * _freopen_locked -> _sys_open previously reached BKPT 0xAB before
 * main(), causing a standalone Cortex-M0+ boot to HardFault.
 *
 * All three default standard streams use the same pseudo-console.
 * Never call printf, malloc, blocking UART HAL routines or a debugger
 * service from these implementations. During C startup the UART is not
 * initialized yet: IH_RuntimePutChar stores bytes in RAM and the UART
 * queue safely discards them until DebugUART_Init sets s_ready.
 *
 * The signatures below are checked against <rt_sys.h>.
 */
#if !defined(__MICROLIB)

FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)name;
    (void)openmode;
    return 1; /* stdin / stdout / stderr share one pseudo-terminal */
}

int _sys_close(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

/* Arm ABI: return the number of bytes NOT written, not bytes written. */
int _sys_write(FILEHANDLE fh, const unsigned char *buf,
               unsigned len, int mode)
{
    unsigned i;
    (void)fh;
    (void)mode;
    if(buf == NULL) return (int)len;
    for(i=0u; i<len; ++i)
        IH_RuntimePutChar((char)buf[i]);
    return 0; /* all bytes accepted by bounded best-effort logging */
}

/* No synchronous stdin path: commands use DebugUART_ReadByte() instead. */
int _sys_read(FILEHANDLE fh, unsigned char *buf,
              unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)len;
    (void)mode;
    return -1; /* I/O error: do not pretend an unsupported read succeeded */
}

int _sys_istty(FILEHANDLE fh)
{
    (void)fh;
    return 1; /* device is a terminal, not a seekable regular file */
}

int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1; /* unsupported */
}

long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return -1L; /* unsupported */
}

#endif /* !__MICROLIB */
