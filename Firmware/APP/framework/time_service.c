/* IH01A time service, adapted from Collar-Radio Algorithms/time.c:
 * keep the safe field-by-field copy of HAL RTC's packed time/date,
 * discard wall-clock editing UI, FM/FreeRTOS integration and floats.
 *
 * All HAL_RTC_GetTime calls MUST be followed by HAL_RTC_GetDate to
 * unlock shadow registers. Only called from cooperative foreground.
 */
#include "framework/time_service.h"
#include "rtc.h"
#include <stdio.h>

static uint32_t s_boot_seconds;
static uint8_t s_boot_valid;

static uint8_t Time_DaysInMonth(uint32_t year, uint32_t month)
{
    static const uint8_t days[12] =
        {31u,28u,31u,30u,31u,30u,31u,31u,30u,31u,30u,31u};
    uint8_t result;
    if(month < 1u || month > 12u) return 0u;
    result = days[month - 1u];
    if(month == 2u && ((year & 3u) == 0u)) result = 29u;
    return result;
}

uint8_t Time_CalendarToSeconds(const Time_Calendar *calendar,
                               uint32_t *seconds)
{
    uint32_t y, m, day_of_year = 0u;
    if(calendar == 0 || seconds == 0 ||
       calendar->year < 2000u || calendar->year > 2099u ||
       calendar->month < 1u || calendar->month > 12u ||
       calendar->day < 1u ||
       calendar->day > Time_DaysInMonth(calendar->year, calendar->month) ||
       calendar->hour > 23u || calendar->minute > 59u ||
       calendar->second > 59u) return 0u;

    for(m=1u; m<calendar->month; ++m)
        day_of_year += Time_DaysInMonth(calendar->year, m);
    day_of_year += calendar->day - 1u;
    y = calendar->year - 2000u;
    /* Leap years: 2000, 2004...2096; 2100 excluded by validation. */
    *seconds = ((y * 365u + (y + 3u)/4u + day_of_year)*86400u) +
               calendar->hour*3600u +
               calendar->minute*60u + calendar->second;
    return 1u;
}

uint8_t Time_ReadCalendar(Time_Calendar *out)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    if(out == 0) return 0u;
    if(HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return 0u;
    /* Mandatory paired read of RTC date after RTC time. */
    if(HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return 0u;
    out->year = 2000u + (uint32_t)d.Year;
    out->month = (uint32_t)d.Month;
    out->day = (uint32_t)d.Date;
    out->hour = (uint32_t)t.Hours;
    out->minute = (uint32_t)t.Minutes;
    out->second = (uint32_t)t.Seconds;
    return 1u;
}

void Time_Init(void)
{
    Time_Calendar now;
    s_boot_valid = 0u;
    s_boot_seconds = 0u;
    /* Called once after MX_RTC_Init resets the calendar on every boot. */
    if(Time_ReadCalendar(&now) &&
       Time_CalendarToSeconds(&now, &s_boot_seconds))
        s_boot_valid = 1u;
}

uint8_t Time_GetUptimeSeconds(uint32_t *seconds)
{
    Time_Calendar now;
    uint32_t value;
    if(seconds == 0 || !s_boot_valid) return 0u;
    if(!Time_ReadCalendar(&now) ||
       !Time_CalendarToSeconds(&now, &value)) return 0u;
    if(value < s_boot_seconds) return 0u;
    *seconds = value - s_boot_seconds;
    return 1u;
}

void Time_SplitDuration(uint32_t seconds, Time_Duration *out)
{
    if(out == 0) return;
    out->days = seconds / 86400u;
    seconds %= 86400u;
    out->hours = (uint8_t)(seconds / 3600u);
    seconds %= 3600u;
    out->minutes = (uint8_t)(seconds / 60u);
    out->seconds = (uint8_t)(seconds % 60u);
}

void Time_FormatDuration(uint32_t seconds, char *out, size_t size)
{
    Time_Duration d;
    if(out == 0 || size == 0u) return;
    Time_SplitDuration(seconds, &d);
    (void)snprintf(out, size, "%ud %02u:%02u:%02u",
                   (unsigned)d.days, (unsigned)d.hours,
                   (unsigned)d.minutes, (unsigned)d.seconds);
}
