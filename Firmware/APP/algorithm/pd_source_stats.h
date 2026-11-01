#ifndef IH01A_PD_SOURCE_STATS_H
#define IH01A_PD_SOURCE_STATS_H

#include <stdint.h>
#include <stddef.h>

/* Up to seven 32-bit Source Power Data Objects, owned by this module.
 * No malloc and no dependency on UI or the USBPD protocol library. */
#define PD_SOURCE_MAX_PDOS 7u

void PD_SourceStats_Receive(const uint8_t *data, uint32_t length_bytes);
void PD_SourceStats_Clear(void);
uint32_t PD_SourceStats_Revision(void);
uint8_t PD_SourceStats_Count(void);
void PD_SourceStats_FormatRow(uint8_t index, char *out, size_t capacity);

#endif
