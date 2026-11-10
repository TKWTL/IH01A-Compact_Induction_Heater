/* Main menu: a view/controller of the persistent Heater service.
 * No control loop, reservation countdown, or hardware PWM in this file.
 * ENTER cycles editable fields; L/R change the selected field; EXIT leaves.
 */
#include "PowerControl/heater_ui.h"
#include "PowerControl/heater_control.h"
#include "PowerControl/conv_controller.h"
#include "PowerControl/conv_phase.h"
#include "PowerControl/conv_zcd.h"
#include "PowerControl/conv_adc.h"
#include "PowerControl/conv_current.h"
#include "PowerControl/conv_protection.h"
#include "tim.h"
#include "display/dispDriver.h"
#include "framework/pm_api.h"
#include "main.h"
#include <stdio.h>

enum {EDIT_MODE=0u,EDIT_TARGET,EDIT_OFF,EDIT_PAN,EDIT_RUN,EDIT_COUNT};
/* Power bench default: L/R edits phase immediately on Main entry. */
static uint8_t s_edit_field=EDIT_TARGET;

static const char *state_name(HeaterState s)
{
    switch(s)
    {
    case HEATER_RUN_REQUESTED:return "REQ";
    case HEATER_FAULT:return "FAULT";
    default:return "STOP";
    }
}
static const char *field_name(uint8_t field)
{
    static const char *const names[EDIT_COUNT]={
        "MODE","TARGET","OFF","PAN","RUN"
    };
    return names[field<EDIT_COUNT?field:0u];
}
/* Why the last bench RUN was refused. Three characters only: the status line
 * already carries state/P/E and the panel is 21 characters wide. */
