#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "PowerControl/heater_ui_format.h"
int main(void)
{
    char text[40];
    uint8_t selected=HEATER_UI_EDIT_TARGET;
    /* Confirm cycles TARGET->OFF->PAN->RUN->MODE; L/R edit selection. */
    selected=HeaterUI_NextField(selected);
    assert(selected==HEATER_UI_EDIT_OFF);
    selected=HeaterUI_NextField(selected);
    assert(selected==HEATER_UI_EDIT_PAN);
    selected=HeaterUI_NextField(selected);
    assert(selected==HEATER_UI_EDIT_RUN);
    selected=HeaterUI_NextField(selected);
    assert(selected==HEATER_UI_EDIT_MODE);
    selected=HeaterUI_NextField(selected);
    assert(selected==HEATER_UI_EDIT_TARGET);
    HeaterUI_FormatFooter(text,sizeof(text),"STOP","MODE",1u,"TIME",
                          1u,0u,'n');
    assert(strcmp(text,"STOP >MODE R:TIME")==0);
    HeaterUI_FormatFooter(text,sizeof(text),"STOP","OFF",0u,"",
                          1u,0u,'n');
    assert(strcmp(text,"STOP P:Y E:Nn >OFF")==0);
    HeaterUI_FormatCountdown(text,sizeof(text),0u,0u,0u,
                             1u,0u,0u,0u);
    assert(strcmp(text,"OFF 0m PAN:ON")==0);
    HeaterUI_FormatCountdown(text,sizeof(text),1u,5u,299u,
                             1u,0u,0u,0u);
    assert(strcmp(text,"OFF 04:59 PAN:ON")==0);
    HeaterUI_FormatCountdown(text,sizeof(text),0u,5u,0u,
                             0u,1u,1u,444u);
    assert(strcmp(text,"OFF 5m IC2:444")==0);
    HeaterUI_FormatCountdown(text,sizeof(text),1u,240u,14399u,
                             0u,1u,1u,65535u);
    assert(strlen(text)<=21u);
    assert(strstr(text,"OFF 239:59 IC2:65535")!=0);
    HeaterUI_FormatCountdown(text,sizeof(text),0u,5u,0u,
                             0u,1u,0u,0u);
    assert(strcmp(text,"OFF 5m IC2:--")==0);
    puts("heater UI navigation/countdown: PASS");
    return 0;
}
