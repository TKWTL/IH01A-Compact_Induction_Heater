#include <assert.h>
#include <stdio.h>
#include "heater_cli_parse.h"
static void yes(const char *line,HeaterCliOp op,unsigned n)
{
    HeaterCliRequest r;
    assert(HeaterCli_Parse(line,&r));
    assert(r.op==op);
    assert(r.value==n);
}
int main(void)
{
    HeaterCliRequest r;
    assert(!HeaterCli_Parse("pd max 12",&r));
    assert(!HeaterCli_Parse("heaterx on",&r));
    assert(!HeaterCli_Parse("",&r));
    assert(!HeaterCli_Parse("heater on",0));
    yes("heater get",HEATER_CLI_GET,0);
    yes("  heater  get  ",HEATER_CLI_GET,0);
    yes("heater on",HEATER_CLI_ON,0);
    yes("heater off",HEATER_CLI_OFF,0);
    yes("heater phase 0",HEATER_CLI_PHASE,0);
    yes("heater phase 180",HEATER_CLI_PHASE,180);
    yes("heater phase 91",HEATER_CLI_PHASE,91);
    yes("heater auto on",HEATER_CLI_AUTO_ON,0);
    yes("heater auto off",HEATER_CLI_AUTO_OFF,0);
    yes("heater freq 150",HEATER_CLI_FREQ,150);
    yes("heater freq 185",HEATER_CLI_FREQ,185);
    yes("heater freq 400",HEATER_CLI_FREQ,400);
    yes("heater freq 149",HEATER_CLI_OUT_OF_RANGE,0);
    yes("heater freq 401",HEATER_CLI_OUT_OF_RANGE,0);
    yes("heater phase 181",HEATER_CLI_OUT_OF_RANGE,0);
    yes("heater phase 9999",HEATER_CLI_OUT_OF_RANGE,0);
    yes("heater auto",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater auto 1",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater auto on extra",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater freq",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater freq -1",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater freq xyz",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater phase +90",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater phase -1",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater phase 90xyz",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater get extra",HEATER_CLI_BAD_SYNTAX,0);
    yes("heater freq 99999",HEATER_CLI_BAD_SYNTAX,0);
    puts("heater cli parse: PASS");
    return 0;
}
