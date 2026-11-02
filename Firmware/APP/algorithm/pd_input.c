/* IH01A / ST USBPD FULL v5.4.1: one-shot SPR -> optional EPR 28V/5A sink.
 * Passive Rd remains external. PWM is never enabled by this module.
 * All requests are made from foreground cooperative task, never from IRQ.
 */
#include "algorithm/pd_input.h"
#include "algorithm/pd_epr_policy.h"
#include "algorithm/pd_diag.h"
#include "main.h"
#include "conv_adc.h"
#include "usbpd_core.h"
#include "usbpd_dpm_user.h"
#include <string.h>

static uint32_t s_caps[7], s_epr_caps[13];
static uint8_t s_count, s_manual, s_epr_count;
static uint8_t s_epr_mode, s_epr_enter_once, s_epr_exit_queued;
static uint8_t s_epr_copy_valid, s_epr_user_requested;
static uint8_t s_epr_any_event, s_epr_stall_logged, s_epr_probe;
#if IH01A_EPR_APP_KEEPALIVE_PROBE
extern volatile uint32_t g_pd_last_keepalive_ms;  /* timestamp from PHY tracing */
static uint32_t s_epr_ka_ms;
static uint8_t  s_epr_ka_fail;   /* consecutive rejected probes */
static uint8_t  s_epr_ka_off;    /* no more app AMS attempts this attachment */
#endif
static uint8_t s_epr_capretry;    /* Get_Source_Cap retried when caps lack EPR */
static uint8_t s_epr_local_pos;   /* 1 = EPR PDOs are numbered 1..6 */
/* 0 = request the 1-based index inside EPR_Source_Capabilities (default),
 * 1 = request the absolute position (index + 7). Selectable at runtime from
 * the CLI so both conventions can be tried without reflashing. */
static uint8_t s_epr_opos_absolute = IH01A_EPR_OPOS_ABSOLUTE_DEFAULT;
/* Auto-enter EPR after the SPR contract; toggled by the 'pd auto' CLI so the
 * same build can produce both sequences for capture. */
static uint8_t s_epr_auto = IH01A_EPR_28V5A_AUTO_ENTER;
void PD_Input_SetEPRAuto(uint8_t on){s_epr_auto=on?1u:0u;}
uint8_t PD_Input_GetEPRAuto(void){return s_epr_auto;}
void PD_Input_SetEPRObjectPosition(uint8_t absolute){s_epr_opos_absolute=absolute?1u:0u;}
#if IH01A_EPR_DELAY_GETCAP
static uint32_t s_epr_getcap_due_ms;
static uint8_t  s_epr_getcap_sent;
#endif
/* Set by the stall fallback, consumed from USBPD_DPM_Notification(). */
volatile uint8_t g_epr_ctx_pending;
static uint32_t s_selected_rdo, s_copy_pdo, s_attach_ms, s_contract_ms;
static uint32_t s_epr_mode_since_ms, s_epr_enter_ms;
static uint32_t s_spr_rdo,s_spr_mv,s_spr_ma;
static uint8_t s_spr_pos;
static PD_InputSnapshot s_state;
static PD_InputContract s_active, s_candidate;
static uint8_t s_active_valid,s_candidate_valid;
static uint8_t s_req_enabled=1u;   /* retained across unplug; not in Flash */
static uint8_t s_max_fixed_v=31u;  /* permits 28V EPR by default */
static uint8_t s_policy_dirty,s_policy_tries;
static uint32_t s_policy_try_ms;
/* Setters affect requested power, not physical CC. Never call a PE send
 * directly from an arbitrary caller (see EPR AMS context incident). */
