/* Host-only SPR/EPR PDO selection regression; no HAL/USBPD binary needed. */
#include "algorithm/pd_select.h"
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
    uint8_t pos=0u;uint32_t mv=0u,ma=0u;
    pdo[0]=fixed(5000,1500);
    pdo[1]=fixed(9000,3000);
    pdo[2]=fixed(20000,5000);
    pdo[3]=fixed(12000,3000);
    pdo[4]=fixed(28000,3000); /* outside SPR selection scope */
    pdo[5]=(3u<<30u)|(110u<<17u)|(33u<<8u)|60u; /* PPS */
    pdo[6]=fixed(32000,3000); /* invalid/nonstandard */
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    /* 20V/5A PDO: the request follows the PDO up to IH01A_PD_SPR_MAX_MA. */
    assert(pos==3u && mv==20000u && ma==5000u);
    /* SW3538-style 20V/7A Fixed PDO: directly use declared current. */
    pdo[2]=fixed(20000u,7000u);
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    assert(pos==3u && mv==20000u && ma==7000u);
    /* A higher advertised current is clamped to board's 7A ceiling. */
    pdo[2]=fixed(20000u,8000u);
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    assert(pos==3u && mv==20000u && ma==7000u);
    assert(PD_Input_BestFixedLimited((const uint8_t *)pdo,sizeof(pdo),
                                      15000u,&pos,&mv,&ma));
    assert(pos==4u && mv==12000u && ma==3000u);
    assert(PD_Input_BestFixedLimited((const uint8_t *)pdo,sizeof(pdo),
                                      5000u,&pos,&mv,&ma));
    assert(pos==1u && mv==5000u && ma==1500u);
    assert(!PD_Input_BestFixedLimited((const uint8_t *)pdo,sizeof(pdo),
                                       4000u,&pos,&mv,&ma));
    /* A 3A Source PDO naturally limits the requested current to 3A. */
    pdo[2]=fixed(20000,3000);
    assert(PD_Input_BestFixed((const uint8_t *)pdo,sizeof(pdo),
                             &pos,&mv,&ma));
    assert(pos==3u && mv==20000u && ma==3000u);
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
    /* SPR-only adapter, no EPR advertised: select the highest legal Fixed. */
    {
        uint32_t only_spr[5]={fixed(5000u,3000u),fixed(9000u,3000u),
            fixed(15000u,3000u),fixed(12000u,3000u),0u};
        assert(PD_Input_BestFixed((const uint8_t *)only_spr,sizeof(only_spr),
                                   &pos,&mv,&ma));
        assert(pos==3u && mv==15000u && ma==3000u);
        only_spr[4]=fixed(20000u,3000u);
        assert(PD_Input_BestFixed((const uint8_t *)only_spr,sizeof(only_spr),
                                   &pos,&mv,&ma));
        assert(pos==5u && mv==20000u && ma==3000u);
    }
    /* Extended caps include seven SPR slots, followed by EPR position 8. */
    {
        uint32_t epr[10]={0u};
        uint32_t chosen=0u;
        uint8_t j;
        for(j=0u;j<7u;++j)epr[j]=fixed(5000u,3000u);
        epr[7]=fixed(28000u,5000u);
        epr[8]=(3u<<30u)|(1u<<28u)|(280u<<17u)|(150u<<8u)|140u; /* AVS */
        epr[9]=fixed(36000u,5000u); /* above strict 32V ceiling */
        assert(PD_Input_BestEPRFixed((const uint8_t *)epr,sizeof(epr),
                                     &pos,&mv,&ma,&chosen));
        assert(pos==8u && mv==28000u && ma==5000u);
        assert(chosen==epr[7]);
        epr[9]=fixed(30000u,5000u); /* nonstandard above desired 28V */
        assert(PD_Input_BestEPRFixed((const uint8_t *)epr,sizeof(epr),
                                     &pos,&mv,&ma,&chosen));
        assert(pos==8u && mv==28000u && ma==5000u);
        epr[7]=0u;
        assert(!PD_Input_BestEPRFixed((const uint8_t *)epr,sizeof(epr),
                                      &pos,&mv,&ma,&chosen));
        assert(!PD_Input_BestEPRFixed((const uint8_t *)epr,25u,
                                      &pos,&mv,&ma,&chosen));
        assert(!PD_Input_BestEPRFixed((const uint8_t *)epr,28u,
                                      &pos,&mv,&ma,&chosen));

    }
    puts("pd selector: PASS");
    return 0;
}
