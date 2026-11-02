#ifndef IH01A_PD_SOURCE_STATS_H
#define IH01A_PD_SOURCE_STATS_H

#include <stdint.h>
#include <stddef.h>

/* SPR positions 1..7 plus EPR positions 8..13, stored independently.
 * No malloc and no dependency on UI or the USBPD protocol library. */
#define PD_SOURCE_MAX_SPR_PDOS 7u
#define PD_SOURCE_MAX_EPR_PDOS 6u
#define PD_SOURCE_MAX_PDOS 13u

void PD_SourceStats_Receive(const uint8_t *data, uint32_t length_bytes);
void PD_SourceStats_ReceiveEPR(const uint8_t *data, uint32_t length_bytes);
void PD_SourceStats_ClearEPR(void);
void PD_SourceStats_Clear(void);
uint32_t PD_SourceStats_Revision(void);
uint8_t PD_SourceStats_Count(void);
void PD_SourceStats_FormatRow(uint8_t index, char *out, size_t capacity);

#endif