static uint32_t PD_PolicyMaxMv(void)
{
    return s_req_enabled ? (uint32_t)s_max_fixed_v*1000u : 5000u;
}
static void PD_ActiveInvalidate(void)
{
    s_active_valid=0u;
    s_candidate_valid=0u;
    memset(&s_active,0,sizeof(s_active));
    memset(&s_candidate,0,sizeof(s_candidate));
}
static void PD_PolicyChanged(void)
{
    PD_ActiveInvalidate();
    s_policy_dirty=1u;
    s_policy_tries=0u;
    s_policy_try_ms=0u;
    s_epr_user_requested=0u;
    g_epr_ctx_pending=0u;
    if(!s_req_enabled || s_max_fixed_v<28u)
    {
        s_epr_enter_once=1u;
        if(s_epr_mode)s_epr_exit_queued=1u;
    }
    else if(!s_epr_mode)s_epr_enter_once=0u;
}
void PD_Input_SetRequestEnabled(uint8_t enabled)
{
    enabled=enabled?1u:0u;
    if(s_req_enabled==enabled)return;
    s_req_enabled=enabled;
    PD_PolicyChanged();
}
uint8_t PD_Input_GetRequestEnabled(void){return s_req_enabled;}
uint8_t PD_Input_SetMaxFixedVoltage(uint8_t voltage_v)
{
    if(voltage_v<5u||voltage_v>31u)return 0u;
    if(s_max_fixed_v!=voltage_v)
    {
        s_max_fixed_v=voltage_v;
        PD_PolicyChanged();
    }
    return 1u;
}
uint8_t PD_Input_GetMaxFixedVoltage(void){return s_max_fixed_v;}
uint8_t PD_Input_GetActiveContract(PD_InputContract *out)
{
    if(!out)return 0u;
    memset(out,0,sizeof(*out));
    if(!s_active_valid||(!s_req_enabled && s_active.voltage_mv!=5000u)||
       s_policy_dirty||s_epr_exit_queued||!s_state.attached||
       !s_state.explicit_contract||s_candidate_valid)return 0u;
    /* Contract voltage is valid only while physical VBUS agrees. */
    {
        uint32_t bus=PD_Input_VbusMillivolts();
        if(bus < (s_active.voltage_mv*9u)/10u ||
           bus > (s_active.voltage_mv*11u)/10u)return 0u;
    }
    *out=s_active;
    return 1u;
}

uint32_t PD_Input_VbusMillivolts(void)
{
    ConvADC_Snapshot frame;
    ConvADC_CopySnapshot(&frame);
    if(!frame.frames)return 0u;
    return (uint32_t)ConvADC_PinMillivolts(frame.raw[CONV_ADC_VBUS])*16u;
}
static uint8_t PD_SourceIsEPRCapable(void)
{
#if defined(USBPDCORE_EPR)
    USBPD_PDO_TypeDef pdo = {0};
    if(!s_count)return 0u;
    pdo.d32=s_caps[0];
    return (uint8_t)((pdo.SRCFixedPDO.FixedSupply==USBPD_CORE_PDO_TYPE_FIXED) &&
                     (pdo.SRCFixedPDO.EPR_Capable!=0u));
#else
    return 0u;
#endif
}
void PD_Input_Init(void)
{
    memset(&s_state,0,sizeof(s_state));
    PD_ActiveInvalidate();
    s_policy_dirty=s_policy_tries=0u;s_policy_try_ms=0u;
    memset(s_caps,0,sizeof(s_caps));
    memset(s_epr_caps,0,sizeof(s_epr_caps));
    s_count=s_manual=s_epr_count=s_epr_mode=s_epr_enter_once=0u;
    s_epr_exit_queued=s_epr_copy_valid=s_epr_user_requested=0u;
    s_epr_any_event=s_epr_stall_logged=0u;s_epr_local_pos=0u;s_epr_capretry=0u;
    s_selected_rdo=s_copy_pdo=s_attach_ms=s_contract_ms=0u;
    s_epr_mode_since_ms=s_epr_enter_ms=0u;
    s_spr_rdo=s_spr_mv=s_spr_ma=0u;s_spr_pos=0u;
#if IH01A_EPR_APP_KEEPALIVE_PROBE
    s_epr_ka_ms=0u;s_epr_ka_fail=0u;s_epr_ka_off=0u;
#endif
}
void PD_Input_Attach(uint8_t attached)
{
    PD_Input_Init();
    s_state.attached=attached?1u:0u;
    s_attach_ms=HAL_GetTick();
}
void PD_Input_Reset(void)
{
    /* A PD hard reset is NOT a detach: do not automatically attempt EPR again. */
    s_state.explicit_contract=0u;
    PD_ActiveInvalidate();
    s_policy_tries=0u;
    s_policy_try_ms=0u;
    /* Hard Reset invalidates the RDO. The next SPR capabilities must be
     * evaluated afresh for the highest legal Fixed PDO. Do not re-enter EPR
     * on this attachment: s_epr_enter_once is intentionally kept at 1. */
    s_state.attempted=0u;
    s_state.position=0u;
    s_state.selected_mv=0u;
    s_state.requested_ma=0u;
    s_manual=s_count=s_epr_count=s_epr_mode=s_epr_copy_valid=0u;
    s_epr_enter_once=1u;
    s_epr_user_requested=0u;
    s_epr_exit_queued=0u;
    s_epr_any_event=0u;
    s_epr_stall_logged=0u;
    s_epr_local_pos=0u;s_epr_capretry=0u;
    s_selected_rdo=s_copy_pdo=0u;
    s_epr_mode_since_ms=s_epr_enter_ms=0u;
    s_spr_rdo=s_spr_mv=s_spr_ma=0u;s_spr_pos=0u;
#if IH01A_EPR_APP_KEEPALIVE_PROBE
    s_epr_ka_ms=0u;s_epr_ka_fail=0u;s_epr_ka_off=0u;
#endif
    memset(s_caps,0,sizeof(s_caps));
    memset(s_epr_caps,0,sizeof(s_epr_caps));
}
void PD_Input_Caps(const uint8_t *data,uint32_t bytes)
{
    if(!data||!bytes||(bytes&3u)||bytes>sizeof(s_caps))
    {s_count=0u;return;}
    /* Force a new RDO after changes to Source current, Source voltage,
     * or user voltage policy. Cached RDO must not hide a 7A offer. */
    if((uint32_t)s_count*4u!=bytes || memcmp(s_caps,data,bytes)!=0 ||
       s_policy_dirty)
        s_state.attempted=0u;
    memcpy(s_caps,data,bytes);
    s_count=(uint8_t)(bytes/4u);
    if(s_policy_dirty)
    {
        /* A new capability message will cause Evaluate to use the
         * requested max Fixed limit. Clear dirty only now, not when the
         * foreground GetSrcCap API merely returns QUEUED. */
        s_policy_dirty=0u;
        s_policy_tries=0u;
    }
    PD_Diag_Record(PD_DIAG_CAPS,s_count,s_caps[0]);
}
/* ST reports EPR PDOs SEPARATELY from the seven SPR positions: the EPR
 * callback carries 1..6 objects (4..24 bytes) and the first one is EPR
 * object position 8. Some PE builds hand over the combined SPR+EPR list
 * instead, so the size decides the layout and no result silently shifts
 * every object position. Truncated/odd payloads are dropped.
 */
