/* Pure SPR/EPR PDO selection; NO request encoding here.
 * All requests are encoded through ST FULL's USBPD_SNKRDO_TypeDef
 * by pd_input.c, never by handmade bit shifts.
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
