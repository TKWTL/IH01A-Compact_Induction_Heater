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
static uint32_t s_selected_rdo, s_copy_pdo, s_attach_ms, s_contract_ms;
static uint32_t s_spr_rdo,s_spr_mv,s_spr_ma;
static uint8_t s_spr_pos;
static PD_InputSnapshot s_state;

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
    memset(s_caps,0,sizeof(s_caps));
    memset(s_epr_caps,0,sizeof(s_epr_caps));
    s_count=s_manual=s_epr_count=s_epr_mode=s_epr_enter_once=0u;
    s_epr_exit_queued=s_epr_copy_valid=s_epr_user_requested=0u;
    s_selected_rdo=s_copy_pdo=s_attach_ms=s_contract_ms=0u;
    s_spr_rdo=s_spr_mv=s_spr_ma=0u;s_spr_pos=0u;
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
    s_manual=s_count=s_epr_count=s_epr_mode=s_epr_copy_valid=0u;
    s_epr_enter_once=1u;
    s_epr_user_requested=0u;
    s_epr_exit_queued=0u;
    s_selected_rdo=s_copy_pdo=0u;
    s_spr_rdo=s_spr_mv=s_spr_ma=0u;s_spr_pos=0u;
    memset(s_caps,0,sizeof(s_caps));
    memset(s_epr_caps,0,sizeof(s_epr_caps));
}
void PD_Input_Caps(const uint8_t *data,uint32_t bytes)
{
    if(!data||!bytes||(bytes&3u)||bytes>sizeof(s_caps))
    {s_count=0u;return;}
    memcpy(s_caps,data,bytes);
    s_count=(uint8_t)(bytes/4u);
    PD_Diag_Record(PD_DIAG_CAPS,s_count,s_caps[0]);
}
/* ST reports EPR PDOs SEPARATELY from the seven SPR positions.
 * EPR callback size is typically 1..6 objects (4..24 bytes), not 32..52.
 * Reserve slots 1..7 and map the first EPR PDO to object position 8.
 * Never guess missing objects if an ST chunked callback is truncated.
 */
void PD_Input_EPRCaps(const uint8_t *data,uint32_t bytes)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    uint32_t count, first=0u;
    s_epr_count=0u;
    s_epr_copy_valid=0u;
    memset(s_epr_caps,0,sizeof(s_epr_caps));
    if(!s_epr_mode||!data||bytes<4u||
       bytes>(USBPD_MAX_NB_EPRPDO*4u)||(bytes&3u)!=0u)
    {
        PD_Diag_Record(PD_DIAG_EPR_CAPS,bytes,0u);
        return;
    }
    count=bytes/4u;
    memcpy(&s_epr_caps[USBPD_MAX_NB_PDO],data,bytes);
    s_epr_count=(uint8_t)(USBPD_MAX_NB_PDO+count);
    memcpy(&first,data,sizeof(first));
    PD_Diag_Record(PD_DIAG_EPR_CAPS,bytes,first);
    /* Additional second object helps diagnose AVS after 28V Fixed. */
    if(count>1u)
        PD_Diag_Record(PD_DIAG_EPR_PDO2,2u,s_epr_caps[8]);
#else
    (void)data;(void)bytes;
#endif
}
void PD_Input_Contract(uint8_t ready)
{
    if(ready && !s_state.explicit_contract)s_contract_ms=HAL_GetTick();
    s_state.explicit_contract=ready?1u:0u;
}
/* USBPD_CORE_DATATYPE_REQ_COPYPDO expects the entire two-object EPR
 * Request payload: object 0 is the RDO, object 1 is the copied Source
 * PDO. Supplying only a four-byte CopyPDO leaves one DO absent.
 */