void PD_Input_EPRCaps(const uint8_t *data,uint32_t bytes)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    uint32_t count, first=0u;
    s_epr_count=0u;
    s_epr_copy_valid=0u;
    memset(s_epr_caps,0,sizeof(s_epr_caps));
    if(!s_epr_mode||!data||bytes<4u||(bytes&3u)!=0u||
       bytes>((USBPD_MAX_NB_PDO+USBPD_MAX_NB_EPRPDO)*4u))
    {
        PD_Diag_Record(PD_DIAG_EPR_CAPS,bytes,0u);
        return;
    }
    count=bytes/4u;
    if(count<=USBPD_MAX_NB_EPRPDO)
    {
        /* EPR-only payload: slots 1..7 stay for SPR, first EPR PDO = slot 8.
         * The Object Position we must request, however, is the 1-based index
         * inside the EPR_Source_Capabilities message (1..6), which is what the
         * reference sink sends and what Sources accept. Requesting 8 here
         * makes the Source answer with a Hard Reset. */
        memcpy(&s_epr_caps[USBPD_MAX_NB_PDO],data,bytes);
        s_epr_count=(uint8_t)(USBPD_MAX_NB_PDO+count);
        s_epr_local_pos=1u;
    }
    else
    {
        /* Combined SPR+EPR payload: object position already equals index+1. */
        memcpy(&s_epr_caps[0],data,bytes);
        s_epr_count=(uint8_t)count;
        s_epr_local_pos=0u;s_epr_capretry=0u;
    }
    memcpy(&first,&s_epr_caps[USBPD_MAX_NB_PDO],sizeof(first));
    PD_Diag_Record(PD_DIAG_EPR_CAPS,bytes,first);
    /* Additional second object helps diagnose AVS after 28V Fixed. */
    if(s_epr_count>8u)
        PD_Diag_Record(PD_DIAG_EPR_PDO2,2u,s_epr_caps[8]);
#else
    (void)data;(void)bytes;
