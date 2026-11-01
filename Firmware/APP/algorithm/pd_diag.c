/* PD callback: record binary event only. Format and transmit later.
 * No blocking UART, printf, flash writes or I2C in the sender-response path.
 */
#include "algorithm/pd_diag.h"
#include "main.h"
#include <stdio.h>
#define PD_DIAG_CAP 128u
typedef struct {
    uint32_t ms,a,b;
    uint8_t event;
} DiagEvent;
static DiagEvent s_events[PD_DIAG_CAP];
static volatile uint8_t s_head,s_tail;
static volatile uint32_t s_dropped;
void PD_Diag_Record(uint8_t code,uint32_t a,uint32_t b)
{
    uint8_t next=(uint8_t)((s_head+1u)&(PD_DIAG_CAP-1u));
    if(next==s_tail){++s_dropped;return;}
    s_events[s_head].ms=HAL_GetTick();
    s_events[s_head].a=a;
    s_events[s_head].b=b;
    s_events[s_head].event=code;
    s_head=next;
}
/* One-shot TX tracing, fed by the ST PHY TX hook. It answers the one
 * question the app-level logs cannot: did the stack really put the EPR_Mode
 * message (header MessageType 0x0A) on the wire, or did the PHY refuse it?
 * No printf here: PD callbacks and the PHY run in interrupt/sender-response
 * context, so only a queue record is made. */
void PD_TxTrace(uint32_t type,uint32_t hdr,uint32_t size,uint32_t do1,uint32_t do2)
{
    /* GoodCRC is reported once. Everything else is reported until the same
     * (type, first data object) repeats: an EPR session emits a KeepAlive
     * every ~485 ms with identical content and logging those floods the UART
     * for no information. b = (raw 16-bit header << 16) | size, so revision,
     * message id and NDO stay visible. */
    static uint32_t seen;
    static uint32_t last_sig=0xFFFFFFFFu;
    if(type==1u)
    {
        if((seen&2UL)!=0u)return;
        seen|=2UL;
    }
    else
    {
        uint32_t sig=(type<<24)|(do1&0x00FFFFFFu);
        if(sig==last_sig)return;
        last_sig=sig;
    }
    PD_Diag_Record(PD_DIAG_PD_TX,type,((hdr&0xFFFFUL)<<16)|(size&0xFFFFUL));
    PD_Diag_Record(PD_DIAG_PD_TXDO,do1,do2);
}
void PD_RxTrace(uint32_t type,uint32_t hdr,uint32_t do1)
{
    static uint32_t seen;
    static uint32_t last_sig=0xFFFFFFFFu;
    if(type==1u)
    {
        if((seen&2UL)!=0u)return;
        seen|=2UL;
    }
    else
    {
        uint32_t sig=(type<<24)|(do1&0x00FFFFFFu);
        if(sig==last_sig)return;
        last_sig=sig;
    }
    PD_Diag_Record(PD_DIAG_PD_RX,type,hdr&0xFFFFUL);
}
void PD_TxReject(uint32_t type,uint32_t status)
{
    static uint32_t seen;
    if(type<32u&&(seen&(1UL<<type))!=0u)return;
    if(type<32u)seen|=1UL<<type;
    PD_Diag_Record(PD_DIAG_PD_TXREJ,type,status);
}
/* UCPD transmit outcome: 0 = sent, 1 = discarded (TXMSGDISC), 2 = aborted
 * (TXMSGABT). A message that never reaches the wire explains a source that
 * never answers, so each outcome is reported once. */
void PD_TxDone(uint32_t code)
{
    static uint32_t seen;
    if(code<32u&&(seen&(1UL<<code))!=0u)return;
    if(code<32u)seen|=1UL<<code;
    PD_Diag_Record(PD_DIAG_PD_TXDONE,code,0u);
}
/* One-shot evidence of the CC state the SinkTxOK predicate actually sees. */
void PD_SinkTxOkProbe(uint32_t ccx,uint32_t vstate)
{
    static uint8_t done;
    if(done)return;
    done=1u;
    PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A5u,(ccx<<16)|(vstate&0xFFFFu));
}
void PD_Diag_Service(void)
{
    DiagEvent e;
    const char *label;
    if(s_head==s_tail)return;
    e=s_events[s_tail];
    s_tail=(uint8_t)((s_tail+1u)&(PD_DIAG_CAP-1u));
    switch(e.event)
    {
    case PD_DIAG_ATTACH:label="ATTACH";break;
    case PD_DIAG_DETACH:label="DETACH";break;
    case PD_DIAG_CAPS:label="SPR_CAPS";break;
    case PD_DIAG_RDO:label="REQUEST";break;
    case PD_DIAG_CONTRACT:label="CONTRACT";break;
    case PD_DIAG_RESET:label="HARD_RESET";break;
    case PD_DIAG_NOTIFY:label="NOTIFY";break;
    case PD_DIAG_MANUAL:label="MANUAL";break;
    case PD_DIAG_EPR_ENTER:label="EPR_ENTER";break;
    case PD_DIAG_EPR_MODE:label="EPR_MODE";break;
    case PD_DIAG_EPR_CAPS:label="EPR_CAPS";break;
    case PD_DIAG_EPR_RDO:label="EPR_REQUEST";break;
    case PD_DIAG_EPR_PDO2:label="EPR_PDO2";break;
    case PD_DIAG_EPR_COPYPDO:label="EPR_COPY";break;
    case PD_DIAG_EPR_EVENT:label="EPR_EVT";break;
    case PD_DIAG_EPR_ACTION:label="EPR_ACT";break;
    case PD_DIAG_GETINFO:label="GETINFO";break;
    case PD_DIAG_SETINFO:label="SETINFO";break;
    case PD_DIAG_PD_TX:label="PD_TX";break;
    case PD_DIAG_PD_TXREJ:label="PD_TXREJ";break;
    case PD_DIAG_PD_TXDONE:label="PD_TXDONE";break;
    case PD_DIAG_PD_RX:label="PD_RX";break;
    case PD_DIAG_PD_TXDO:label="PD_TXDO";break;
    default:label="UNKNOWN";break;
    }
    if(e.event==PD_DIAG_PD_TXDO)
        printf("[PD %lu] %s do1=0x%08lX do2=0x%08lX\r\n",
               (unsigned long)e.ms,label,(unsigned long)e.a,(unsigned long)e.b);
    else
        printf("[PD %lu] %s a=%lu b=0x%08lX\r\n",
               (unsigned long)e.ms,label,(unsigned long)e.a,(unsigned long)e.b);
    /* Decode the 112..119 EPR notification ids for readability. */
    if(e.event==PD_DIAG_EPR_EVENT)
    {
        const char *name;
        switch(e.a)
        {
        case 112u:name="EPRMODE_INVALID";break;
        case 113u:name="EPRMODE_ACK";break;
        case 114u:name="EPRMODE_SUCCEEDED";break;
        case 115u:name="EPRMODE_FAILED";break;
        case 116u:name="EPRMODE_INIT";break;
        case 117u:name="EPRMODE_EXIT";break;
        case 118u:name="EPR_SNKCAP_RECEIVED";break;
        case 119u:name="EPR_SRCCAP_RECEIVED";break;
        default:name="";break;
        }
        if(name[0])printf("[PD %lu]   %s\r\n",(unsigned long)e.ms,name);
    }
    (void)s_dropped;
}
