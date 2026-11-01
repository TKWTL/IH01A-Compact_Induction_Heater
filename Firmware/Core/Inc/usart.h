/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

/* USER CODE BEGIN Private defines */
#define DEBUG_UART_TX_RING_SIZE 512u
#define DEBUG_UART_RX_RING_SIZE 512u
/* Observable from Keil Watch if the UART pin remains idle high. */
extern volatile uint32_t g_uart_boot_stage;
extern volatile uint32_t g_uart_probe_result;
/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */
typedef struct {
    uint32_t tx_dropped, rx_dropped, rx_errors, rx_idle_events;
    uint32_t tx_dma_starts, tx_dma_failures, tx_dma_completions;
} DebugUART_Stats;
void DebugUART_Init(uint32_t reset_flags);
void DebugUART_Service(void);
void DebugUART_WriteByte(uint8_t byte);
uint32_t DebugUART_Write(const uint8_t *data, uint32_t size);
uint8_t DebugUART_ReadByte(uint8_t *byte);
void DebugUART_GetStats(DebugUART_Stats *out);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