#endif
}
void PD_Input_Contract(uint8_t ready)
{
    if(ready)
    {
        if(!s_state.explicit_contract)s_contract_ms=HAL_GetTick();
        s_state.explicit_contract=1u;
        /* Only the PE's explicit-contract/ready notification can promote
         * an RDO candidate to power-loop authority. */
        if(s_candidate_valid && !s_epr_exit_queued && !s_policy_dirty)
        {
            s_active=s_candidate;
            s_active_valid=1u;
            s_candidate_valid=0u;
        }
    }
    else
    {
        s_state.explicit_contract=0u;
        PD_ActiveInvalidate();
    }
}
void PD_Input_RequestRejected(void)
{
    /* Do not report a newly requested but rejected voltage as active. */
    s_candidate_valid=0u;
}
/* USBPD_CORE_DATATYPE_REQ_COPYPDO expects the entire two-object EPR
 * Request payload: object 0 is the RDO, object 1 is the copied Source
 * PDO. Supplying only a four-byte CopyPDO leaves one DO absent.
 */
/* USBPD_CORE_DATATYPE_REQ_COPYPDO supplies ONLY the copied Source PDO; the
 * policy engine builds the EPR_Request as DO1=RDO, DO2=what we return here.
 * The logic-analyser capture proved that returning RDO+PDO (8 bytes) made the
 * engine copy our first four bytes, so DO2 became 0x11C4B12C (our RDO)
 * instead of 0x0008C1F4 (the 28V/5A EPR PDO) and the Source answered with a
 * Hard Reset. Always hand back exactly four bytes: the copied PDO. */
uint8_t PD_Input_EPR_CopyPDO(uint32_t *out)
{
    if(!out||!s_epr_mode||!s_epr_copy_valid)return 0u;
    *out=s_copy_pdo;
    return 1u;
}
uint8_t PD_Input_EPR_Active(void){return s_epr_mode;}
static void PD_EPR_RestoreSPR(void)
{
    if(s_spr_pos==0u)return;
    s_selected_rdo=s_spr_rdo;
    s_state.position=s_spr_pos;
    s_state.selected_mv=s_spr_mv;
    s_state.requested_ma=s_spr_ma;
    s_epr_copy_valid=0u;
}
void PD_Input_EPR_RequestRejected(void)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    if(s_epr_mode && s_epr_copy_valid)
    {
        s_epr_user_requested=0u;
        PD_EPR_RestoreSPR();
        s_epr_exit_queued=1u;
        PD_Diag_Record(PD_DIAG_EPR_MODE,0u,0xFFFFu);
    }
#endif
}
void PD_Input_EPR_Notify(uint32_t event)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    /* Log EVERY EPR notification first: this is the only proof that the
     * PE really exchanged EPR_Mode / EPR capability messages. */
    s_epr_any_event=1u;
    PD_Diag_Record(PD_DIAG_EPR_EVENT,event,
                   (uint32_t)s_epr_mode|((uint32_t)s_epr_copy_valid<<1)|
                   ((uint32_t)s_epr_enter_once<<2));
    switch(event)
    {
    case USBPD_NOTIFY_EPRMODE_SUCCEEDED:
        s_epr_mode=1u;
        PD_ActiveInvalidate(); /* SPR contract is not an EPR power budget */
        if(!s_req_enabled || s_max_fixed_v<28u)s_epr_exit_queued=1u;
        s_epr_mode_since_ms=HAL_GetTick();
        s_epr_count=0u;
        s_epr_copy_valid=0u;
#if IH01A_EPR_DELAY_GETCAP
        /* Fetch the EPR capabilities ourselves after the reference delay. */
        s_epr_getcap_due_ms=HAL_GetTick()+IH01A_EPR_GETCAP_DELAY_MS;
        s_epr_getcap_sent=0u;
#endif
        PD_Diag_Record(PD_DIAG_EPR_MODE,1u,event);
        break;
#if defined(USBPD_NOTIFY_EPR_SRCCAP_RECEIVED)
    case USBPD_NOTIFY_EPR_SRCCAP_RECEIVED:
    case USBPD_NOTIFY_EPR_SNKCAP_RECEIVED:
        /* Capability content is logged by PD_Input_EPRCaps(); keep the
         * state machine untouched. */
        break;
#endif
    case USBPD_NOTIFY_EPRMODE_FAILED:
    case USBPD_NOTIFY_EPRMODE_INVALID:
    case USBPD_NOTIFY_EPRMODE_EXIT:
        s_epr_mode=0u;
        PD_ActiveInvalidate(); /* Cannot assume a completed SPR contract. */
        if(s_policy_dirty)s_policy_tries=0u;
        s_epr_mode_since_ms=0u;
        s_epr_count=0u;
        s_epr_copy_valid=0u;
        s_epr_exit_queued=0u;
        s_epr_user_requested=0u;
        PD_EPR_RestoreSPR();
        PD_Diag_Record(PD_DIAG_EPR_MODE,0u,event);
        break;
    default:break;
    }
