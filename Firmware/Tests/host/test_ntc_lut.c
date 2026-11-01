#include "algorithm/ntc_lut.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    int16_t t=0;
    /* Key exact table breakpoints: 20C, 40C, 90C and endpoints. */
    assert(NTC_SYS_Convert(2871,&t)&&t==200);
    assert(NTC_SYS_Convert(2061,&t)&&t==400);
    assert(NTC_POT_Convert(3782,&t)&&t==200);
    assert(NTC_POT_Convert(2064,&t)&&t==900);
    assert(NTC_SYS_Convert(3984,&t)&&t==-400);
    assert(NTC_POT_Convert(736,&t)&&t==1500);
    /* Midpoint interpolation must lie strictly inside the adjacent 5C. */
    assert(NTC_SYS_Convert(2164,&t)&&t>350&&t<400);
    assert(NTC_POT_Convert(1989,&t)&&t>900&&t<950);
    /* Safe fault reporting: still clamp, but not mark out-of-range valid. */
    assert(!NTC_SYS_Convert(4095,&t)&&t==-400);
    assert(!NTC_SYS_Convert(0,&t)&&t==1500);
    assert(!NTC_POT_Convert(4095,&t)&&t==-400);
    assert(!NTC_POT_Convert(0,&t)&&t==1500);
    assert(!NTC_SYS_Convert(2000,0));
    /* Strict monotonic inverse over the representable nominal span. */
    {
        int16_t prev=-401;
        unsigned a;
        for(a=3984u;a>=153u;--a)
        {
            assert(NTC_SYS_Convert((uint16_t)a,&t));
            assert(t>=prev);
            prev=t;
        }
    }
    puts("ntc LUT: PASS");
    return 0;
}
