#ifndef IH01A_TEST_RTC_MOCK_H
#define IH01A_TEST_RTC_MOCK_H
#include <stdint.h>
typedef struct { uint8_t Hours, Minutes, Seconds; } RTC_TimeTypeDef;
typedef struct { uint8_t WeekDay, Month, Date, Year; } RTC_DateTypeDef;
typedef struct { uint8_t unused; } RTC_HandleTypeDef;
typedef enum { HAL_OK=0, HAL_ERROR=1 } HAL_StatusTypeDef;
#define RTC_FORMAT_BIN 0u
extern RTC_HandleTypeDef hrtc;
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t,
                                  uint32_t format);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d,
                                  uint32_t format);
#endif