#else
    (void)event;
#endif
}
static uint32_t PD_EncodeFixedRDO(uint8_t pos,uint32_t ma,uint8_t epr)
{
    USBPD_SNKRDO_TypeDef request = {0};
    /* Fixed RDO current is a 10-bit number in 10mA units, not limited to 5A.
     * EPR requests remain constrained to their advertised 5A fixed PDO. */
    if(pos==0u||pos>13u||ma<10u||ma>10230u)return 0u;
    request.FixedVariableRDO.ObjectPosition=pos;
    request.FixedVariableRDO.MaxOperatingCurrent10mAunits=ma/10u;
    request.FixedVariableRDO.OperatingCurrentIn10mAunits=ma/10u;
    request.FixedVariableRDO.NoUSBSuspend=1u;
#if defined(USBPDCORE_EPR)
    request.FixedVariableRDO.EPR_Capable=epr?1u:0u;
    /* PD3.1: an EPR-capable Sink also declares Unchunked Extended Messages
     * support in the Request RDO. With this bit at 0 the Source answered our
     * EPR_Request with a Hard Reset (no Reject, no Not_Supported), which is a
     * protocol-error reaction, so the request itself must carry the bit. */
    request.FixedVariableRDO.UnchunkedExtendedMessage=
        IH01A_EPR_28V5A_HARDWARE_APPROVED?1u:0u;
#else
    (void)epr;
#endif
    return request.d32;
}
uint8_t PD_Input_Evaluate(uint32_t *rdo,uint32_t *type)
{
    uint8_t pos,epr=0u;
    uint32_t mv,ma,p;
    if(!rdo||!type||!s_state.attached)return 0u;
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    if(s_epr_mode)
    {
#if IH01A_EPR_DELAY_GETCAP
        /* Decline the capability set the PE fetched immediately after
         * EPRMODE_SUCCEEDED; we ask for a fresh one after the reference
         * delay, which is what the working CH32X035 sink does. */
        if(!s_epr_getcap_sent && !s_epr_exit_queued)
        {
            *rdo=0u;
            *type=USBPD_CORE_PDO_TYPE_FIXED;
            return 0u;
        }
#endif
        if(!s_epr_exit_queued && s_epr_count>=8u &&
           PD_Input_BestEPRFixed((const uint8_t *)s_epr_caps,
           (uint32_t)s_epr_count*4u,&pos,&mv,&ma,&p) &&
           mv==28000u && mv<=PD_PolicyMaxMv() && s_req_enabled &&
           ma>=IH01A_EPR_REQUEST_MA)
        {
            uint32_t nrdo;
            /* Two conventions are in the field: the object position is either
             * the 1-based index inside EPR_Source_Capabilities (1..6) or the
             * absolute position continuing the SPR list (8..13). The internal
             * slot is always 8..13; convert unless the absolute one is asked. */
            if(s_epr_local_pos && pos>(uint8_t)USBPD_MAX_NB_PDO && !s_epr_opos_absolute)
                pos=(uint8_t)(pos-(uint8_t)USBPD_MAX_NB_PDO);
            /* Request the configured current, never more than the PDO offers. */
            if(ma>IH01A_EPR_REQUEST_MA)ma=IH01A_EPR_REQUEST_MA;
            nrdo=PD_EncodeFixedRDO(pos,ma,1u);
            if(!nrdo)return 0u;
            if(!s_epr_copy_valid)
            {
                s_spr_rdo=s_selected_rdo;
                s_spr_mv=s_state.selected_mv;
                s_spr_ma=s_state.requested_ma;
                s_spr_pos=s_state.position;
            }
            s_copy_pdo=p;
            s_epr_copy_valid=1u;
            s_selected_rdo=nrdo;
            s_state.attempted=1u;
            s_state.position=pos;
            s_state.selected_mv=mv;
            s_state.requested_ma=ma;
            s_candidate.voltage_mv=mv;s_candidate.current_ma=ma;
            s_candidate.position=pos;s_candidate.epr=1u;
            s_candidate_valid=1u;s_active_valid=0u;
            PD_Diag_Record(PD_DIAG_EPR_RDO,mv,nrdo);
            PD_Diag_Record(PD_DIAG_EPR_COPYPDO,pos,p);
            *rdo=nrdo;
            *type=USBPD_CORE_PDO_TYPE_FIXED;
            return 1u;
        }
        /* Do not pass a SPR RDO to the FULL PE while in EPR mode.
         * An EPR_REQUEST needs a valid EPR RDO+CopyPDO pair.
         * Missing/truncated/unsupported EPR caps are an exit condition.
         */
        if(s_epr_count>=8u)
        {
            s_epr_user_requested=0u;
            s_epr_exit_queued=1u;
            PD_Diag_Record(PD_DIAG_EPR_MODE,0u,0xBAD0u);
        }
        *rdo=0u;
        *type=USBPD_CORE_PDO_TYPE_FIXED;
        return 0u;
    }
#endif
    if(s_state.attempted&&!s_manual)
    {
        if(s_state.position>0u && s_state.position<=s_count &&
           s_state.selected_mv<=PD_PolicyMaxMv())
        {
            p=s_caps[s_state.position-1u];
            if((p>>30u)==0u && ((p>>10u)&1023u)*50u==s_state.selected_mv &&
               (p&1023u)*10u>=s_state.requested_ma)
            {
                *rdo=s_selected_rdo;
                *type=USBPD_CORE_PDO_TYPE_FIXED;
                return 1u;
            }
        }
        /* The previous SPR PDO disappeared. Re-select the best *current*
         * Fixed SPR PDO rather than unnecessarily reverting to 5 V. This
         * does not trigger an additional EPR entry attempt. */
        if(!PD_Input_BestFixedLimited((const uint8_t *)s_caps,
               (uint32_t)s_count*4u,PD_PolicyMaxMv(),&pos,&mv,&ma))return 0u;
    }
    else if(!PD_Input_BestFixedLimited((const uint8_t *)s_caps,
              (uint32_t)s_count*4u,PD_PolicyMaxMv(),&pos,&mv,&ma))return 0u;
    epr=(uint8_t)(IH01A_EPR_28V5A_HARDWARE_APPROVED && PD_SourceIsEPRCapable());
    s_selected_rdo=PD_EncodeFixedRDO(pos,ma,epr);
    if(!s_selected_rdo)return 0u;
    s_state.attempted=1u;
    s_state.position=pos;
    s_state.selected_mv=mv;
    s_state.requested_ma=ma;
    s_candidate.voltage_mv=mv;s_candidate.current_ma=ma;
    s_candidate.position=pos;s_candidate.epr=0u;
    s_candidate_valid=1u;s_active_valid=0u;
    s_manual=0u;
    PD_Diag_Record(PD_DIAG_RDO,mv,s_selected_rdo);
    *rdo=s_selected_rdo;
    *type=USBPD_CORE_PDO_TYPE_FIXED;
    return 1u;
}
void PD_Input_EPR_Poll(void)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    if(!s_state.attached)return;
    /* Active policy changes are negotiated by the existing GetSrcCap ->
     * Evaluate -> Request -> contract PE path, never by forging a VBUS level.
     * Bounded retries tolerate a busy PE; on unresolved failure GetActive
     * Contract remains invalid, so the heater cannot draw stale power. */
    if(s_policy_dirty && !s_epr_mode && s_state.explicit_contract &&
       s_policy_tries<3u && (s_policy_tries==0u ||
       (uint32_t)(HAL_GetTick()-s_policy_try_ms)>=1000u))
    {
        USBPD_StatusTypeDef r=USBPD_DPM_RequestGetSourceCapability(USBPD_PORT_0);
        s_policy_try_ms=HAL_GetTick();
        ++s_policy_tries;
        PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2ACu,(uint32_t)r);
    }
    if(s_epr_mode && !s_epr_copy_valid &&
       (uint32_t)(HAL_GetTick()-s_epr_mode_since_ms)>2500u)
    {
        s_epr_user_requested=0u;
        s_epr_exit_queued=1u;
        PD_Diag_Record(PD_DIAG_EPR_MODE,0u,0xBAD1u);
    }
    /* The PE accepted the EPR_Mode request but produced no notification at
     * all: report it once, then probe the stack in increasing order. */
    if(s_epr_enter_ms&&s_epr_enter_once&&!s_epr_mode&&!s_epr_any_event)
    {
        uint32_t waited=(uint32_t)(HAL_GetTick()-s_epr_enter_ms);
        if(waited>1000u&&!s_epr_stall_logged)
        {
            s_epr_stall_logged=1u;
            PD_Diag_Record(PD_DIAG_EPR_EVENT,0u,0xD00Du);
        }
#if IH01A_EPR_STALL_FALLBACK
        if(waited>700u&&s_epr_probe<1u)
        {
            /* Probe 1: provoke one incoming message so the PE runs a message
             * cycle while an EPR machine is registered. */
            USBPD_StatusTypeDef r;
            s_epr_probe=1u;
            r=USBPD_PE_Request_CtrlMessage(USBPD_PORT_0,USBPD_CONTROLMSG_GET_SRC_CAP,
                                           USBPD_SOPTYPE_SOP);
            PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A4u,(uint32_t)r);
        }
        else if(waited>1300u&&s_epr_probe<2u)
        {
            /* Probe 2: emit EPR_Mode directly as a data message.
             * DO = (EPR Mode Action << 24) | (Sink PDP W << 16). */
            uint32_t mdo;
            USBPD_StatusTypeDef r;
            s_epr_probe=2u;
            mdo=(1UL<<24)|((uint32_t)IH01A_EPR_SINK_PDP_W<<16);
            r=USBPD_PE_Request_DataMessage(USBPD_PORT_0,USBPD_DATAMSG_EPR_MODE,&mdo);
            PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A1u,(uint32_t)r);
        }
