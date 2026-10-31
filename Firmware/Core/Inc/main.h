/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

#include "stm32g0xx_ll_rcc.h"
#include "stm32g0xx_ll_bus.h"
#include "stm32g0xx_ll_system.h"
#include "stm32g0xx_ll_exti.h"
#include "stm32g0xx_ll_cortex.h"
#include "stm32g0xx_ll_utils.h"
#include "stm32g0xx_ll_pwr.h"
#include "stm32g0xx_ll_dma.h"
#include "stm32g0xx_ll_ucpd.h"
#include "stm32g0xx_ll_gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EEP_WP_Pin LL_GPIO_PIN_13
#define EEP_WP_GPIO_Port GPIOC
#define BEEP_Pin LL_GPIO_PIN_0
#define BEEP_GPIO_Port GPIOF
#define ISR_TIME_Pin LL_GPIO_PIN_1
#define ISR_TIME_GPIO_Port GPIOF
#define ISR_LOAD_Pin LL_GPIO_PIN_0
#define ISR_LOAD_GPIO_Port GPIOA
#define NTC_POT_Pin LL_GPIO_PIN_1
#define NTC_POT_GPIO_Port GPIOA
#define NTC_SYS_Pin LL_GPIO_PIN_2
#define NTC_SYS_GPIO_Port GPIOA
#define CT_OCP_Pin LL_GPIO_PIN_3
#define CT_OCP_GPIO_Port GPIOA
#define VBIAS_Pin LL_GPIO_PIN_4
#define VBIAS_GPIO_Port GPIOA
#define IBUS_Pin LL_GPIO_PIN_5
#define IBUS_GPIO_Port GPIOA
#define VBUS_Pin LL_GPIO_PIN_2
#define VBUS_GPIO_Port GPIOB
#define CT_RMS_Pin LL_GPIO_PIN_10
#define CT_RMS_GPIO_Port GPIOB
#define CT_ZERO_Pin LL_GPIO_PIN_9
#define CT_ZERO_GPIO_Port GPIOA
#define PWM_EN_Pin LL_GPIO_PIN_6
#define PWM_EN_GPIO_Port GPIOC
#define ENTR_Pin LL_GPIO_PIN_7
#define ENTR_GPIO_Port GPIOC
#define R_Pin LL_GPIO_PIN_11
#define R_GPIO_Port GPIOA
#define L_Pin LL_GPIO_PIN_12
#define L_GPIO_Port GPIOA
#define EXIT_Pin LL_GPIO_PIN_3
#define EXIT_GPIO_Port GPIOD
#define D_C_Pin LL_GPIO_PIN_4
#define D_C_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
