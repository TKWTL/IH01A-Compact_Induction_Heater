/* Bounded UART RX line shell. Never blocks the USBPD cooperative loop. */
#include "console_cli.h"
#include "debug_uart.h"
#include "algorithm/pd_input.h"
#include <stdio.h>
#include <string.h>
static char s_line[48];
static uint8_t s_len;
static void cli_dispatch(void)
{
    PD_InputSnapshot pd;
    DebugUART_Stats stats;
    if(!strcmp(s_line,"help"))
        printf("[CLI] help, ping, pd, uart\r\n");
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
