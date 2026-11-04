#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "heater_cli.h"
#include "heater_cli_mock.h"

static char s_reply[256];
static uint32_t s_now=1000u;
static uint16_t s_phase=90u;
static HeaterSnapshot s_heater;
static ConvADC_Snapshot s_adc;
static ConvProtectionSnapshot s_protection;
static ConvZcdSnapshot s_zcd;
static ConvState s_state=CONV_STATE_ADC_MONITOR;
static uint8_t s_enabled,s_on_allowed,s_block;
static HeaterCliMockTim s_timer={639u};
HeaterCliMockTim *TIM1=&s_timer;

int HeaterCLI_MockPrintf(const char *fmt,...)
{
    va_list ap;
    int size;
    va_start(ap,fmt);
    size=vsnprintf(s_reply,sizeof(s_reply),fmt,ap);
    va_end(ap);
    return size;
}
uint32_t HAL_GetTick(void){return s_now;}
void pm_api_refresh_idle(void){}
void Heater_GetSnapshot(HeaterSnapshot *out){*out=s_heater;}
void Heater_SetPhaseDegrees(uint16_t value)
{
    s_heater.target_phase_deg=value;
}
void Heater_Stop(void)
{
    s_heater.state=HEATER_OFF;
}
void ConvADC_CopySnapshot(ConvADC_Snapshot *out){*out=s_adc;}
void ConvProtection_GetSnapshot(ConvProtectionSnapshot *out){*out=s_protection;}
void ConvZcd_Poll(uint32_t now){(void)now;}
void ConvZcd_GetSnapshot(ConvZcdSnapshot *out){*out=s_zcd;}
ConvZcdCalStatus ConvZcd_CorrectionStatus(const ConvZcdSnapshot *v,
                                           uint32_t ct_ma,uint8_t valid)
{
    if(!v || !v->valid)return CONV_ZCD_CAL_NO_CAPTURE;
    if(!v->correction_valid)return CONV_ZCD_CAL_FREQ_RANGE;
    if(v->phase_deg<120u || v->phase_deg>180u)
        return CONV_ZCD_CAL_PHASE_RANGE;
    if(!valid)return CONV_ZCD_CAL_ADC_INVALID;
    if(ct_ma<50u)return CONV_ZCD_CAL_LOW_CURRENT;
    return CONV_ZCD_CAL_FIT;
}
uint8_t ConvController_OutputEnabled(void){return s_enabled;}
uint8_t ConvController_OutputPinLevel(void){return s_enabled;}
uint16_t ConvController_GetPhaseDegrees(void){return s_phase;}
uint16_t ConvController_GetFrequencyKhz(void)
{
    return ConvPhase_KHzFromArr((uint16_t)TIM1->ARR);
}
void ConvController_SetPhaseDegrees(uint16_t value)
{
    s_phase=value;
}
uint8_t ConvController_SetFrequencyKhz(uint16_t khz)
{
    uint16_t arr;
    if(!ConvPhase_ArrFromKHz(khz,&arr))return 0u;
    TIM1->ARR=arr;
    return 1u;
}
uint8_t ConvController_RequestBenchRunNow(uint32_t now)
{
    (void)now;
    s_enabled=s_on_allowed;
    s_block=s_on_allowed?CONV_BENCH_OK:CONV_BENCH_NOPD;
    if(s_enabled)s_heater.state=HEATER_RUN_REQUESTED;
    return s_enabled;
}
void ConvController_BenchPowerOff(void)
{
    s_enabled=0u;
}
uint8_t ConvController_BenchBlockReason(void){return s_block;}
ConvState ConvController_GetState(void){return s_state;}

static void send(const char *cmd,const char *expected)
{
    s_reply[0]='\0';
    assert(HeaterCLI_Dispatch(cmd)==1u);
    assert(strcmp(s_reply,expected)==0);
}

