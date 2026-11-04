#include "heater_cli_parse.h"
#include <string.h>
static const char *skip_space(const char *p)
{
    while(*p==' ' || *p=='\t')++p;
    return p;
}
static uint8_t token(const char **p,const char *wanted)
{
    const char *s=*p;
    size_t n=strlen(wanted);
    if(strncmp(s,wanted,n)!=0)return 0u;
    if(s[n]!='\0' && s[n]!=' ' && s[n]!='\t')return 0u;
    *p=skip_space(s+n);
    return 1u;
}
static uint8_t end(const char *p)
{
    return (uint8_t)(*skip_space(p)=='\0');
}
static uint8_t decimal(const char *p,uint16_t *value)
{
    uint32_t v=0u;
    if(*p<'0'||*p>'9')return 0u;
    do {
        v=v*10u+(uint32_t)(*p-'0');
        if(v>9999u)return 0u;
        ++p;
    }while(*p>='0'&&*p<='9');
    if(!end(p))return 0u;
    *value=(uint16_t)v;
    return 1u;
}
uint8_t HeaterCli_Parse(const char *line,HeaterCliRequest *out)
{
    const char *p;
    uint16_t value;
    if(!line || !out)return 0u;
    p=skip_space(line);
    if(!token(&p,"heater"))return 0u;
    out->op=HEATER_CLI_BAD_SYNTAX;
    out->value=0u;
    if(token(&p,"get") && end(p)){out->op=HEATER_CLI_GET;return 1u;}
    p=skip_space(line);
    (void)token(&p,"heater");
    if(token(&p,"on") && end(p)){out->op=HEATER_CLI_ON;return 1u;}
    p=skip_space(line);(void)token(&p,"heater");
    if(token(&p,"off") && end(p)){out->op=HEATER_CLI_OFF;return 1u;}
    p=skip_space(line);(void)token(&p,"heater");
    if(token(&p,"auto"))
    {
        if(token(&p,"on") && end(p))out->op=HEATER_CLI_AUTO_ON;
        else if(token(&p,"off") && end(p))out->op=HEATER_CLI_AUTO_OFF;
        return 1u;
    }
    if(token(&p,"freq"))
    {
        /* Explicit migration error: no hidden numeric tuning bypass. */
        out->op=HEATER_CLI_FREQ_DISABLED;
        return 1u;
    }
    if(!token(&p,"phase"))return 1u;
    if(!decimal(p,&value))return 1u;
    if(value>180u)out->op=HEATER_CLI_OUT_OF_RANGE;
    else
    {
        out->op=HEATER_CLI_PHASE;
        out->value=value;
    }
    return 1u;
}