uint8_t PD_Input_EPR_RequestPayload(uint32_t out[2])
{
    if(!out||!s_epr_mode||!s_epr_copy_valid)return 0u;
    out[0]=s_selected_rdo;
    out[1]=s_copy_pdo;
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
    switch(event)
    {
    case USBPD_NOTIFY_EPRMODE_SUCCEEDED:
        s_epr_mode=1u;
        s_epr_count=0u;
        s_epr_copy_valid=0u;
        PD_Diag_Record(PD_DIAG_EPR_MODE,1u,event);
        break;
    case USBPD_NOTIFY_EPRMODE_FAILED:
    case USBPD_NOTIFY_EPRMODE_INVALID:
    case USBPD_NOTIFY_EPRMODE_EXIT:
        s_epr_mode=0u;
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
    if(pos==0u||pos>13u||ma<10u||ma>5000u)return 0u;
    request.FixedVariableRDO.ObjectPosition=pos;
    request.FixedVariableRDO.MaxOperatingCurrent10mAunits=ma/10u;
    request.FixedVariableRDO.OperatingCurrentIn10mAunits=ma/10u;
    request.FixedVariableRDO.NoUSBSuspend=1u;
#if defined(USBPDCORE_EPR)
    request.FixedVariableRDO.EPR_Capable=epr?1u:0u;
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
    if(s_epr_mode && s_epr_count>=8u)
    {
        if(PD_Input_BestEPRFixed((const uint8_t *)s_epr_caps,
           (uint32_t)s_epr_count*4u,&pos,&mv,&ma,&p) &&
           mv==28000u && ma==5000u)
        {
            uint32_t nrdo=PD_EncodeFixedRDO(pos,ma,1u);
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
            PD_Diag_Record(PD_DIAG_EPR_RDO,mv,nrdo);
            PD_Diag_Record(PD_DIAG_EPR_COPYPDO,pos,p);
            *rdo=nrdo;
            *type=USBPD_CORE_PDO_TYPE_FIXED;
            return 1u;
        }
        /* No suitable 28V/5A Fixed EPR PDO: stay at SPR voltage and exit EPR. */
        s_epr_user_requested=0u;
        s_epr_exit_queued=1u;
    }
#endif
    if(s_state.attempted&&!s_manual)
    {
        if(s_state.position>0u && s_state.position<=s_count)
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
        p=s_count?s_caps[0]:0u;
        if((p>>30u)!=0u||(((p>>10u)&1023u)*50u)!=5000u)return 0u;
        pos=1u;mv=5000u;ma=(p&1023u)*10u;
        if(ma>PD_INPUT_MAX_MA)ma=PD_INPUT_MAX_MA;
    }
    else if(!PD_Input_BestFixed((const uint8_t *)s_caps,
              (uint32_t)s_count*4u,&pos,&mv,&ma))return 0u;
    epr=(uint8_t)(IH01A_EPR_28V5A_HARDWARE_APPROVED && PD_SourceIsEPRCapable());
    s_selected_rdo=PD_EncodeFixedRDO(pos,ma,epr);
    if(!s_selected_rdo)return 0u;
    s_state.attempted=1u;
    s_state.position=pos;
    s_state.selected_mv=mv;
    s_state.requested_ma=ma;
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
    if(s_epr_exit_queued)
    {
        s_epr_exit_queued=0u; /* only one exit attempt */
        (void)USBPD_PE_Request_EPRModeExit(USBPD_PORT_0);
        return;
    }
    if(!(IH01A_EPR_28V5A_AUTO_ENTER || s_epr_user_requested)||
       s_epr_enter_once||s_epr_mode||!s_state.explicit_contract ||
       !PD_SourceIsEPRCapable() ||
       (uint32_t)(HAL_GetTick()-s_contract_ms)<250u)return;
    s_epr_enter_once=1u; /* only one request for this attachment */
    if(USBPD_PE_Request_EPRModeEnter(USBPD_PORT_0)==USBPD_OK)
        PD_Diag_Record(PD_DIAG_EPR_ENTER,1u,0u);
    else
        PD_Diag_Record(PD_DIAG_EPR_ENTER,0u,0u);
#endif
}
/* Authorized manual EPR attempt: can be invoked by CLI or PD settings.
 * It never bypasses the FULL stack's EPR cable/source checks.
 */
uint8_t PD_Input_ManualEPR(void)
{
#if IH01A_EPR_28V5A_HARDWARE_APPROVED
    if(!s_state.attached||!s_state.explicit_contract||s_epr_mode||
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
