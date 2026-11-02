/* Production PD log: a bounded event queue, decoded only in the foreground.
 * No printf, UART calls or dynamic allocation in PE callback context.
 * Original USBPD PHY trace hooks remain ABI-compatible and silent.
 */
#define IH_PD_DIAG_INTERNAL 1
#include "algorithm/pd_diag.h"
#include "algorithm/pd_source_stats.h"
#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define PD_DIAG_CAP 32u
typedef struct {
    uint32_t ms, a, b;
    uint8_t event;
} DiagEvent;
static DiagEvent s_events[PD_DIAG_CAP];
static volatile uint8_t s_head, s_tail;
static volatile uint32_t s_dropped;
static uint32_t s_last_pdo[PD_SOURCE_MAX_PDOS];
static uint16_t s_pdo_seen;
static uint32_t s_last_rdo[2], s_last_rdo_mv[2];
static uint8_t s_rdo_seen;

/* Retained for the optional application-side KeepAlive diagnostic probe.
 * Production uses the ST FULL stack KeepAlive unmodified. */
volatile uint32_t g_pd_last_keepalive_ms;

void PD_Diag_Record(uint8_t code, uint32_t a, uint32_t b)
{
    uint8_t next;
    uint8_t slot;
    if(code==PD_DIAG_RESET || code==PD_DIAG_ATTACH ||
       code==PD_DIAG_DETACH)
    {
        s_pdo_seen=0u;
        s_rdo_seen=0u;
    }
    /* Only user-relevant production records enter the queue. In particular,
     * raw TX, GoodCRC, KeepAlive, GETINFO and EPR state probes are omitted. */
    if(code!=PD_DIAG_SOURCE_PDO && code!=PD_DIAG_RDO &&
       code!=PD_DIAG_EPR_RDO && code!=PD_DIAG_RESET &&
       !(code==PD_DIAG_MANUAL && (a==3u || a==4u)))return;
    if(code==PD_DIAG_SOURCE_PDO)
    {
        uint32_t pos=a&0xFFu;
        uint16_t bit;
        if(pos<1u || pos>PD_SOURCE_MAX_PDOS || b==0u)return;
        bit=(uint16_t)(1u<<(pos-1u));
        if((s_pdo_seen&bit) && s_last_pdo[pos-1u]==b)return;
        s_last_pdo[pos-1u]=b;
        s_pdo_seen|=bit;
    }
    if(code==PD_DIAG_RDO || code==PD_DIAG_EPR_RDO)
    {
        slot=(uint8_t)(code==PD_DIAG_EPR_RDO);
        if((s_rdo_seen&(1u<<slot)) &&
           s_last_rdo[slot]==b && s_last_rdo_mv[slot]==a)return;
        s_last_rdo[slot]=b;
        s_last_rdo_mv[slot]=a;
        s_rdo_seen|=(uint8_t)(1u<<slot);
    }
    next=(uint8_t)((s_head+1u)&(PD_DIAG_CAP-1u));
    if(next==s_tail){++s_dropped;return;}
    s_events[s_head].ms=HAL_GetTick();
    s_events[s_head].a=a;
    s_events[s_head].b=b;
    s_events[s_head].event=code;
    s_head=next;
}

/* EPR-only data are positioned at objects 8..13; a combined EPR list
 * contains seven SPR slots followed by EPR objects. Copy unaligned words
 * only; call this after confirming the PE callback type/length. */
