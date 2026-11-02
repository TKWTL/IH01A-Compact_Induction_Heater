/* USB-PD Source capabilities read-only mirror.
 * The PE calls Receive() from USBPD_DPM_SetDataInfo() after receiving
 * Source_Capabilities. No requested-voltage / negotiated-contract guesses.
 * No dynamic memory, no writes to protocol state.
 */
#include "pd_source_stats.h"
#include <stdio.h>
#include <string.h>

static uint32_t s_pdos[PD_SOURCE_MAX_PDOS];
static uint8_t s_count, s_epr_count;
static uint32_t s_revision;

void PD_SourceStats_ClearEPR(void)
{
    uint8_t j;
    for(j = PD_SOURCE_MAX_SPR_PDOS; j < PD_SOURCE_MAX_PDOS; ++j)
        s_pdos[j] = 0u;
    s_epr_count = 0u;
    ++s_revision;
}

void PD_SourceStats_Clear(void)
{
    uint8_t i;
    s_count = 0u;
    s_epr_count = 0u;
    for(i = 0u; i < PD_SOURCE_MAX_PDOS; i++)
        s_pdos[i] = 0u;
    ++s_revision;
}

void PD_SourceStats_Receive(const uint8_t *data, uint32_t length_bytes)
{
    uint8_t i;
    uint8_t count;
    /* New SPR capabilities invalidate the previous EPR list. */
    PD_SourceStats_ClearEPR();
    if(data == 0 || length_bytes == 0u ||
       (length_bytes & 3u) != 0u ||
       length_bytes > 4u * PD_SOURCE_MAX_SPR_PDOS)
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

/* ST FULL may pass just the EPR objects (4..24 B) or a combined
 * list with seven SPR positions followed by EPR objects (32..52 B).
 * 28 B is ambiguous and rejected instead of shifting object numbers. */
void PD_SourceStats_ReceiveEPR(const uint8_t *data, uint32_t length_bytes)
{
    uint8_t i, count;
    uint8_t start;
    if(data == 0 || length_bytes == 0u || (length_bytes & 3u) != 0u ||
       length_bytes == 28u || length_bytes > 4u * PD_SOURCE_MAX_PDOS)
    {
        PD_SourceStats_ClearEPR();
        return;
    }
    if(length_bytes <= 4u * PD_SOURCE_MAX_EPR_PDOS)
    {
        start = 0u;
        count = (uint8_t)(length_bytes / 4u);
    }
    else if(length_bytes >= 32u)
    {
        start = PD_SOURCE_MAX_SPR_PDOS;
        count = (uint8_t)(length_bytes / 4u - start);
        for(i = 0u; i < PD_SOURCE_MAX_SPR_PDOS; ++i)
            memcpy(&s_pdos[i], data + 4u * i, sizeof(uint32_t));
        s_count = PD_SOURCE_MAX_SPR_PDOS;
    }
    else
    {
        PD_SourceStats_ClearEPR();
        return;
    }
    for(i = 0u; i < PD_SOURCE_MAX_EPR_PDOS; ++i)
    {
        s_pdos[PD_SOURCE_MAX_SPR_PDOS + i] = 0u;
        if(i < count)
            memcpy(&s_pdos[PD_SOURCE_MAX_SPR_PDOS + i],
                   data + (uint32_t)(start + i) * 4u, sizeof(uint32_t));
    }
    s_epr_count = count;
    ++s_revision;
}

uint32_t PD_SourceStats_Revision(void) { return s_revision; }
uint8_t PD_SourceStats_Count(void)
{
    return s_epr_count ? (uint8_t)(PD_SOURCE_MAX_SPR_PDOS + s_epr_count) : s_count;
}

/* Display values with one decimal digit using integer-only printf.
 * Fixed voltage: 50mV/count; fixed current: 10mA/count.
 * Variable/Battery: min/max in 50mV; battery power in 250mW.
 * PPS: voltage in 100mV/count; current in 50mA/count.
 * Unknown APDO subtypes are identified but NOT misdecoded as PPS. */
void PD_SourceStats_FormatRow(uint8_t index, char *out, size_t capacity)
{
    uint32_t pdo, type, v, i, vmin, vmax, power;
    uint8_t epr;
    if(out == 0 || capacity == 0u) return;
    out[0] = '\0';
    if(s_count == 0u && s_epr_count == 0u)
    {
        if(index == 0u) (void)snprintf(out, capacity, "No PD Source");
        return;
    }
    if(index >= PD_SourceStats_Count() || index >= PD_SOURCE_MAX_PDOS) return;
    if(index >= s_count && index < PD_SOURCE_MAX_SPR_PDOS) return;
    epr = (uint8_t)(index >= PD_SOURCE_MAX_SPR_PDOS);
    pdo = s_pdos[index];
    if(pdo == 0u) return; /* empty reserved slot */
    type = pdo >> 30u;
    if(type == 0u)
    {
        v = (((pdo >> 10u) & 0x3FFu) + 1u) / 2u; /* 50mV -> 0.1V */
        i = ((pdo & 0x3FFu) + 5u) / 10u;         /* 10mA -> 0.1A */
        (void)snprintf(out, capacity, epr ? "%u. EPR Fixed %u.%uV %u.%uA" :
                                             "%u. Fixed %u.%uV %u.%uA",
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
    else if(type == 3u && ((pdo >> 28u) & 3u) == 1u && epr)
    {
        /* EPR AVS: min/max 0.1V, PDP in 1W. */
        vmin = (pdo >> 8u) & 0xFFu;
        vmax = (pdo >> 17u) & 0x1FFu;
        power = pdo & 0xFFu;
        (void)snprintf(out, capacity, "%u. EPR AVS %u.%u-%u.%uV %uW",
                       (unsigned)(index + 1u),
                       (unsigned)(vmin / 10u), (unsigned)(vmin % 10u),
                       (unsigned)(vmax / 10u), (unsigned)(vmax % 10u),
                       (unsigned)power);
    }
    else if(type == 3u && ((pdo >> 28u) & 3u) == 2u && !epr)
    {
        (void)snprintf(out, capacity, "%u. SPR AVS 9-20V",
                       (unsigned)(index + 1u));
    }
    else
    {
        (void)snprintf(out, capacity, "%u. %s 0x%08lX",
                       (unsigned)(index + 1u), epr ? "EPR" : "APDO",
                       (unsigned long)pdo);
    }
}