int main(void)
{
    s_heater.state=HEATER_OFF;
    s_heater.target_phase_deg=90u;
    s_protection.zero_status=CONV_ZERO_WAIT;
    s_adc.frames=0u;
    s_on_allowed=0u;
    assert(HeaterCLI_Dispatch("ping")==0u);
    send("heater phase 180","HCLI OK PHASE_DEG=180\r\n");
    send("heater phase 181","HCLI ERR RANGE\r\n");
    send("heater freq 150","HCLI OK FREQ_KHZ=150\r\n");
    send("heater freq 400","HCLI OK FREQ_KHZ=400\r\n");
    send("heater freq 149","HCLI ERR RANGE\r\n");
    send("heater freq xyz","HCLI ERR SYNTAX\r\n");
    send("heater on","HCLI ERR HEATER_BLOCK=NO_PD\r\n");

    s_now=1100u;
    s_on_allowed=1u;
    send("heater on","HCLI OK HEATER=ON\r\n");
    send("heater freq 200","HCLI OK FREQ_KHZ=200\r\n");
    s_zcd.valid=1u;
    s_zcd.last_ms=1090u;
    s_zcd.capture_raw=444u;
    s_zcd.lag_deci_deg=1149; /* raw */
    s_zcd.lag_corr_deci_deg=950; /* 95.0deg host-injected correction */
    s_zcd.correction_valid=1u;
    s_zcd.phase_deg=180u;
    s_adc.frames=1u;
    s_adc.raw[CONV_ADC_CT_RMS]=1389u;
    s_adc.filtered[CONV_ADC_CT_RMS]=1389u;
    s_protection.zero_status=CONV_ZERO_READY;
    s_protection.ct_zero_raw=1100u;
    s_reply[0]='\0';
    assert(HeaterCLI_Dispatch("heater get"));
    assert(strstr(s_reply,"HCLI OK HEATER=ON PHASE_DEG=180 FREQ_KHZ=200 FREQ_HZ=200000 ")==s_reply);
    assert(strstr(s_reply,"IC2=444 LAG_DEG=+95.0 LAG_RAW_DEG=+114.9 LAG_QUAL=FIT CT_RMS_A=0.699")!=0 ||
           strstr(s_reply,"IC2=444 LAG_DEG=+95.0 LAG_RAW_DEG=+114.9 LAG_QUAL=FIT CT_RMS_A=0.700")!=0);
    s_zcd.last_ms=0u;
    s_reply[0]='\0';
    assert(HeaterCLI_Dispatch("heater get"));
    assert(strstr(s_reply,"IC2=NA LAG_DEG=NA LAG_RAW_DEG=NA LAG_QUAL=NO_IC2")!=0);
    s_adc.raw[CONV_ADC_CT_RMS]=4095u;
    s_zcd.last_ms=1090u; /* valid capture, but CT ADC is saturated */
    s_reply[0]='\0';
    assert(HeaterCLI_Dispatch("heater get"));
    assert(strstr(s_reply,"LAG_QUAL=ADC")!=0);
    assert(strstr(s_reply,"LAG_DEG=NA LAG_RAW_DEG=+114.9") != 0);
    assert(strstr(s_reply,"CT_RMS_A=SAT")!=0);
    /* With a valid capture, low CT level suppresses correction only.
     * Raw capture and raw phase must remain observable to the agent. */
    s_adc.raw[CONV_ADC_CT_RMS]=1110u;
    s_adc.filtered[CONV_ADC_CT_RMS]=1110u;
    s_reply[0]='\0';
    assert(HeaterCLI_Dispatch("heater get"));
    assert(strstr(s_reply,"LAG_DEG=NA LAG_RAW_DEG=+114.9 LAG_QUAL=LOW_CT")!=0);
    s_adc.raw[CONV_ADC_CT_RMS]=1389u;
    s_adc.filtered[CONV_ADC_CT_RMS]=1389u;
    s_zcd.correction_valid=0u; /* outside calibrated frequency band */
    s_reply[0]='\0';
    assert(HeaterCLI_Dispatch("heater get"));
    assert(strstr(s_reply,"LAG_QUAL=FREQ")!=0);
    send("heater off","HCLI OK HEATER=OFF\r\n");
    puts("heater CLI command: PASS");
    return 0;
}
