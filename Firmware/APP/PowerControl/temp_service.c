/* Low-frequency temperature service, modeled after Collar-Radio
 * application-level periodic ADC processing. Does NOT start ADC or DMA:
 * the existing 20kHz 8-rank ISR continues to sample/filter only.
 */
#include "temp_service.h"
#include "conv_adc.h"
#include "conv_controller.h"
#include "algorithm/ntc_lut.h"
#include "main.h"
static TempService_Snapshot s_temp;
static uint32_t s_last_poll, s_last_frame;
static uint8_t s_started;
void TempService_Poll(void)
{
    ConvADC_Snapshot adc;
    uint32_t now=HAL_GetTick();
    int32_t mcu;
    if(s_started && (uint32_t)(now-s_last_poll)<TEMP_SERVICE_PERIOD_MS)
        return;
    s_started=1u;
    s_last_poll=now;
    s_temp.valid=0u;
    if(ConvController_GetState()!=CONV_STATE_ADC_MONITOR)return;
    ConvADC_CopySnapshot(&adc);
    if(!adc.frames || adc.frames==s_last_frame)return;
    s_last_frame=adc.frames;
    s_temp.adc_frames=adc.frames;
    s_temp.updated_ms=now;
    s_temp.sys_adc=adc.filtered[CONV_ADC_NTC_SYS];
    s_temp.pot_adc=adc.filtered[CONV_ADC_NTC_POT];
    s_temp.mcu_adc=adc.filtered[CONV_ADC_MCU_TEMP];
    if(NTC_SYS_Convert(s_temp.sys_adc,&s_temp.sys_deci_c))
        s_temp.valid|=TEMP_VALID_SYS;
    if(NTC_POT_Convert(s_temp.pot_adc,&s_temp.pot_deci_c))
        s_temp.valid|=TEMP_VALID_POT;
    if(ConvADC_McuTempDeciC(s_temp.mcu_adc,&mcu))
    {
        s_temp.mcu_deci_c=mcu;
        s_temp.valid|=TEMP_VALID_MCU;
    }
}
void TempService_GetSnapshot(TempService_Snapshot *out)
{
    if(out)*out=s_temp; /* only called from the cooperative main loop */
}
