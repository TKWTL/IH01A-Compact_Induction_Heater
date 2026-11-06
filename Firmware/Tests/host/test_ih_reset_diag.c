/* Host-only validation of warm-reset forensics; no STM32 HAL is needed. */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "ih_reset_diag.h"
int main(void)
{
    IH_ResetDiagRecord d={IH_RESET_DIAG_MAGIC,
                         IH_RESET_CAUSE_HARDFAULT,
                         ~IH_RESET_CAUSE_HARDFAULT,4u};
    uint32_t stage=999u,reason;
    reason=IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF,&d,&stage);
    assert(reason==IH_RESET_CAUSE_HARDFAULT && stage==4u);
    assert(strcmp(IH_ResetDiag_CauseName(reason),"HARDFAULT")==0);
    reason=IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF |
                                  IH_RESET_CSR_PWRRSTF,&d,&stage);
    assert(reason==IH_RESET_CAUSE_NONE && stage==0u);
    d.inverted_cause=0u;
    assert(IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF,&d,&stage)==0u);
    d.inverted_cause=~IH_RESET_CAUSE_ERROR_HANDLER;
    d.cause=IH_RESET_CAUSE_ERROR_HANDLER;
    assert(IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF,&d,&stage)==
           IH_RESET_CAUSE_ERROR_HANDLER);
    d.magic=0u;
    assert(IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF,&d,&stage)==0u);
    d.magic=IH_RESET_DIAG_MAGIC;
    assert(IH_ResetDiag_PriorCause(0u,&d,&stage)==0u);
    d.cause=90u;d.inverted_cause=~d.cause;
    assert(IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF,&d,&stage)==0u);
    assert(IH_ResetDiag_PriorCause(IH_RESET_CSR_SFTRSTF,0,&stage)==0u);
    puts("reset diagnostics: PASS");
    return 0;
}
