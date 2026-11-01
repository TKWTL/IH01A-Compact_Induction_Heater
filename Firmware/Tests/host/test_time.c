#include "framework/time_service.h"
#include "rtc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

RTC_HandleTypeDef hrtc;
static RTC_TimeTypeDef current_time;
static RTC_DateTypeDef current_date;
static uint32_t read_time_count, read_date_count;
static uint8_t fail_date;

HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h,
                                  RTC_TimeTypeDef *t, uint32_t fmt)
{
    (void)h;
    assert(fmt==RTC_FORMAT_BIN);
    ++read_time_count;
    *t=current_time;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h,
                                  RTC_DateTypeDef *d, uint32_t fmt)
{
    (void)h;
    assert(fmt==RTC_FORMAT_BIN);
    ++read_date_count;
    if(fail_date) return HAL_ERROR;
    *d=current_date;
    return HAL_OK;
}

static void set_clock(uint8_t year, uint8_t month, uint8_t date,
                      uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    current_date.Year=year;
    current_date.Month=month;
    current_date.Date=date;
    current_date.WeekDay=1;
    current_time.Hours=hours;
    current_time.Minutes=minutes;
    current_time.Seconds=seconds;
}
static uint32_t calendar_seconds(uint32_t y,uint32_t m,uint32_t d)
{
    Time_Calendar x={y,m,d,0u,0u,0u};
    uint32_t value=0;
    assert(Time_CalendarToSeconds(&x,&value));
    return value;
}

int main(void)
{
    Time_Calendar bad;
    uint32_t seconds=999u, initial;
    Time_Duration duration;
    char text[32];

    /* CubeMX initializes RTC calendar at 2000-01-01 00:00:00. */
    set_clock(0,1,1,0,0,0);
    Time_Init();
    assert(Time_GetUptimeSeconds(&seconds) && seconds==0u);
    assert(read_time_count==read_date_count);

    set_clock(0,1,1,1,1,1);
    assert(Time_GetUptimeSeconds(&seconds) && seconds==3661u);
    Time_FormatDuration(seconds,text,sizeof(text));
    assert(strcmp(text,"0d 01:01:01")==0);

    set_clock(0,1,2,1,1,1);
    assert(Time_GetUptimeSeconds(&seconds) && seconds==90061u);
    Time_FormatDuration(seconds,text,sizeof(text));
    assert(strcmp(text,"1d 01:01:01")==0);
    Time_SplitDuration(seconds,&duration);
    assert(duration.days==1u&&duration.hours==1u&&
           duration.minutes==1u&&duration.seconds==1u);

    /* 2000 is leap, February 29 exists; 2001 is not leap. */
    initial=calendar_seconds(2000,1,1);
    assert(initial==0u);
    assert(calendar_seconds(2000,2,29)==59u*86400u);
    assert(calendar_seconds(2000,3,1)==60u*86400u);
    assert(calendar_seconds(2001,1,1)==366u*86400u);
    bad.year=2001;bad.month=2;bad.day=29;
    bad.hour=0;bad.minute=0;bad.second=0;
    assert(!Time_CalendarToSeconds(&bad,&seconds));

    /* Exact date/clock validation and no fabricated uptime on RTC errors. */
    bad.year=2100;
    assert(!Time_CalendarToSeconds(&bad,&seconds));
    set_clock(0,3,1,0,0,0);
    assert(Time_GetUptimeSeconds(&seconds)&&seconds==60u*86400u);
    fail_date=1u;
    assert(!Time_GetUptimeSeconds(&seconds));
    fail_date=0u;
    assert(read_time_count==read_date_count);

    Time_FormatDuration(10u,text,1u);
    assert(text[0]=='\0');
    puts("time service: PASS");
    return 0;
}