#endif
    }
#if IH01A_EPR_DELAY_GETCAP
    /* After the reference delay, fetch the EPR capabilities ourselves. */
    if(s_epr_mode && !s_epr_getcap_sent &&
       (int32_t)(HAL_GetTick()-s_epr_getcap_due_ms)>=0)
    {
        USBPD_StatusTypeDef r;
        s_epr_getcap_sent=1u;
        r=USBPD_PE_Send_ExtendeControlMessage(USBPD_PORT_0,
                                              USBPD_EXTENDED_CONTROL_EPR_GETSRCCAPA);
        PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A6u,(uint32_t)r);
    }
#endif
#if IH01A_EPR_APP_KEEPALIVE_PROBE
    /* Bounded diagnostic only (disabled by default). The 4 MSa/s capture
     * found no app-injected KeepAlive frames at 375 ms; the FULL stack alone
     * maintained its validated 500 ms cadence. At most one accepted probe
     * or two rejected requests are attempted per attachment. */
    if(!s_epr_ka_off && s_epr_mode && s_state.selected_mv>=28000u &&
       (uint32_t)(HAL_GetTick()-s_epr_ka_ms)>=375u &&
       (uint32_t)(HAL_GetTick()-g_pd_last_keepalive_ms)>=250u)
    {
        USBPD_StatusTypeDef r;
        s_epr_ka_ms=HAL_GetTick();
        r=USBPD_PE_Send_ExtendeControlMessage(USBPD_PORT_0,
                                              USBPD_EXTENDED_CONTROL_EPR_KEEPALIVE);
        if(r==USBPD_OK)
        {
            /* A queued AMS is NOT proof of CC transmission. Stop after this
             * one probe to avoid repeatedly registering a silent machine. */
            s_epr_ka_fail=0u;
            s_epr_ka_off=1u;
            PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2ABu,0u);
        }
        else
        {
            PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2AAu,(uint32_t)r);
            if(++s_epr_ka_fail>=2u)s_epr_ka_off=1u;
        }
    }
