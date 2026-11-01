/* USART1 PB6 TX / PB7 RX, 115200 8N1. Nonblocking software TX/RX rings.
 * TX DMA1 Ch5 normal; RX DMA1 Ch4 circular with IDLE / HT / TC.
 * Adapted from PB02B DMA TX ring, deliberately NEVER busy-waits when full.
 * All HAL UART completion callbacks live here, not in generated Core code.
 */
#include "debug_uart.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define RING_MASK (DEBUG_UART_TX_RING_SIZE - 1u)
#define UART_DMA_RX_SIZE 128u
#if (DEBUG_UART_TX_RING_SIZE != 512u) || (DEBUG_UART_RX_RING_SIZE != 512u)
#error "IH01A UART rings must each occupy 512 bytes"
#endif
static uint8_t s_tx[DEBUG_UART_TX_RING_SIZE];
static uint8_t s_rx[DEBUG_UART_RX_RING_SIZE];
static uint8_t s_rx_dma[UART_DMA_RX_SIZE];
static volatile uint16_t s_tx_head, s_tx_tail, s_tx_active;
static volatile uint16_t s_rx_head, s_rx_tail;
static uint16_t s_dma_rx_last;
static volatile uint8_t s_tx_busy, s_rx_running, s_ready;
static volatile uint32_t s_tx_dropped, s_rx_dropped, s_rx_errors, s_rx_idle;

void DebugUART_WriteByte(uint8_t byte)
{
    uint16_t next;
    if(!s_ready)return;
    next=(uint16_t)((s_tx_tail+1u)&RING_MASK);
    if(next==s_tx_head) { ++s_tx_dropped; return; }
    s_tx[s_tx_tail]=byte;
    s_tx_tail=next;
}
uint32_t DebugUART_Write(const uint8_t *data,uint32_t size)
{
    uint32_t before=s_tx_dropped,i;
    if(!data)return 0u;
    for(i=0u;i<size;++i)DebugUART_WriteByte(data[i]);
    return size-(s_tx_dropped-before);
}
static void uart_rx_commit(uint8_t byte)
{
    uint16_t next=(uint16_t)((s_rx_tail+1u)&RING_MASK);
    if(next==s_rx_head) { ++s_rx_dropped; return; }
    s_rx[s_rx_tail]=byte;
    s_rx_tail=next;
}
uint8_t DebugUART_ReadByte(uint8_t *byte)
{
    if(!byte||s_rx_head==s_rx_tail)return 0u;
    *byte=s_rx[s_rx_head];
    s_rx_head=(uint16_t)((s_rx_head+1u)&RING_MASK);
    return 1u;
}
void DebugUART_GetStats(DebugUART_Stats *out)
{
    if(!out)return;
    out->tx_dropped=s_tx_dropped;
    out->rx_dropped=s_rx_dropped;
    out->rx_errors=s_rx_errors;
    out->rx_idle_events=s_rx_idle;
}
void DebugUART_Init(uint32_t reset_flags)
{
    s_tx_head=0u;s_tx_tail=0u;s_tx_active=0u;s_tx_busy=0u;
    s_rx_head=0u;s_rx_tail=0u;s_dma_rx_last=0u;
    s_tx_dropped=0u;s_rx_dropped=0u;s_rx_errors=0u;s_rx_idle=0u;
    s_ready=1u;
    s_rx_running=(HAL_UARTEx_ReceiveToIdle_DMA(&huart1,s_rx_dma,
                                                 UART_DMA_RX_SIZE)==HAL_OK);
    if(!s_rx_running)++s_rx_errors;
    printf("\r\n[BOOT %lu] IH01A USART1 DMA+IDLE 115200 8N1 flags=0x%08lX\r\n",
           (unsigned long)HAL_GetTick(),(unsigned long)reset_flags);
    printf("[UART] TX ring=512 RX ring=512 DMA RX staging=128; type help\r\n");
}
void DebugUART_Service(void)
{
    uint16_t head,tail,len;
    /* HAL aborts DMA reception after certain UART errors. Recover outside ISR. */
    if(s_ready&&!s_rx_running&&huart1.RxState==HAL_UART_STATE_READY)
    {
        s_dma_rx_last=0u;
        if(HAL_UARTEx_ReceiveToIdle_DMA(&huart1,s_rx_dma,UART_DMA_RX_SIZE)==HAL_OK)
            s_rx_running=1u;
    }
    if(!s_ready||s_tx_busy)return;
    head=s_tx_head;tail=s_tx_tail;
    if(head==tail)return;
    len=(tail>head)?(uint16_t)(tail-head):
                    (uint16_t)(DEBUG_UART_TX_RING_SIZE-head);
    s_tx_busy=1u;
    s_tx_active=len;
    if(HAL_UART_Transmit_DMA(&huart1,&s_tx[head],len)!=HAL_OK)
    {
        s_tx_active=0u;s_tx_busy=0u;
    }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance!=USART1)return;
    s_tx_head=(uint16_t)((s_tx_head+s_tx_active)&RING_MASK);
    s_tx_active=0u;
    s_tx_busy=0u;
}
/* In HAL circular ReceiveToIdle, Size is the position in the DMA buffer,
 * NOT the count since last callback. Keep 128 as a distinct end sentinel,
 * otherwise a TC then IDLE at position 128 duplicates an entire frame.
 * USART1 IRQ priority must equal the shared DMA1 Ch4-7 IRQ priority, to
 * prevent an IDLE callback being interrupted by a simultaneous HT/TC.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart,uint16_t size)
{
    uint16_t i,last;
    if(huart->Instance!=USART1||!s_rx_running||size>UART_DMA_RX_SIZE)return;
    if(HAL_UARTEx_GetRxEventType(huart)==HAL_UART_RXEVENT_IDLE)++s_rx_idle;
    last=s_dma_rx_last;
    if(size>last)
    {
        for(i=last;i<size;++i)uart_rx_commit(s_rx_dma[i]);
    }
    else if(size<last)
    {
        for(i=last;i<UART_DMA_RX_SIZE;++i)uart_rx_commit(s_rx_dma[i]);
        for(i=0u;i<size;++i)uart_rx_commit(s_rx_dma[i]);
    }
    s_dma_rx_last=size;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance!=USART1)return;
    ++s_rx_errors;
    s_rx_running=0u;
}
