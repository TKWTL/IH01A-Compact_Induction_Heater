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
    PD_DIAG_PD_TXDONE,      /* a=0 sent, 1 discarded, 2 aborted          */
    PD_DIAG_PD_RX,          /* a=PD header MessageType, b=raw header word */
    PD_DIAG_PD_TXDO,        /* historic full tracing (production disabled) */
    PD_DIAG_SOURCE_PDO      /* a=1..13 absolute PDO position, b=raw PDO    */
};
void PD_Diag_Record(uint8_t code,uint32_t a,uint32_t b);
/* Drop legacy raw instrumentation at compile time, not merely in the
 * background logger. This lets armclang remove argument calculations and
 * PE/PHY debug call sites while keeping the proven protocol path intact.
 * The pd_diag.c implementation opts out to define the real symbol. */
#ifndef IH_PD_DIAG_INTERNAL
#define PD_Diag_Record(code,a,b) do {                                   \
    const uint8_t _ih_log_code=(uint8_t)(code);                           \
    if(_ih_log_code==PD_DIAG_SOURCE_PDO ||                              \
       _ih_log_code==PD_DIAG_RDO || _ih_log_code==PD_DIAG_EPR_RDO ||    \
       _ih_log_code==PD_DIAG_RESET ||                                   \
       (_ih_log_code==PD_DIAG_MANUAL &&                                 \
        ((a)==3u || (a)==4u)))                                          \
        PD_Diag_Record(_ih_log_code,(a),(b));                           \
} while(0)
#endif
void PD_Diag_Service(void);
void PD_Diag_Flush(void);
/* Records received capabilities without printing in PE callback context. */
void PD_Diag_SourceCaps(const uint8_t *data, uint32_t bytes, uint8_t epr);
/* Called from the ST PHY TX hook: one record per distinct message type. */
void PD_TxTrace(uint32_t type,uint32_t hdr,uint32_t size,uint32_t do1,uint32_t do2);
void PD_RxTrace(uint32_t type,uint32_t hdr,uint32_t do1);
void PD_TxReject(uint32_t type,uint32_t status);
void PD_TxDone(uint32_t code);
#endif