static const char *bench_block_name(uint8_t why)
{
    switch(why)
    {
    case CONV_BENCH_MODE:  return "MOD";
    case CONV_BENCH_PHASE: return "APP";
    case CONV_BENCH_NOPD:  return "NOPD";
    case CONV_BENCH_VOLT:  return "12V";
    case CONV_BENCH_CURR:  return "CUR";
    case CONV_BENCH_SOURCE:return "SRC";
    case CONV_BENCH_BUS:   return "BUS";
    case CONV_BENCH_ZERO:  return "ZERO";
    case CONV_BENCH_NTC:   return "NTC";
    case CONV_BENCH_HOT:   return "HOT";
    case CONV_BENCH_AGE:   return "AGE";
    case CONV_BENCH_PWM:   return "PWM";
    case CONV_BENCH_COMP:  return "COMP";
    case CONV_BENCH_FAULT: return "FLT";
    case CONV_BENCH_BRK:   return "BRK";
    case CONV_BENCH_COUNTDOWN:return "TIME";
    default:               return "";
    }
}
static void edit_value(uint8_t field,UI_ACTION action,uint32_t now)
{
    HeaterSnapshot s;
    uint8_t up=(action==UI_ACTION_DOWN)?1u:0u;
    Heater_GetSnapshot(&s);
    switch(field)
    {
    case EDIT_MODE:
        /* Switching control modes is an immediate bench power-off. */
        ConvController_BenchPowerOff();
        if(s.state==HEATER_RUN_REQUESTED)Heater_Stop();
        Heater_SetMode(s.mode==HEATER_MODE_TEMP?
                       HEATER_MODE_POWER:HEATER_MODE_TEMP);
        break;
    case EDIT_TARGET:
        if(s.mode==HEATER_MODE_TEMP)
        {
            int32_t v=(int32_t)s.target_temp_deci_c+(up?50:-50);
            if(v<400)v=400;
            if(v>1200)v=1200;
            Heater_SetTargetTemp((uint16_t)v);
        }
        else
        {
            int32_t v=(int32_t)s.target_phase_deg+(up?10:-10);
            if(v<0)v=0;
            if(v>180)v=180;
            Heater_SetPhaseDegrees((uint16_t)v);
            ConvController_SetPhaseDegrees((uint16_t)v); /* immediate scope */
        }
        break;
    case EDIT_OFF:
        {
            int32_t minutes=(int32_t)s.off_minutes+(up?5:-5);
            if(minutes<0)minutes=0;
            if(minutes>240)minutes=240;
            Heater_SetOffMinutes((uint16_t)minutes,now);
        }
        break;
    case EDIT_PAN:
        /* Refuse mode toggle during RUN or a latched fault. */
        (void)Heater_SetPanDetectEnabled((uint8_t)!s.pan_detect_enabled);
        break;
    case EDIT_RUN:
        if(up)
        {
            /* Explicit RUN also acknowledges a previous transient fault,
             * but only through the shared PC6/ADC/COMP/PD safety gate. */
            (void)ConvController_RequestBenchRunNow(now);
        }
        else
        {
            /* User stop must drop FD2204 EN immediately, not 100ms later. */
            ConvController_BenchPowerOff();
            Heater_Stop();
        }
        break;
    default:break;
    }
    pm_api_refresh_idle();
}
void Heater_UI_Page(ui_t *ui)
{
    char line[36];
    HeaterSnapshot s;
    uint32_t now=HAL_GetTick();
    if(!ui)return;
    if(ui->action==UI_ACTION_ENTER)
    {
        s_edit_field=(uint8_t)((s_edit_field+1u)%EDIT_COUNT);
        ui->action=UI_ACTION_NONE; /* prevent MiaoUI from exiting the page */
    }
    else if(ui->action==UI_ACTION_UP || ui->action==UI_ACTION_DOWN)
    {
        edit_value(s_edit_field,ui->action,now);
        ui->action=UI_ACTION_NONE;
    }
    else if(ui->action==UI_ACTION_BACK)
    {
        /* Both reservation timers survive returning to the parent menu. */
        return;
    }
    /* CH2 capture is polled by thread_heater, not by any UI page. */
    Heater_GetSnapshot(&s);
    Disp_ClearBuffer();
    Disp_SetFont(UI_FONT);
    {
        const char *kind=s.input==HEATER_INPUT_PD?"PD":
            s.input==HEATER_INPUT_DCIN?"DC":
            s.input==HEATER_INPUT_WAIT?"WAIT":"NO";
        if(ConvController_OutputEnabled())
            (void)snprintf(line,sizeof(line),"HEATER %c %s%luV %us",
                s.mode==HEATER_MODE_TEMP?'T':'P',kind,
                (unsigned long)((s.bus_mv+500u)/1000u),
                (unsigned)ConvController_BenchElapsedSeconds(now));
        else
            (void)snprintf(line,sizeof(line),"HEATER %c %s%luV",
                s.mode==HEATER_MODE_TEMP?'T':'P',kind,
                (unsigned long)((s.bus_mv+500u)/1000u));
        Disp_DrawStr(0u,11u,line);
    }
    {
        ConvADC_Snapshot adc;
        ConvProtectionSnapshot protection;
        char ibus_text[13];
        ConvADC_CopySnapshot(&adc);
        ConvProtection_GetSnapshot(&protection);
        if(ConvController_GetState()==CONV_STATE_ADC_MONITOR &&
           adc.frames && protection.zero_status==CONV_ZERO_READY &&
           adc.raw[CONV_ADC_IBUS]>5u && adc.raw[CONV_ADC_IBUS]<4090u)
        {
            int32_t ma=ConvCurrent_IbusMilliAmp(
                           adc.filtered[CONV_ADC_IBUS],
                           protection.ibus_zero_raw);
            uint32_t abs_ma=(uint32_t)(ma<0?-ma:ma);
            uint32_t centi=(abs_ma+5u)/10u;
            (void)snprintf(ibus_text,sizeof(ibus_text),"%s%lu.%02luA",
                ma<0?"-":"",(unsigned long)(centi/100u),
                (unsigned long)(centi%100u));
        }
        else (void)snprintf(ibus_text,sizeof(ibus_text),"--");
        /* Fit 128px OLED line; keep the IBUS label explicit. */
        if(s.pot_valid && s.sys_valid)
            (void)snprintf(line,sizeof(line),"P%d S%d IBUS:%s",
                (int)(s.pot_deci_c/10),(int)(s.sys_deci_c/10),
                ibus_text);
        else
            (void)snprintf(line,sizeof(line),"P/S:-- IBUS:%s",ibus_text);
    }
    Disp_DrawStr(0u,23u,line);
    if(s.mode==HEATER_MODE_TEMP)
        (void)snprintf(line,sizeof(line),"SET %uC P%luW",
            (unsigned)(s.target_temp_deci_c/10u),
            (unsigned long)(s.requested_power_mw/1000u));
    else
    {
        ConvPhaseConfig phase;
        if(ConvPhase_Calculate((uint16_t)TIM1->ARR,s.target_phase_deg,&phase))
            (void)snprintf(line,sizeof(line),"%3u deg C3:%u C4:%u",
                (unsigned)s.target_phase_deg,
                (unsigned)phase.ccr3,(unsigned)phase.ccr4);
        else
            (void)snprintf(line,sizeof(line),"PHASE --");
    }
    Disp_DrawStr(0u,35u,line);
    /* Auto-stop countdown only; there is no reservation/countdown start.
     * The timer keeps running while this menu item is closed. */
    if(s.state==HEATER_RUN_REQUESTED && s.off_minutes)
        (void)snprintf(line,sizeof(line),"OFF %02lu:%02lu",
            (unsigned long)(s.off_remaining_s/60u),
            (unsigned long)(s.off_remaining_s%60u));
    else
        (void)snprintf(line,sizeof(line),"OFF %um",(unsigned)s.off_minutes);
    /* During manual phase testing reserve one row for latest TIM1_CH2 edge.
     * Lag is relative to the model voltage +->- at ARR/4+D/2. */
    if(s.pan_detect_enabled)
        (void)snprintf(line,sizeof(line),"PAN:ON 150k 180deg");
    else if(s.mode==HEATER_MODE_POWER)
    {
        ConvZcdSnapshot z;
        ConvZcd_GetSnapshot(&z);
        if(z.valid && (uint32_t)(now-z.last_ms)<=250u)
        {
            /* Display the historical correction estimate whenever it is
             * computable. Do NOT treat it as a calibrated safety input. */
            int16_t lag=z.correction_valid?
                        z.lag_corr_deci_deg:z.lag_deci_deg;
            int16_t mag=(int16_t)(lag<0?-lag:lag);
            (void)snprintf(line,sizeof(line),"IC2:%u %c:%c%d.%d",
                           (unsigned)z.capture_raw,
                           z.correction_valid?'C':'R',lag<0?'-':'+',
                           (int)(mag/10),(int)(mag%10));
        }
        else (void)snprintf(line,sizeof(line),"IC2:-- C:--");
    }
    Disp_DrawStr(0u,47u,line);
    /* At 128px and ~6px/glyph, keep line <=21 ASCII characters.
     * P=TIM1 PWM output. E shows BOTH PC6 states: first character is the
     * commanded output latch (ODR), second is the pin actually read back
     * (IDR). 'E:YN' therefore means the firmware drives PC6 high while the
     * pin is held low externally; 'E:Y-' would mean MODER is not output.
     * A refused bench RUN replaces the edit-field hint with its reason code so
     * "REQ flashes and drops back to STOP" is never silent. */
    {
        uint8_t why=ConvController_BenchBlockReason();
        char pin1=ConvController_OutputPinIsOutput()?
                  (ConvController_OutputPinLevel()?'Y':'n'):'-';
        if(why!=CONV_BENCH_OK && s.state!=HEATER_RUN_REQUESTED)
            (void)snprintf(line,sizeof(line),"%s P:%c E:%c%c R:%s",
                state_name(s.state),
                ConvController_LogicPwmRunning()?'Y':'N',
                ConvController_OutputEnabled()?'Y':'N',pin1,
                bench_block_name(why));
        else
            (void)snprintf(line,sizeof(line),"%s P:%c E:%c%c >%s",
                state_name(s.state),
                ConvController_LogicPwmRunning()?'Y':'N',
                ConvController_OutputEnabled()?'Y':'N',pin1,
                field_name(s_edit_field));
    }
    Disp_DrawStr(0u,59u,line);
    Disp_SendBuffer();
}
