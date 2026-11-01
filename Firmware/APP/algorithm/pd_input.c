/* IH01A dedicated sink. SPR Fixed PDO only. No EPR entry/PPS in
 * USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil library.
 * USB-C physical Rd is externally fitted and permanently present:
 * this module never toggles CC GPIOs or power source mode.
 */
#include "algorithm/pd_input.h"
#include "main.h"
#include "conv_adc.h"
#include "usbpd_core.h"
#include "usbpd_dpm_user.h"
#include "algorithm/pd_diag.h"
#include <string.h>

static uint32_t s_caps[7];
static uint8_t s_count, s_manual;
static uint32_t s_selected_rdo, s_attach_ms;
static PD_InputSnapshot s_state;

uint32_t PD_Input_VbusMillivolts(void)
{
    ConvADC_Snapshot frame;
    ConvADC_CopySnapshot(&frame);
    if(!frame.frames)return 0u;
    return (uint32_t)ConvADC_PinMillivolts(frame.raw[CONV_ADC_VBUS])*16u;
}
void PD_Input_Init(void)
{
    memset(&s_state,0,sizeof(s_state));
    memset(s_caps,0,sizeof(s_caps));
     s_count=0u;s_manual=0u;s_selected_rdo=0u;
    s_attach_ms=0u;
}
void PD_Input_Attach(uint8_t attached)
{
    memset(&s_state,0,sizeof(s_state));
    s_state.attached=attached?1u:0u;
    s_attach_ms=HAL_GetTick();
    s_count=0u;s_manual=0u;s_selected_rdo=0u;
    memset(s_caps,0,sizeof(s_caps));
}
void PD_Input_Reset(void)
{
    s_state.explicit_contract=0u;
    s_manual=0u;s_count=0u;
    memset(s_caps,0,sizeof(s_caps));
    /* Hard reset is NOT a new cable attachment. */
}
void PD_Input_Caps(const uint8_t *data,uint32_t bytes)
{
    if(!data||!bytes||(bytes&3u)||bytes>28u)
    {
        s_count=0u;return;
    }
    memcpy(s_caps,data,bytes);
    s_count=(uint8_t)(bytes/4u);
    PD_Diag_Record(PD_DIAG_CAPS,s_count,s_caps[0]);
}
void PD_Input_Contract(uint8_t ready)
{
    s_state.explicit_contract=ready?1u:0u;
}
uint8_t PD_Input_Evaluate(uint32_t *rdo,uint32_t *type)
{
    uint8_t pos;
    uint32_t mv,ma,p;
    if(!rdo||!type||!s_state.attached)return 0u;
    if(s_state.attempted&&!s_manual)
    {
        /* Protocol engine may call Evaluate again after Source Caps
         * retransmission. It needs an RDO, but policy must not
         * automatically upgrade twice. Reuse previous if still valid. */
        if(s_state.position<=s_count&&s_state.position>0u)
        {
            p=s_caps[s_state.position-1u];
            if((p>>30u)==0u&&
               ((p>>10u)&1023u)*50u==s_state.selected_mv&&
               (p&1023u)*10u>=s_state.requested_ma)
            {
                *rdo=s_selected_rdo;
                *type=USBPD_CORE_PDO_TYPE_FIXED;
                return 1u;
            }
        }
        /* If previous profile vanished request standard default 5V,
         * not a new automatic high-voltage negotiation. */
        p=s_count?s_caps[0]:0u;
        if((p>>30u)!=0u||(((p>>10u)&1023u)*50u)!=5000u)return 0u;
        pos=1u;mv=5000u;ma=(p&1023u)*10u;
        if(ma>PD_INPUT_MAX_MA)ma=PD_INPUT_MAX_MA;
    }
    else if(!PD_Input_BestFixed((const uint8_t *)s_caps,
             (uint32_t)s_count*4u,&pos,&mv,&ma))return 0u;
    s_selected_rdo=PD_Input_FixedRDO(pos,ma);
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
uint8_t PD_Input_Manual(void)
{
    /* Called by MiaoUI thread, not by IRQ. Re-evaluation is explicitly
     * initiated by user through GET_SOURCE_CAP. Never send hard reset. */
    /* Manual retry may also be requested when the first negotiation
     * failed. The PE returns BUSY/ERROR if it is not in a legal state. */
    if(!s_state.attached)return 0u;
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
