/* Main menu: a view/controller of the persistent Heater service.
 * No control loop, reservation countdown, or hardware PWM in this file.
 * ENTER cycles editable fields; L/R change the selected field; EXIT leaves.
 */
#include "PowerControl/heater_ui.h"
#include "PowerControl/heater_control.h"
#include "PowerControl/conv_controller.h"
#include "PowerControl/conv_phase.h"
#include "PowerControl/conv_zcd.h"
#include "tim.h"
#include "display/dispDriver.h"
#include "framework/pm_api.h"
#include "main.h"
#include <stdio.h>

enum {EDIT_MODE=0u,EDIT_TARGET,EDIT_OFF,EDIT_ON,EDIT_RUN,EDIT_COUNT};
/* Power bench default: L/R edits phase immediately on Main entry. */
static uint8_t s_edit_field=EDIT_TARGET;

static const char *state_name(HeaterState s)
{
    switch(s)
    {
    case HEATER_WAIT_START:return "WAIT";
    case HEATER_WAIT_CONFIRM:return "CFM";
    case HEATER_RUN_REQUESTED:return "REQ";
    case HEATER_FAULT:return "FAULT";
    default:return "STOP";
    }
}
static const char *field_name(uint8_t field)
{
    static const char *const names[EDIT_COUNT]={
        "MODE","TARGET","OFF","ON","RUN"
    };
    return names[field<EDIT_COUNT?field:0u];
}
static void edit_value(uint8_t field,UI_ACTION action,uint32_t now)
{
    HeaterSnapshot s;
    uint8_t up=(action==UI_ACTION_DOWN)?1u:0u;
    Heater_GetSnapshot(&s);
    switch(field)
    {
    case EDIT_MODE:
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
    case EDIT_ON:
        {
            int32_t minutes=(int32_t)s.on_minutes+(up?5:-5);
            if(minutes<0)minutes=0;
            if(minutes>1440)minutes=1440;
            Heater_ScheduleMinutes((uint16_t)minutes,now);
        }
        break;
    case EDIT_RUN:
        if(up)
        {
            /* Manual start OR confirmation after appointment expiry.
             * The power stage is still hardware-locked OFF at this stage. */
            Heater_StartConfirmed(now);
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
    ConvZcd_Poll(now);
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
                (unsigned)ConvController_BenchSecondsLeft(now));
        else
            (void)snprintf(line,sizeof(line),"HEATER %c %s%luV",
                s.mode==HEATER_MODE_TEMP?'T':'P',kind,
                (unsigned long)((s.bus_mv+500u)/1000u));
        Disp_DrawStr(0u,11u,line);
    }
    if(s.pot_valid && s.sys_valid)
        (void)snprintf(line,sizeof(line),"POT %dC SYS %dC",
            (int)(s.pot_deci_c/10),(int)(s.sys_deci_c/10));
    else
        (void)snprintf(line,sizeof(line),"POT/SYS: --");
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
    /* Show live countdown when armed, not only the configuration values.
     * Timers remain active even if this menu item is closed. */
    if(s.state==HEATER_WAIT_START)
        (void)snprintf(line,sizeof(line),"ON %02lu:%02lu OFF %um",
            (unsigned long)(s.on_remaining_s/60u),
            (unsigned long)(s.on_remaining_s%60u),
            (unsigned)s.off_minutes);
    else if(s.state==HEATER_RUN_REQUESTED && s.off_minutes)
        (void)snprintf(line,sizeof(line),"ON -- OFF %02lu:%02lu",
            (unsigned long)(s.off_remaining_s/60u),
            (unsigned long)(s.off_remaining_s%60u));
    else
        (void)snprintf(line,sizeof(line),"ON %um OFF %um",
            (unsigned)s.on_minutes,(unsigned)s.off_minutes);
    /* During manual phase testing reserve one row for latest TIM1_CH2 edge.
     * Lag is relative to the model voltage +->- at ARR/4+D/2. */
    if(s.mode==HEATER_MODE_POWER)
    {
        ConvZcdSnapshot z;
        ConvZcd_GetSnapshot(&z);
        if(z.valid)
        {
            int16_t mag=z.lag_deci_deg<0?(int16_t)-z.lag_deci_deg:
                                              z.lag_deci_deg;
            (void)snprintf(line,sizeof(line),"IC2:%u L:%c%d.%d",
                           (unsigned)z.capture_raw,
                           z.lag_deci_deg<0?'-':'+',
                           (int)(mag/10),(int)(mag%10));
        }
        else (void)snprintf(line,sizeof(line),"IC2:-- L:--");
    }
    Disp_DrawStr(0u,47u,line);
    /* At 128px and ~6px/glyph, keep line <=21 ASCII characters.
     * P=TIM1 PWM output, E=FD2204 PC6 gate enable. */
    (void)snprintf(line,sizeof(line),"%s P:%c E:%c >%s",
        state_name(s.state),
        ConvController_LogicPwmRunning()?'Y':'N',
        ConvController_OutputEnabled()?'Y':'N',
        field_name(s_edit_field));
    Disp_DrawStr(0u,59u,line);
    Disp_SendBuffer();
}
