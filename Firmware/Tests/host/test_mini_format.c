/* Host unit tests for the unmodified PB02B mini_format core.
 * Does not need STM32 hardware, semihosting, UART or an RTOS. */
#include "mini_format.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    char text[64];
    char small[5];
    int n;

    n=mini_snprintf(text,sizeof(text),"%s %05d %04X","PD",-12,0x2A);
    assert(n==13);
    assert(strcmp(text,"PD -0012 002A")==0);

    n=mini_snprintf(text,sizeof(text),"%-5s:%u:%x:%%","UI",42u,255u);
    assert(n==13);
    assert(strcmp(text,"UI   :42:ff:%")==0);

    n=mini_snprintf(small,sizeof(small),"123456");
    assert(n==6);
    assert(strcmp(small,"1234")==0);

    n=mini_snprintf(text,sizeof(text),"%s", (const char *)0);
    assert(n==6);
    assert(strcmp(text,"(null)")==0);
    puts("mini_format: PASS");
    return 0;
}
