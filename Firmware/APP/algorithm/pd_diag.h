#ifndef IH01A_PD_DIAG_H
#define IH01A_PD_DIAG_H
#include <stdint.h>
enum {
    PD_DIAG_ATTACH=1u, PD_DIAG_DETACH, PD_DIAG_CAPS, PD_DIAG_RDO,
    PD_DIAG_CONTRACT, PD_DIAG_RESET, PD_DIAG_NOTIFY, PD_DIAG_MANUAL
};
void PD_Diag_Record(uint8_t code,uint32_t a,uint32_t b);
void PD_Diag_Service(void);
#endif
