#ifndef IH01A_TEMP_SERVICE_H
#define IH01A_TEMP_SERVICE_H
#include <stdint.h>
#define TEMP_SERVICE_PERIOD_MS 100u
#define TEMP_VALID_SYS (1u<<0)
#define TEMP_VALID_POT (1u<<1)
#define TEMP_VALID_MCU (1u<<2)
typedef struct {
    int16_t sys_deci_c;
    int16_t pot_deci_c;
    int32_t mcu_deci_c;
    uint16_t sys_adc, pot_adc, mcu_adc;
    uint32_t updated_ms, adc_frames;
    uint8_t valid;
} TempService_Snapshot;
void TempService_Poll(void);
void TempService_GetSnapshot(TempService_Snapshot *out);
#endif
