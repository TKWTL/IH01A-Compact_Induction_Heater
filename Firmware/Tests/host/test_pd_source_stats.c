/* Host regression of Source PDO decode; no HAL, RTOS or real USBPD library. */
#include "algorithm/pd_source_stats.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void check_row(uint8_t row, const char *expected)
{
    char actual[64];
    PD_SourceStats_FormatRow(row, actual, sizeof(actual));
    if(strcmp(actual, expected) != 0)
        printf("PDO %u: actual=[%s], expected=[%s]\n",
               (unsigned)row, actual, expected);
    assert(strcmp(actual, expected) == 0);
}

int main(void)
{
    uint32_t objects[7] = {0};
    uint32_t revision;

    PD_SourceStats_Clear();
    check_row(0, "No PD Source");
    check_row(1, "");
    assert(PD_SourceStats_Count() == 0u);

    objects[0] = (100u << 10) | 300u; /* Fixed 5V, 3A */
    objects[1] = (180u << 10) | 300u; /* Fixed 9V, 3A */
    revision = PD_SourceStats_Revision();
    PD_SourceStats_Receive((const uint8_t *)objects, 8u);
    assert(PD_SourceStats_Revision() != revision);
    assert(PD_SourceStats_Count() == 2u);
    check_row(0, "1. Fixed 5.0V 3.0A");
    check_row(1, "2. Fixed 9.0V 3.0A");
    check_row(2, "");

    objects[2] = (3u << 30) | (110u << 17) | (33u << 8) | 60u;
    PD_SourceStats_Receive((const uint8_t *)objects, 12u);
    check_row(2, "3. PPS 3.3-11.0V 3.0A");

    objects[0] = (1u << 30) | (400u << 20) | (100u << 10) | 180u;
    PD_SourceStats_Receive((const uint8_t *)objects, 4u);
    check_row(0, "1. Batt 5-20V 45.0W");

    objects[0] = (2u << 30) | (240u << 20) | (100u << 10) | 300u;
    PD_SourceStats_Receive((const uint8_t *)objects, 4u);
    check_row(0, "1. Var 5.0-12.0V 3.0A");

    PD_SourceStats_Receive((const uint8_t *)objects, 3u);
    assert(PD_SourceStats_Count() == 0u);
    check_row(0, "No PD Source");
    PD_SourceStats_Receive((const uint8_t *)objects, 32u);
    assert(PD_SourceStats_Count() == 0u);

    /* EPR only: position 8 starts after seven SPR positions, including
     * vacant slots 6 and 7. No EPR must preserve the SPR list. */
    {
        uint32_t epr[2], combined[9] = {0u};
        epr[0] = (560u << 10) | 500u;  /* 28V/5A EPR Fixed */
        epr[1] = (3u << 30) | (1u << 28) | (280u << 17) | (150u << 8) | 140u;
        objects[0] = (100u << 10) | 300u;
        objects[1] = (400u << 10) | 500u;
        PD_SourceStats_Receive((const uint8_t *)objects, 8u);
        assert(PD_SourceStats_Count() == 2u);
        PD_SourceStats_ReceiveEPR((const uint8_t *)epr, 8u);
        assert(PD_SourceStats_Count() == 9u);
        check_row(5, ""); check_row(6, "");
        check_row(7, "8. EPR Fixed 28.0V 5.0A");
        check_row(8, "9. EPR AVS 15.0-28.0V 140W");
        PD_SourceStats_ClearEPR();
        assert(PD_SourceStats_Count() == 2u);
        check_row(1, "2. Fixed 20.0V 5.0A");
        check_row(7, "");
        combined[0] = objects[0]; combined[1] = objects[1];
        combined[7] = epr[0]; combined[8] = epr[1];
        PD_SourceStats_ReceiveEPR((const uint8_t *)combined, sizeof(combined));
        assert(PD_SourceStats_Count() == 9u);
        check_row(7, "8. EPR Fixed 28.0V 5.0A");
        check_row(8, "9. EPR AVS 15.0-28.0V 140W");
        PD_SourceStats_ReceiveEPR((const uint8_t *)combined, 28u);
        assert(PD_SourceStats_Count() == 7u);
        check_row(7, "");
        PD_SourceStats_ReceiveEPR((const uint8_t *)combined, 3u);
        assert(PD_SourceStats_Count() == 7u);
        /* New SPR source capabilities invalidate EPR metadata. */
        PD_SourceStats_Receive((const uint8_t *)objects, 8u);
        assert(PD_SourceStats_Count() == 2u);
    }

    PD_SourceStats_Clear(); /* detach/hard reset path */
    check_row(0, "No PD Source");
    puts("pd_source_stats: PASS");
    return 0;
}
