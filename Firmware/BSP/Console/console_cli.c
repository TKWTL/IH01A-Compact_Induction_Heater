/* Bounded UART RX line shell. Never blocks the USBPD cooperative loop. */
#include "console_cli.h"
#include "usart.h"
#include "algorithm/pd_input.h"
#include "algorithm/pd_epr_policy.h"
#include <stdio.h>
#include <string.h>
static char s_line[48];
static uint8_t s_len;
static void cli_dispatch(void)
{
    PD_InputSnapshot pd;
    PD_InputContract active;
    DebugUART_Stats stats;
    if(!strcmp(s_line,"help"))
        printf("[CLI] help, ping, pd, pd on, pd off, pd max N, pd epr, pd auto, uart\r\n");
    else if(!strcmp(s_line,"ping"))
        printf("[CLI] pong\r\n");
    else if(!strcmp(s_line,"pd"))
    {
        PD_Input_GetSnapshot(&pd);
        printf("[CLI] PD attached=%u contract=%u attempted=%u mode=%u\r\n",
               pd.attached,pd.explicit_contract,pd.attempted,pd.mode);
        printf("[CLI] bus=%lu mV select=%lu mV current=%lu mA PDO=%u\r\n",
               (unsigned long)pd.bus_mv,(unsigned long)pd.selected_mv,
               (unsigned long)pd.requested_ma,pd.position);
        printf("[CLI] EPR enabled=%u mode=%u auto=%u\r\n",
               (unsigned)IH01A_EPR_28V5A_HARDWARE_APPROVED,
               (unsigned)PD_Input_EPR_Active(),
               (unsigned)PD_Input_GetEPRAuto());
        printf("[CLI] request=%u maxFixed=%uV (off=5V)\r\n",
               PD_Input_GetRequestEnabled(),PD_Input_GetMaxFixedVoltage());
        if(PD_Input_GetActiveContract(&active))
            printf("[CLI] ACTIVE %lu mV %lu mA PDO=%u EPR=%u\r\n",
                   (unsigned long)active.voltage_mv,
                   (unsigned long)active.current_ma,active.position,active.epr);
        else printf("[CLI] ACTIVE not confirmed (0W budget)\r\n");
    }
    else if(!strcmp(s_line,"pd on") || !strcmp(s_line,"pd off"))
    {
        uint8_t en=(uint8_t)!strcmp(s_line,"pd on");
        PD_Input_SetRequestEnabled(en);
        printf("[CLI] Fixed request %s; negotiating\r\n",en?"ON":"OFF (5V)");
    }
    else if(!strncmp(s_line,"pd max ",7))
    {
        const char *p=s_line+7;
        uint32_t v=0u;
        if(!*p)v=255u;
        while(*p>='0'&&*p<='9'&&v<=31u)
        {
            v=v*10u+(uint32_t)(*p-'0');
            ++p;
        }
        if(*p || v>31u || !PD_Input_SetMaxFixedVoltage((uint8_t)v))
            printf("[CLI] Usage: pd max 5..31\r\n");
        else printf("[CLI] Max Fixed %luV; negotiating\r\n",(unsigned long)v);
    }
    else if(!strcmp(s_line,"pd epr") || !strcmp(s_line,"pd epr1"))
    {
        /* 'pd epr'  : absolute object position 8..13 (the verified default)
         * 'pd epr1' : 1-based index inside EPR_Source_Capabilities (fails on
         *             this Source with a Hard Reset; kept for comparison) */
        uint8_t absolute = strcmp(s_line,"pd epr1")==0 ? 0u : 1u;
        PD_Input_SetEPRObjectPosition(absolute);
        if(PD_Input_ManualEPR())
            printf("[CLI] EPR enter queued (opos=%s)\r\n",
                   absolute ? "absolute" : "local");
        else
            printf("[CLI] EPR unavailable: need SPR contract, EPR source and approval\r\n");
    }
    else if(!strcmp(s_line,"pd auto"))
    {
        uint8_t on = (uint8_t)!PD_Input_GetEPRAuto();
        PD_Input_SetEPRAuto(on);
        printf("[CLI] EPR auto-enter %s (takes effect on the next attach)\r\n",
               on ? "ON" : "OFF");
    }
    else if(!strcmp(s_line,"uart"))
    {
        DebugUART_GetStats(&stats);
        printf("[CLI] UART tx_drop=%lu rx_drop=%lu rx_err=%lu idle=%lu\r\n",
               (unsigned long)stats.tx_dropped,(unsigned long)stats.rx_dropped,
               (unsigned long)stats.rx_errors,(unsigned long)stats.rx_idle_events);
        printf("[CLI] tx_start=%lu tx_done=%lu tx_fail=%lu boot=%lu probe=%lu\r\n",
               (unsigned long)stats.tx_dma_starts,
               (unsigned long)stats.tx_dma_completions,
               (unsigned long)stats.tx_dma_failures,
               (unsigned long)g_uart_boot_stage,
               (unsigned long)g_uart_probe_result);
    }
    else if(s_len!=0u)
        printf("[CLI] unknown: %s\r\n",s_line);
}
void ConsoleCLI_Poll(void)
{
    uint8_t ch,n;
    /* Limit work per PD scheduler pass even with a continuous RX stream. */
    for(n=0u;n<16u && DebugUART_ReadByte(&ch);++n)
    {
        if(ch=='\r'||ch=='\n')
        {
            if(s_len)
            {
                s_line[s_len]=0;
                cli_dispatch();
                s_len=0u;
            }
        }
        else if(ch==8u||ch==127u)
        {
            if(s_len)--s_len;
        }
        else if(ch>=32u&&ch<=126u)
        {
            if(s_len<(sizeof(s_line)-1u))s_line[s_len++]=(char)ch;
            else s_len=0u;
        }
    }
}
