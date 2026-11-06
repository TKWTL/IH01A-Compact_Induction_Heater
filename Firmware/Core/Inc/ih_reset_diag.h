#ifndef IH01A_RESET_DIAG_H
#define IH01A_RESET_DIAG_H
#include <stdint.h>

/* STM32G071 RCC->CSR reset flags. PWRRSTF combines POR/PDR/BOR;
 * it cannot distinguish power-on from brownout on this MCU. */
#define IH_RESET_CSR_PWRRSTF  (1UL << 27)
#define IH_RESET_CSR_SFTRSTF  (1UL << 28)
#define IH_RESET_DIAG_MAGIC  0x49483141UL /* 'IH1A' */

enum {
    IH_RESET_CAUSE_NONE=0u,
    IH_RESET_CAUSE_EMERGENCY=1u,
    IH_RESET_CAUSE_ERROR_HANDLER=2u,
    IH_RESET_CAUSE_ASSERT=3u,
    IH_RESET_CAUSE_HARDFAULT=4u,
    IH_RESET_CAUSE_NMI=5u
};
typedef struct {
    uint32_t magic;      /* committed last, after data barriers */
    uint32_t cause;
    uint32_t inverted_cause;
    uint32_t stage;
} IH_ResetDiagRecord;

/* Retained SRAM is evidence ONLY for software resets without a power reset.
 * A cold boot may leave arbitrary contents in SRAM; never trust the marker
 * merely because its bytes happen to survive a brownout. */
static inline uint32_t IH_ResetDiag_PriorCause(
    uint32_t csr,const volatile IH_ResetDiagRecord *record,
    uint32_t *stage)
{
    if(stage)*stage=0u;
    if(!record || (csr&IH_RESET_CSR_SFTRSTF)==0u ||
       (csr&IH_RESET_CSR_PWRRSTF)!=0u ||
       record->magic!=IH_RESET_DIAG_MAGIC ||
       record->cause<IH_RESET_CAUSE_EMERGENCY ||
       record->cause>IH_RESET_CAUSE_NMI ||
       record->inverted_cause!=(~record->cause))
        return IH_RESET_CAUSE_NONE;
    if(stage)*stage=record->stage;
    return record->cause;
}
static inline const char *IH_ResetDiag_CauseName(uint32_t cause)
{
    switch(cause)
    {
    case IH_RESET_CAUSE_EMERGENCY:return "EMERGENCY";
    case IH_RESET_CAUSE_ERROR_HANDLER:return "ERROR_HANDLER";
    case IH_RESET_CAUSE_ASSERT:return "ASSERT";
    case IH_RESET_CAUSE_HARDFAULT:return "HARDFAULT";
    case IH_RESET_CAUSE_NMI:return "NMI";
    default:return "NONE";
    }
}
#endif
