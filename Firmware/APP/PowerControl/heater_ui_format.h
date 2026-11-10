#ifndef IH01A_HEATER_UI_FORMAT_H
#define IH01A_HEATER_UI_FORMAT_H
/* Main page status helpers are deliberately free of HAL/MiaoUI dependencies,
 * so navigation and display regressions run in the host CI suite. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    HEATER_UI_EDIT_MODE=0u,
    HEATER_UI_EDIT_TARGET,
    HEATER_UI_EDIT_OFF,
    HEATER_UI_EDIT_PAN,
    HEATER_UI_EDIT_RUN,
    HEATER_UI_EDIT_COUNT
};

static inline uint8_t HeaterUI_NextField(uint8_t field)
{
    return (uint8_t)((field+1u)%HEATER_UI_EDIT_COUNT);
}

/* ALWAYS show the active edit field, including after failed bench RUN.
 * On rejection replace P/E telemetry with reason, never with a hidden
 * selector. This is the only bottom-row navigation feedback. */
static inline void HeaterUI_FormatFooter(char *out,size_t cap,
    const char *state,const char *field,
    uint8_t blocked,const char *reason,
    uint8_t pwm_on,uint8_t en_on,char pin)
{
    if(!out || !cap)return;
    if(blocked)
        (void)snprintf(out,cap,"%s >%s R:%s",state,field,reason);
    else
        (void)snprintf(out,cap,"%s P:%c E:%c%c >%s",
                       state,pwm_on?'Y':'N',en_on?'Y':'N',pin,field);
}

/* Draw the OFF countdown BEFORE optional capture status, not a discarded
 * snprintf immediately overwritten by IC2/PAN. 128px OLED has ~21 chars. */
static inline void HeaterUI_FormatCountdown(char *out,size_t cap,
    uint8_t running,uint16_t minutes,uint32_t remaining_s,
    uint8_t pan_enabled,uint8_t show_ic2,uint8_t ic2_valid,uint16_t ic2)
{
    int n;
    if(!out || !cap)return;
    if(running && minutes)
        n=snprintf(out,cap,"OFF %02lu:%02lu",
                   (unsigned long)(remaining_s/60u),
                   (unsigned long)(remaining_s%60u));
    else
        n=snprintf(out,cap,"OFF %um",(unsigned)minutes);
    if(n<0 || (size_t)n>=cap)return;
    if(pan_enabled)
        (void)snprintf(out+n,cap-(size_t)n," PAN:ON");
    else if(show_ic2)
    {
        if(ic2_valid)
            (void)snprintf(out+n,cap-(size_t)n,
                           " IC2:%u",(unsigned)ic2);
        else
            (void)snprintf(out+n,cap-(size_t)n," IC2:--");
    }
}
#endif
