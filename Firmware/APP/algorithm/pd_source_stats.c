/* USB-PD Source capabilities read-only mirror.
 * The PE calls Receive() from USBPD_DPM_SetDataInfo() after receiving
 * Source_Capabilities. No requested-voltage / negotiated-contract guesses.
 * No dynamic memory, no writes to protocol state.
 */
#include "pd_source_stats.h"
#include <stdio.h>
#include <string.h>

static uint32_t s_pdos[PD_SOURCE_MAX_PDOS];
static uint8_t s_count;
static uint32_t s_revision;

void PD_SourceStats_Clear(void)
{
    uint8_t i;
    s_count = 0u;
    for(i = 0u; i < PD_SOURCE_MAX_PDOS; i++)
        s_pdos[i] = 0u;
    ++s_revision;
}

void PD_SourceStats_Receive(const uint8_t *data, uint32_t length_bytes)
{
    uint8_t i;
    uint8_t count;
    if(data == 0 || length_bytes == 0u ||
       (length_bytes & 3u) != 0u ||
       length_bytes > 4u * PD_SOURCE_MAX_PDOS)
    {
        PD_SourceStats_Clear();
        return;
    }

    count = (uint8_t)(length_bytes / 4u);
    s_count = 0u;
    /* memcpy avoids unaligned 32-bit accesses to USBPD callback buffers. */
    for(i = 0u; i < count; i++)
        memcpy(&s_pdos[i], data + 4u * i, sizeof(uint32_t));
    for(; i < PD_SOURCE_MAX_PDOS; i++)
        s_pdos[i] = 0u;
    s_count = count;
    ++s_revision;
}

uint32_t PD_SourceStats_Revision(void) { return s_revision; }
uint8_t PD_SourceStats_Count(void) { return s_count; }

/* Display values with one decimal digit using integer-only printf.
 * Fixed voltage: 50mV/count; fixed current: 10mA/count.
 * Variable/Battery: min/max in 50mV; battery power in 250mW.
 * PPS: voltage in 100mV/count; current in 50mA/count.
 * Unknown APDO subtypes are identified but NOT misdecoded as PPS. */
void PD_SourceStats_FormatRow(uint8_t index, char *out, size_t capacity)
{
    uint32_t pdo, type, v, i, vmin, vmax, power;
    if(out == 0 || capacity == 0u) return;
    out[0] = '\0';
    if(s_count == 0u)
    {
        if(index == 0u) (void)snprintf(out, capacity, "No PD Source");
        return;
    }
    if(index >= s_count || index >= PD_SOURCE_MAX_PDOS) return;

    pdo = s_pdos[index];
    type = pdo >> 30u;
    if(type == 0u)
    {
        v = (((pdo >> 10u) & 0x3FFu) + 1u) / 2u; /* 50mV -> 0.1V */
        i = ((pdo & 0x3FFu) + 5u) / 10u;         /* 10mA -> 0.1A */
        (void)snprintf(out, capacity, "%u. Fixed %u.%uV %u.%uA",
                       (unsigned)(index + 1u), (unsigned)(v / 10u),
                       (unsigned)(v % 10u), (unsigned)(i / 10u),
                       (unsigned)(i % 10u));
    }
    else if(type == 2u)
    {
        vmin = (((pdo >> 10u) & 0x3FFu) + 1u) / 2u;
        vmax = (((pdo >> 20u) & 0x3FFu) + 1u) / 2u;
        i = ((pdo & 0x3FFu) + 5u) / 10u;
        (void)snprintf(out, capacity, "%u. Var %u.%u-%u.%uV %u.%uA",
                       (unsigned)(index + 1u),
                       (unsigned)(vmin / 10u), (unsigned)(vmin % 10u),
                       (unsigned)(vmax / 10u), (unsigned)(vmax % 10u),
                       (unsigned)(i / 10u), (unsigned)(i % 10u));
    }
    else if(type == 1u)
    {
        vmin = (((pdo >> 10u) & 0x3FFu) + 1u) / 2u;
        vmax = (((pdo >> 20u) & 0x3FFu) + 1u) / 2u;
        power = (pdo & 0x3FFu) * 25u; /* 250mW/count -> 0.01W */
        (void)snprintf(out, capacity, "%u. Batt %u-%uV %u.%uW",
                       (unsigned)(index + 1u),
                       (unsigned)(vmin / 10u), (unsigned)(vmax / 10u),
                       (unsigned)(power / 100u), (unsigned)((power / 10u) % 10u));
    }
    else if(((pdo >> 28u) & 3u) == 0u)
    {
        /* Standard PPS APDO, NOT AVS or a vendor extension. */
        vmin = (pdo >> 8u) & 0xFFu;  /* 100mV -> 0.1V */
        vmax = (pdo >> 17u) & 0xFFu;
        i = ((pdo & 0x7Fu) + 1u) / 2u; /* 50mA -> 0.1A */
        (void)snprintf(out, capacity, "%u. PPS %u.%u-%u.%uV %u.%uA",
                       (unsigned)(index + 1u),
                       (unsigned)(vmin / 10u), (unsigned)(vmin % 10u),
                       (unsigned)(vmax / 10u), (unsigned)(vmax % 10u),
                       (unsigned)(i / 10u), (unsigned)(i % 10u));
    }
    else
    {
        (void)snprintf(out, capacity, "%u. APDO 0x%08X",
                       (unsigned)(index + 1u), (unsigned)pdo);
    }
}