void PD_Diag_SourceCaps(const uint8_t *data, uint32_t bytes, uint8_t epr)
{
    uint8_t i, count, offset;
    if(!data || !bytes || (bytes&3u))return;
    if(epr)
    {
        if(bytes<=PD_SOURCE_MAX_EPR_PDOS*4u)
        {
            offset=0u;
            count=(uint8_t)(bytes/4u);
        }
        else if(bytes>=32u && bytes<=PD_SOURCE_MAX_PDOS*4u)
        {
            offset=PD_SOURCE_MAX_SPR_PDOS;
            count=(uint8_t)(bytes/4u);
        }
        else return;
    }
    else
    {
        if(bytes>PD_SOURCE_MAX_SPR_PDOS*4u)return;
        offset=0u;
        count=(uint8_t)(bytes/4u);
    }
    for(i=offset;i<count;++i)
    {
        uint32_t raw;
        uint32_t pos=epr && offset==0u ?
                     (uint32_t)PD_SOURCE_MAX_SPR_PDOS+i+1u : (uint32_t)i+1u;
        memcpy(&raw,data+(uint32_t)i*4u,sizeof(raw));
        if(raw)PD_Diag_Record(PD_DIAG_SOURCE_PDO,pos,raw);
    }
}

void PD_Diag_Service(void)
{
    DiagEvent e;
    uint32_t pos, ma;
    char buf[64];
    static const char *const hr_names[]={
        "START_ACK","START_REQ","MSG_SENT","WAIT_0V",
        "WAIT_5V","COMPLETE","FAILED"
    };
    if(s_head==s_tail)return;
    e=s_events[s_tail];
    s_tail=(uint8_t)((s_tail+1u)&(PD_DIAG_CAP-1u));
    if(e.event==PD_DIAG_SOURCE_PDO)
    {
        PD_SourceStats_FormatPDO((uint8_t)(e.a-1u),e.b,buf,sizeof(buf));
        if(buf[0])printf("[PD %lu] PDO %s\r\n",(unsigned long)e.ms,buf);
    }
    else if(e.event==PD_DIAG_RDO || e.event==PD_DIAG_EPR_RDO)
    {
        pos=(e.b>>28u)&15u;
        ma=((e.b>>10u)&1023u)*10u;
        printf("[PD %lu] RDO #%lu %lu.%02luV %lu.%02luA%s\r\n",
               (unsigned long)e.ms,(unsigned long)pos,
               (unsigned long)(e.a/1000u),(unsigned long)((e.a%1000u)/10u),
               (unsigned long)(ma/1000u),(unsigned long)((ma%1000u)/10u),
               e.event==PD_DIAG_EPR_RDO?" EPR":"");
    }
    else if(e.event==PD_DIAG_RESET)
    {
        const char *name=e.a<7u?hr_names[e.a]:"UNKNOWN";
        printf("[PD %lu] HARD_RESET %s\r\n",(unsigned long)e.ms,name);
    }
    else if(e.event==PD_DIAG_MANUAL)
    {
        if(e.a==3u)
            printf("[PD %lu] API max Fixed %luV\r\n",
                   (unsigned long)e.ms,(unsigned long)e.b);
        else if(e.a==4u)
            printf("[PD %lu] API request %s\r\n",
                   (unsigned long)e.ms,e.b?"ON":"OFF (5V)");
    }
}

void PD_Diag_Flush(void)
{
    uint8_t guard=0u;
    DebugUART_Service();
    while(s_head!=s_tail && guard++<PD_DIAG_CAP)PD_Diag_Service();
    DebugUART_Service();
}

/* Retain the PHY callback signatures: these no longer record traffic.
 * The optional probe needs only the timestamp of native KeepAlive TX. */
void PD_TxTrace(uint32_t type,uint32_t hdr,uint32_t size,
                uint32_t do1,uint32_t do2)
{
    (void)hdr;(void)size;(void)do2;
    if(type==0x10u && do1==0x00038002u)
        g_pd_last_keepalive_ms=HAL_GetTick();
}
void PD_RxTrace(uint32_t type,uint32_t hdr,uint32_t do1)
{(void)type;(void)hdr;(void)do1;}
void PD_TxReject(uint32_t type,uint32_t status)
{(void)type;(void)status;}
void PD_TxDone(uint32_t code){(void)code;}
void PD_SinkTxOkProbe(uint32_t ccx,uint32_t vstate)
{(void)ccx;(void)vstate;}
