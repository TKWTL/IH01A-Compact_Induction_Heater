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
    {
        uint16_t arr;
        assert(!ConvPhase_ArrFromKHz(149u,&arr));
        assert(!ConvPhase_ArrFromKHz(401u,&arr));
        assert(ConvPhase_ArrFromKHz(185u,&arr));
        assert(!ConvPhase_ArrFromKHz(200u,0));
        assert(ConvPhase_ArrFromKHz(200u,&arr) && arr==639u);
        assert(ConvPhase_KHzFromArr(arr)==200u);
        assert(ConvPhase_ArrFromKHz(180u,&arr) && arr==710u);
        assert(ConvPhase_KHzFromArr(arr)==180u);
        assert(ConvPhase_ArrFromKHz(300u,&arr) && arr==426u);
        assert(ConvPhase_KHzFromArr(arr)==300u);
        assert(ConvPhase_ArrFromKHz(150u,&arr) && arr==852u);
        assert(ConvPhase_KHzFromArr(arr)==150u);
        assert(ConvPhase_ArrFromKHz(400u,&arr) && arr==319u);
        assert(ConvPhase_KHzFromArr(arr)==400u);
        assert(ConvPhase_ArrFromKHz(391u,&arr));
        assert(ConvPhase_KHzFromArr(arr)==391u);
        assert(ConvPhase_Calculate(arr,91u,&c));
        assert(c.angle_deg==91u && c.ccr4<=arr);
        /* For every UI frequency and 10deg angle, compare values stay
         * within the newly loaded ARR, including CH4 internal edge. */
        for(unsigned khz=150u;khz<=400u;khz+=1u)
        {
            assert(ConvPhase_ArrFromKHz((uint16_t)khz,&arr));
            for(unsigned angle=0u;angle<=180u;angle+=10u)
            {
                assert(ConvPhase_Calculate(arr,(uint16_t)angle,&c));
                assert(c.ccr1==arr/2u);
                assert(c.ccr3<=arr/2u);
                assert(c.ccr4==c.ccr3+c.ccr1);
                assert(c.ccr4<=arr);
            }
        }
    }
    printf("phase: PASS\n");
    return 0;
}
