/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <stdio.h>
/* USART1 DMA transport lives here, not in a separate BSP source.
 * HAL calls/ISR callbacks and 512-byte RX/TX software rings are CubeMX-safe. */
/* USER CODE END 0 */

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    LL_RCC_SetUSARTClockSource(LL_RCC_USART1_CLKSOURCE_PCLK1);

    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PB6     ------> USART1_TX
    PB7     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    GPIO_InitStruct.Alternate = GPIO_AF0_USART1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA1_Channel4;
    hdma_usart1_rx.Init.Request = DMA_REQUEST_USART1_RX;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    /* USART1_TX Init */
    hdma_usart1_tx.Instance = DMA1_Channel5;
    hdma_usart1_tx.Init.Request = DMA_REQUEST_USART1_TX;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart1_tx);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PB6     ------> USART1_TX
    PB7     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6|GPIO_PIN_7);

    /* USART1 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
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
static volatile uint32_t s_dma_starts,s_dma_failures,s_dma_completions;
volatile uint32_t g_uart_boot_stage;
volatile uint32_t g_uart_probe_result=0xFFFFFFFFu;

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
    out->tx_dma_starts=s_dma_starts;
    out->tx_dma_failures=s_dma_failures;
    out->tx_dma_completions=s_dma_completions;
}
void DebugUART_Init(uint32_t reset_flags)
{
    s_tx_head=0u;s_tx_tail=0u;s_tx_active=0u;s_tx_busy=0u;
    s_rx_head=0u;s_rx_tail=0u;s_dma_rx_last=0u;
    s_tx_dropped=0u;s_rx_dropped=0u;s_rx_errors=0u;s_rx_idle=0u;
    s_dma_starts=0u;s_dma_failures=0u;s_dma_completions=0u;
    s_ready=1u;
    s_rx_running=(HAL_UARTEx_ReceiveToIdle_DMA(&huart1,s_rx_dma,
                                                 UART_DMA_RX_SIZE)==HAL_OK);
    if(!s_rx_running)++s_rx_errors;
    /* One short direct TX probe BEFORE USBPD_DPM_Run: proves clock/pin/USART
     * even if the cooperative PD loop never reaches DebugUART_Service().
     * Six 8N1 bytes take ~0.52 ms at 115200 baud, only at boot.
     * This is NEVER invoked inside an active PD negotiation callback. */
    {
        static const uint8_t marker[]="U1OK\r\n";
        g_uart_probe_result=(uint32_t)HAL_UART_Transmit(&huart1,
                       (uint8_t *)(void *)marker,(uint16_t)(sizeof(marker)-1u),10u);
    }
    printf("\r\n[BOOT %lu] IH01A USART1 DMA+IDLE 115200 8N1 flags=0x%08lX\r\n",
           (unsigned long)HAL_GetTick(),(unsigned long)reset_flags);
    printf("[UART] TX ring=512 RX ring=512 DMA RX staging=128; type help\r\n");
    DebugUART_Service(); /* kick DMA before the nonreturning PD main loop */
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
        ++s_dma_failures;
        s_tx_active=0u;s_tx_busy=0u;
    }
    else ++s_dma_starts;
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance!=USART1)return;
    s_tx_head=(uint16_t)((s_tx_head+s_tx_active)&RING_MASK);
    s_tx_active=0u;
    s_tx_busy=0u;
    ++s_dma_completions;
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

/* USER CODE END 1 */