#endif /* IH01A_EPR_APP_KEEPALIVE_PROBE */
    if(s_epr_exit_queued)
    {
        USBPD_StatusTypeDef r;
        /* Existing PE API; do not infer EPR exit from its return code.
         * An exit notification is required before requesting 5V SPR. */
        s_epr_exit_queued=0u;
        r=USBPD_PE_Request_EPRModeExit(USBPD_PORT_0);
        PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2ADu,(uint32_t)r);
        return;
    }
    /* The Source sometimes advertises the PDO1 EPR-capable bit only on a
     * later capability set (capture: one attach had eprCap=1 then 0, another
     * the opposite). Ask for one fresh capability set before giving up on
     * auto EPR for this attachment. */
    if(s_req_enabled && PD_PolicyMaxMv()>=28000u &&
       (s_epr_auto || s_epr_user_requested) && !s_epr_enter_once && !s_epr_mode &&
       s_state.explicit_contract && !PD_SourceIsEPRCapable() && !s_epr_capretry &&
       (uint32_t)(HAL_GetTick()-s_contract_ms)>250u)
    {
        s_epr_capretry=1u;
        (void)USBPD_PE_Request_CtrlMessage(USBPD_PORT_0,USBPD_CONTROLMSG_GET_SRC_CAP,
                                          USBPD_SOPTYPE_SOP);
        PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A9u,0u);
        return;
    }
    if(!s_req_enabled || PD_PolicyMaxMv()<28000u ||
       !(s_epr_auto || s_epr_user_requested)||
       s_epr_enter_once||s_epr_mode||!s_state.explicit_contract ||
       !PD_SourceIsEPRCapable() ||
       (uint32_t)(HAL_GetTick()-s_contract_ms)<250u)return;
    s_epr_enter_once=1u; /* only one request for this attachment */
    s_epr_enter_ms=HAL_GetTick();
    s_epr_any_event=0u;
    s_epr_stall_logged=0u;
    s_epr_probe=0u;
