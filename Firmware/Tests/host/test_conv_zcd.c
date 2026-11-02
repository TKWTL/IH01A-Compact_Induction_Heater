#include <assert.h>
#include <stdio.h>
#include "conv_zcd.h"
int main(void)
{
    ConvZcdSnapshot v={0};
    /* ARR=639, D(20deg)=36: voltage +->- zero at 159.75+18 =177.75 */
    assert(ConvZcd_Calculate(639u,20u,178u,&v));
    assert(v.valid && v.capture_raw==178u && v.lag_deci_deg==1);
    assert(ConvZcd_Calculate(639u,20u,242u,&v));
    assert(v.lag_deci_deg>=360 && v.lag_deci_deg<=362);
    assert(ConvZcd_Calculate(639u,20u,80u,&v));
    assert(v.lag_deci_deg<0);
    /* Capture near CNT=0 precedes the voltage reference near 320 by
     * approximately half a cycle (negative lag). */
    assert(ConvZcd_Calculate(639u,180u,1u,&v));
    assert(v.lag_deci_deg<0);
    assert(!ConvZcd_Calculate(639u,0u,200u,&v));
    assert(!ConvZcd_Calculate(639u,20u,640u,&v));
    puts("zcd: PASS");
    return 0;
}
