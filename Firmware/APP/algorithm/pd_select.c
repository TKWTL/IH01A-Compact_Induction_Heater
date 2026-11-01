/* Pure PDO selector / RDO builder. The active MINSNK link remains SPR-only.
 * EPR helpers do not enter EPR mode; the full stack must handle entry,
 * chunking, keepalives and the copied PDO in EPR_REQUEST.
 */
#include "algorithm/pd_select.h"
#include <string.h>

uint8_t PD_Input_BestFixed(const uint8_t *data,uint32_t len,
                           uint8_t *pos,uint32_t *mv,uint32_t *ma)
{
    uint32_t i,best_v=0u,best_c=0u;
    uint8_t best_pos=0u;
    if(!data||!pos||!mv||!ma||!len||(len&3u)||len>28u)return 0u;
    for(i=0u;i<len/4u;++i)
    {
        uint32_t p,v,c;
        memcpy(&p,data+4u*i,4u);
        if((p>>30u)!=0u)continue;
        v=((p>>10u)&1023u)*50u;
        c=(p&1023u)*10u;
        if(v<4500u||v>=PD_INPUT_MAX_MV||
           v>PD_INPUT_SPR_MAX_MV||c==0u)continue;
        if(v>best_v||(v==best_v&&c>best_c))
            best_v=v,best_c=c,best_pos=(uint8_t)(i+1u);
    }
    if(!best_pos)return 0u;
    if(best_c>PD_INPUT_MAX_MA)best_c=PD_INPUT_MAX_MA;
    *pos=best_pos;*mv=best_v;*ma=best_c;
    return 1u;
}
uint32_t PD_Input_FixedRDO(uint8_t pos,uint32_t ma)
{
    uint32_t n;
    if(pos<1u||pos>7u||ma<10u)return 0u;
    if(ma>PD_INPUT_MAX_MA)ma=PD_INPUT_MAX_MA;
    n=ma/10u;
    return ((uint32_t)pos<<28u)|(1u<<24u)|(n<<10u)|n;
}

/*
 * Only the FULL EPR policy engine may use this RDO for an initial SPR
 * explicit contract. Bit 22 advertises that the sink can enter EPR mode.
 * Do NOT call it from the current MINSNK stack: its CAD/PE lacks EPR.
 */
uint32_t PD_Input_FixedRDOForEPR(uint8_t pos,uint32_t ma)
{
    uint32_t rdo=PD_Input_FixedRDO(pos,ma);
    return rdo ? (rdo | (1u<<22u)) : 0u;
}

/* EPR Source Capabilities is a combined list: SPR positions 1..7,
 * followed by EPR positions 8..13. Never treat the beginning of the
 * extended message as EPR position 8. AVS is excluded intentionally. */
uint8_t PD_Input_BestEPRFixed(const uint8_t *data,uint32_t len,
                             uint8_t *pos,uint32_t *mv,uint32_t *ma,
                             uint32_t *selected_pdo)
{
    uint32_t i,best_v=0u,best_c=0u,best_pdo=0u;
    uint8_t best_pos=0u;
    if(!data||!pos||!mv||!ma||!selected_pdo||
       (len&3u)||len<32u||len>52u)return 0u;
    for(i=7u;i<len/4u;++i)
    {
        uint32_t p,v,c;
        memcpy(&p,data+4u*i,4u);
        if((p>>30u)!=0u)continue;
        v=((p>>10u)&1023u)*50u;
        c=(p&1023u)*10u;
        if(v<=PD_INPUT_SPR_MAX_MV||v>=PD_INPUT_MAX_MV||c==0u)
            continue;
        if(v>best_v||(v==best_v&&c>best_c))
        {
            best_v=v;best_c=c;best_pdo=p;
            best_pos=(uint8_t)(i+1u);
        }
    }
    if(!best_pos)return 0u;
    if(best_c>PD_INPUT_EPR_MAX_MA)best_c=PD_INPUT_EPR_MAX_MA;
    *pos=best_pos;*mv=best_v;*ma=best_c;*selected_pdo=best_pdo;
    return 1u;
}
uint32_t PD_Input_EPRFixedRDO(uint8_t pos,uint32_t ma)
{
    uint32_t n;
    if(pos<8u||pos>13u||ma<10u)return 0u;
    if(ma>PD_INPUT_EPR_MAX_MA)ma=PD_INPUT_EPR_MAX_MA;
    n=ma/10u;
    /* Bit 22: EPR capable, bit 24: NoUSBsuspend. */
    return ((uint32_t)pos<<28u)|(1u<<24u)|(1u<<22u)|(n<<10u)|n;
}
