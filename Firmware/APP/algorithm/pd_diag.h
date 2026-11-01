#ifndef IH01A_PD_DIAG_H
#define IH01A_PD_DIAG_H
#include <stdint.h>
enum {
    PD_DIAG_ATTACH=1u, PD_DIAG_DETACH, PD_DIAG_CAPS, PD_DIAG_RDO,
    PD_DIAG_CONTRACT, PD_DIAG_RESET, PD_DIAG_NOTIFY, PD_DIAG_MANUAL,
    PD_DIAG_EPR_ENTER, PD_DIAG_EPR_MODE, PD_DIAG_EPR_CAPS, PD_DIAG_EPR_RDO,
    PD_DIAG_EPR_PDO2, PD_DIAG_EPR_COPYPDO,
    PD_DIAG_EPR_EVENT,      /* a=raw USBPD_NOTIFY_EPR* id, b=state bitmask */
    PD_DIAG_EPR_ACTION,     /* a=USBPD_ACTION_*, b=raw reply to the PE    */
    PD_DIAG_GETINFO,        /* a=USBPD_CORE_DATATYPE_*, b=bytes returned  */
    PD_DIAG_SETINFO,        /* a=USBPD_CORE_DATATYPE_*, b=bytes received  */
    PD_DIAG_PD_TX,          /* a=PD header MessageType, b=bytes sent     */
    PD_DIAG_PD_TXREJ,       /* a=PD header MessageType, b=USBPD status   */
    PD_DIAG_PD_TXDONE       /* a=0 sent, 1 discarded, 2 aborted          */
};
void PD_Diag_Record(uint8_t code,uint32_t a,uint32_t b);
void PD_Diag_Service(void);
/* Called from the ST PHY TX hook: one record per distinct message type. */
void PD_TxTrace(uint32_t type,uint32_t size);
void PD_TxReject(uint32_t type,uint32_t status);
void PD_TxDone(uint32_t code);
#endif
