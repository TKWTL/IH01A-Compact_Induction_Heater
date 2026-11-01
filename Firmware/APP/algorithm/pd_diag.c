/* PD callback: record binary event only. Format and transmit later.
 * No blocking UART, printf, flash writes or I2C in the sender-response path.
 */
#include "algorithm/pd_diag.h"
#include "main.h"
#include <stdio.h>
#define PD_DIAG_CAP 32u
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
    default:label="UNKNOWN";break;
    }
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
