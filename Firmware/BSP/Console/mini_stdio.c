/* IH01A console. The formatter itself is copied from PB02B/Bootstrap.
 * Formats: %% %c %s %d %i %u %x %X with '-'/'0', width and one 'l'.
 * No floating point, precision, pointer specifier or dynamic allocation.
 * printf and snprintf family resolve to mini_format (not libc printf).
 * All output goes to the RAM diagnostic ring; NEVER semihosting.
 * The display, PD stack, or application must not call these from ISRs.
 */
#include "mini_format.h"
#include <stdarg.h>
#include <stdio.h>

/* ArmClang 6 stdio.h declares putchar as a function-like macro:
 *   #define putchar(c) putc((c), stdout)
 * Undefine it here before providing the actual function symbol.
 * putc() is also implemented below so callers retaining the macro
 * are redirected to the same nonblocking log sink. */
#ifdef putchar
#undef putchar
#endif

extern void IH_RuntimePutChar(char ch);

static void mini_log_sink(char ch, void *ctx)
{
    (void)ctx;
    IH_RuntimePutChar(ch);
}

int vprintf(const char *fmt, va_list args)
{
    return mini_vformat(mini_log_sink, 0, fmt, args);
}

int printf(const char *fmt, ...)
{
    int result;
    va_list args;
    va_start(args, fmt);
    result = mini_vformat(mini_log_sink, 0, fmt, args);
    va_end(args);
    return result;
}

int vsnprintf(char *dst, size_t capacity, const char *fmt, va_list args)
{
    return mini_vsnprintf(dst, capacity, fmt, args);
}

int snprintf(char *dst, size_t capacity, const char *fmt, ...)
{
    int result;
    va_list args;
    va_start(args, fmt);
    result = mini_vsnprintf(dst, capacity, fmt, args);
    va_end(args);
    return result;
}

/* ArmClang can replace constant printf("text\n") with puts(). */
int puts(const char *str)
{
    while(*str)
        IH_RuntimePutChar(*str++);
    IH_RuntimePutChar('\n');
    return 0;
}

int putchar(int ch)
{
    IH_RuntimePutChar((char)ch);
    return (unsigned char)ch;
}

/* Calls using the macro from stdio.h resolve to putc(), not putchar(). */
int putc(int ch, FILE *stream)
{
    (void)stream;
    return putchar(ch);
}

/* No file-system console. stdout and stderr are the same diagnostic ring. */
int vfprintf(FILE *stream, const char *fmt, va_list args)
{
    (void)stream;
    return mini_vformat(mini_log_sink, 0, fmt, args);
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    int result;
    va_list args;
    (void)stream;
    va_start(args, fmt);
    result = mini_vformat(mini_log_sink, 0, fmt, args);
    va_end(args);
    return result;
}