#if IH01A_EPR_REENTER_ON_CONTRACT
    /* We must register the EPR entry while the PE is processing a message
     * (the API itself validates that the last message was a Request). So
     * first provoke a complete Source_Capabilities -> Request -> PS_RDY
     * cycle, then register from the POWER_EXPLICIT_CONTRACT notification. */
    {
        USBPD_StatusTypeDef r;
        r=USBPD_PE_Request_CtrlMessage(USBPD_PORT_0,USBPD_CONTROLMSG_GET_SRC_CAP,
                                       USBPD_SOPTYPE_SOP);
        PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A4u,(uint32_t)r);
        if(r==USBPD_OK)
        {
            g_epr_ctx_pending=1u;   /* consumed in USBPD_DPM_Notification() */
        }
        else
        {
            uint32_t back=s_spr_rdo?s_spr_rdo:s_selected_rdo;
            USBPD_StatusTypeDef rb=USBPD_PE_Send_Request(USBPD_PORT_0,back,
                                                         USBPD_CORE_PDO_TYPE_FIXED);
            PD_Diag_Record(PD_DIAG_EPR_ACTION,0x2A2u,(uint32_t)rb);
            if(rb==USBPD_OK)
            {
                g_epr_ctx_pending=1u;
            }
            else
            {
                USBPD_StatusTypeDef rc=(USBPD_StatusTypeDef)USBPD_PE_Request_EPRModeEnter(USBPD_PORT_0);
                PD_Diag_Record(PD_DIAG_EPR_ENTER,(uint32_t)rc,s_state.position);
            }
        }
    }
#else
    {
        /* Raw USBPD_StatusTypeDef: 0=OK 1=NOTSUPPORTED 2=ERROR 3=BUSY.
         * A boolean hid the difference between "submitted" and "refused". */
        USBPD_StatusTypeDef rc=(USBPD_StatusTypeDef)USBPD_PE_Request_EPRModeEnter(USBPD_PORT_0);
        PD_Diag_Record(PD_DIAG_EPR_ENTER,(uint32_t)rc,s_state.position);
    }
#endif
#endif
}
/* Authorized manual EPR attempt: can be invoked by CLI or PD settings.
 * It never bypasses the FULL stack's EPR cable/source checks.
 */
uint8_t PD_Input_ManualEPR(void)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    if(!s_req_enabled || PD_PolicyMaxMv()<28000u ||
       !s_state.attached||!s_state.explicit_contract||s_epr_mode||
       !PD_SourceIsEPRCapable())return 0u;
    s_epr_enter_once=0u;
    s_epr_user_requested=1u;
    s_contract_ms=HAL_GetTick()-250u;
    PD_Diag_Record(PD_DIAG_MANUAL,2u,0u);
    return 1u;
#else
    return 0u;
#endif
}
uint8_t PD_Input_Manual(void)
{
    if(!s_state.attached)return 0u;
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    if(PD_Input_ManualEPR())return 1u;
#endif
    if(USBPD_DPM_RequestGetSourceCapability(0u)!=USBPD_OK)return 0u;
    s_manual=1u;
    PD_Diag_Record(PD_DIAG_MANUAL,1u,0u);
    return 1u;
}
void PD_Input_GetSnapshot(PD_InputSnapshot *out)
{
    if(!out)return;
    *out=s_state;
    out->bus_mv=PD_Input_VbusMillivolts();
    if(out->bus_mv<4500u||out->bus_mv>=PD_INPUT_MAX_MV)
        out->mode=PD_INPUT_NONE;
    else if(out->explicit_contract)
        out->mode=PD_INPUT_PD;
    else if(out->attached&&!out->attempted &&
            (uint32_t)(HAL_GetTick()-s_attach_ms)<3000u)
        out->mode=PD_INPUT_WAIT;
    else out->mode=PD_INPUT_DCIN;
}
