/* Host regression of Source PDO decode; no HAL, RTOS or real USBPD library. */
#include "pd_source_stats.h"
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

    PD_SourceStats_Clear(); /* detach/hard reset path */
    check_row(0, "No PD Source");
    puts("pd_source_stats: PASS");
    return 0;
}
