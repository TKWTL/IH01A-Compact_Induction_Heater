/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app/usbpd.c
  * @author  MCD Application Team
  * @brief   This file contains the device define.
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
#include "usbpd.h"

/* USER CODE BEGIN 0 */
#include "usart.h"
#include <stdio.h>
/* 0=not reached, 1=HW, 2=Core, 3=User, 4=OS ready;
 * >=0xE0: init failure. Inspect in Keil even if DMA TX cannot progress. */
volatile uint32_t g_usbpd_full_stage;
/* 来自 usbpd_dpm_core.c: InitCore 内部失败子步骤与状态码 */
extern volatile uint32_t g_pd_init_substep;
extern volatile uint32_t g_pd_init_status;
/* microLIB 堆边界(由 startup_stm32g071xx.s 导出), 用于打印当前堆大小 */
extern uint32_t __heap_base;
extern uint32_t __heap_limit;
/* USER CODE END 0 */

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

/* Private variables ---------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/* USER CODE BEGIN 2 */
/* USER CODE END 2 */
/* USBPD init function */
void MX_USBPD_Init(void)
{

  /* Global Init of USBPD HW */
  USBPD_HW_IF_GlobalHwInit();
  g_usbpd_full_stage=1u;

  /* Initialize the Device Policy Manager */
  printf("[PD_INIT] LIB_ID=0x%08X check=%lu reqmem=%lu heap=%lu\r\n",
         (unsigned)_LIB_ID, (unsigned long)USBPD_PE_CheckLIB(_LIB_ID),
         (unsigned long)USBPD_PE_GetMemoryConsumption(),
         (unsigned long)((uint32_t)&__heap_limit - (uint32_t)&__heap_base));
  DebugUART_Service();
  if (USBPD_OK != USBPD_DPM_InitCore())
  {
    g_usbpd_full_stage=0xE1u;
    printf("[PD_INIT] FULL core / library check FAILED\r\n");
    printf("[PD_INIT] substep=%lu status=0x%08X\r\n",
           (unsigned long)g_pd_init_substep, (unsigned)g_pd_init_status);
    DebugUART_Service();
    while(1);
  }
  g_usbpd_full_stage=2u;

  /* Initialise the DPM application */
  if (USBPD_OK != USBPD_DPM_UserInit())
  {
    g_usbpd_full_stage=0xE2u;
    printf("[PD_INIT] user init FAILED\r\n");
    DebugUART_Service();
    while(1);
  }
  g_usbpd_full_stage=3u;

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */

  if (USBPD_OK != USBPD_DPM_InitOS())
  {
    g_usbpd_full_stage=0xE3u;
    printf("[PD_INIT] OS init FAILED\r\n");
    DebugUART_Service();
    while(1);
  }
  g_usbpd_full_stage=4u;
  printf("[PD_INIT] FULL v5.4.1 initialized\r\n");
  DebugUART_Service();

  /* USER CODE BEGIN EnableIRQ */
  /* Enable IRQ which has been disabled by FreeRTOS services */
  __enable_irq();
  /* USER CODE END EnableIRQ */

}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @}
  */

/**
  * @}
  */
