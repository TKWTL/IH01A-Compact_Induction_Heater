#include <assert.h>
#include <stdio.h>
#include "PowerControl/conv_phase.h"
int main(void)
{
    static const unsigned angles[]={0,30,60,90,120,150,180};
    static const unsigned ccr3[]={0,36,71,107,142,178,213};
    unsigned i;
    ConvPhaseConfig c;
    assert(!ConvPhase_Calculate(1u,90u,&c));
    assert(!ConvPhase_Calculate(426u,0u,0));
    for(i=0u;i<7u;++i)
    {
        assert(ConvPhase_Calculate(426u,(uint16_t)angles[i],&c));
        assert(c.ccr1==213u && c.ccr3==ccr3[i]);
        assert(c.ccr4==c.ccr3+213u);
        assert(c.ccr4<=426u);
    }
    assert(ConvPhase_Calculate(426u,200u,&c));
    assert(c.angle_deg==180u && c.ccr3==213u);
    assert(ConvPhase_Calculate(427u,180u,&c));
    assert(c.ccr3==213u && c.ccr4==426u);
    assert(ConvPhase_Calculate(639u,0u,&c));
    assert(c.ccr1==319u && c.ccr3==0u && c.ccr4==319u);
    assert(ConvPhase_Calculate(639u,20u,&c));
    assert(c.ccr1==319u && c.ccr3==36u && c.ccr4==355u);
    assert(ConvPhase_Calculate(639u,180u,&c));
    assert(c.ccr3==319u && c.ccr4==638u);
    printf("phase: PASS\n");
    return 0;
}
