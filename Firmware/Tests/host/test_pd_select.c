/* Host-only USBPD PDO/RDO regression; no HAL/USBPD binary needed. */
#include "framework/pd_select.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static uint32_t fixed(uint32_t mv,uint32_t ma)
{
    return ((mv/50u)<<10u)|(ma/10u);
}
int main(void)
{
    uint32_t pdo[7]={0u};
    uint8_t pos=0u;uint32_t mv=0u,ma=0u,rdo;
    pdo[0]=fixed(5000,1500);
    pdo[1]=fixed(9000,3000);
    pdo[2]=fixed(20000,5000);
    pdo[3]=fixed(12000,3000);
    pdo[4]=fixed(28000,3000); /* EPR: this stack cannot enter EPR */
    pdo[5]=(3u<<30u)|(110u<<17u)|(33u<<8u)|60u; /* PPS */
    pdo[6]=fixed(32000,3000); /* invalid/nonstandard */
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    assert(pos==3u && mv==20000u && ma==3000u);
    rdo=PD_Input_FixedRDO(pos,ma);
    assert((rdo>>28u)==3u);
    assert((rdo&1023u)==300u);
    assert(((rdo>>10u)&1023u)==300u);
    assert((rdo&(1u<<24u))!=0u);
    pdo[2]=fixed(15000,2000);
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    assert(pos==3u && mv==15000u && ma==2000u);
    pdo[2]=0u;
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    assert(pos==4u && mv==12000u);
    assert(!PD_Input_BestFixed((const uint8_t *)pdo,3u,&pos,&mv,&ma));
    assert(!PD_Input_BestFixed((const uint8_t *)pdo,32u,&pos,&mv,&ma));
    assert(PD_Input_FixedRDO(0u,3000u)==0u);
    assert(PD_Input_FixedRDO(1u,5000u)==
           PD_Input_FixedRDO(1u,3000u));
    /* EPR payload is separate from the seven SPR slots. */
    {
        uint32_t epr[6]={0u};
        uint32_t chosen=0u;
        epr[0]=fixed(28000u,5000u);
        epr[1]=(3u<<30u)|(1u<<28u)|(280u<<17u)|(150u<<8u)|140u; /* AVS */
        epr[2]=fixed(36000u,5000u); /* above strict 32V ceiling */
        assert(PD_Input_BestEPRFixed((const uint8_t *)epr,sizeof(epr),
                                     &pos,&mv,&ma,&chosen));
        assert(pos==8u && mv==28000u && ma==3000u);
        assert(chosen==epr[0]);
        rdo=PD_Input_EPRFixedRDO(pos,ma);
        assert((rdo>>28u)==8u && (rdo&(1u<<22u))!=0u);
        assert((rdo&1023u)==300u);
        epr[0]=0u;
        assert(!PD_Input_BestEPRFixed((const uint8_t *)epr,sizeof(epr),
                                      &pos,&mv,&ma,&chosen));
        assert(!PD_Input_BestEPRFixed((const uint8_t *)epr,25u,
                                      &pos,&mv,&ma,&chosen));
        assert(PD_Input_EPRFixedRDO(7u,3000u)==0u);
        assert(PD_Input_EPRFixedRDO(14u,3000u)==0u);
    }
    puts("pd selector: PASS");
    return 0;
}
