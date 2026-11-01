#ifndef IH01A_FRAMEWORK_TIME_SERVICE_H
#define IH01A_FRAMEWORK_TIME_SERVICE_H

#include <stdint.h>
#include <stddef.h>

/* RTC-based relative time (not wall-clock time). No backup supply exists.
 * CubeMX starts RTC calendar at 2000-01-01 00:00:00 on each MCU boot.
 * No timezone, DST, epoch synchronization or heap allocations. */
typedef struct
{
    uint32_t year;     /* 2000..2099 */
    uint32_t month;    /* 1..12 */
    uint32_t day;      /* 1..31, calendar-validated */
    uint32_t hour;     /* 0..23 */
    uint32_t minute;   /* 0..59 */
    uint32_t second;   /* 0..59 */
} Time_Calendar;

typedef struct
{
    uint32_t days;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} Time_Duration;

void Time_Init(void);
uint8_t Time_ReadCalendar(Time_Calendar *out);
uint8_t Time_CalendarToSeconds(const Time_Calendar *calendar,
                               uint32_t *seconds);
uint8_t Time_GetUptimeSeconds(uint32_t *seconds);
void Time_SplitDuration(uint32_t seconds, Time_Duration *out);
void Time_FormatDuration(uint32_t seconds, char *out, size_t size);

#endif
