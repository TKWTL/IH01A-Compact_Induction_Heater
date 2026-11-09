/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32g0xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "main.h"
#include "stm32g0xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "conv_protection.h"
#include "conv_controller.h"
#include "usbpd_hw_if.h"  /* USBPD_PORT0_IRQHandler */
#include "usbpd.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* CubeMX owns the fallback HAL DMA handler below and may regenerate it.
 * Keep its symbol weak: the strong SRAM-resident implementation in
 * APP/PowerControl/conv_controller.c takes precedence in the vector table.
 * Do NOT remove this declaration on CubeMX regeneration, or Keil will
 * report L6200E: DMA1_Channel2_3_IRQHandler multiply defined.
 */
__WEAK void DMA1_Channel2_3_IRQHandler(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* STM32G071 TIM1 combined Break/Update/Trigger/Commutation vector.
 * The TIM1 Break input already asynchronously disables MOE in hardware.
 * This ISR additionally disables FD2204 through PC6 on the same event.
 * We deliberately do NOT enable TIM1 UIE, so the 200kHz update rate
 * causes no interrupts; only a Break invokes this handler. */
void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
    if((TIM1->SR&TIM_SR_BIF)!=0u)
        ConvProtection_BreakISR();
}
/* Fatal-path console. The normal console formats into a RAM ring and lets the
 * main loop start the DMA, so it silently stops working when the superloop
 * stops - exactly the case these dumps exist for. Here USART1 is driven
 * directly: DMA request disabled, TDR polled, interrupts not required. */
static void ih_fatal_putc(char c)
{
  uint32_t guard=0u;
  if((USART1->CR1&USART_CR1_UE)==0u)return;
  /* ISR bit 7 = TXE/TXFNF (name differs between G0 sub-families). */
  while(((USART1->ISR&(1UL<<7))==0u)&&(guard++<400000u)) { }
  USART1->TDR=(uint32_t)(uint8_t)c;
}
static void ih_fatal_str(const char *s)
{
  while(*s)ih_fatal_putc(*s++);
}
static void ih_fatal_hex(uint32_t v)
{
  static const char hex[]="0123456789ABCDEF";
  char b[9];
  uint32_t i;
  b[8]=0;
  for(i=0u;i<8u;++i){ b[7u-i]=hex[v&0xFu]; v>>=4; }
  ih_fatal_str(b);
}
static void ih_fatal_frame(uint32_t off,const uint32_t *base)
{
  ih_fatal_str(" off=");ih_fatal_hex(off);
  ih_fatal_str(" pc=");ih_fatal_hex(base[6]);
  ih_fatal_str(" lr=");ih_fatal_hex(base[5]);
  ih_fatal_str(" r0=");ih_fatal_hex(base[0]);
  ih_fatal_str(" r1=");ih_fatal_hex(base[1]);
  ih_fatal_str(" psr=");ih_fatal_hex(base[7]);
  ih_fatal_str("\r\n");
}
/* Called from a fault handler: report the Cortex-M0+ exception stack frame
 * (R0,R1,R2,R3,R12,LR,PC,xPSR) so PC can be resolved in the .map file.
 * The handler prologue may already have pushed, so the frame is printed for
 * three candidate MSP offsets; the real one has PC inside the flash image. */
void IH_FatalReport(const char *tag)
{
  uint32_t sp=0u;
  const uint32_t *base;
  USART1->CR3&=~USART_CR3_DMAT;   /* take the transmitter away from the DMA */
  __asm volatile ("mrs %0, msp" : "=r"(sp));
  ih_fatal_str("\r\n[FAULT] ");ih_fatal_str(tag);
  ih_fatal_str(" sp=");ih_fatal_hex(sp);
  ih_fatal_str("\r\n");
  base=(const uint32_t *)sp;
  ih_fatal_frame(0u,base);
  ih_fatal_frame(8u,base+2);
  ih_fatal_frame(16u,base+4);
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_tim1_ch2;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern UART_HandleTypeDef huart1;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M0+ Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
  /* Fatal faults must immediately cut PC6, not wait on UART telemetry. */
  IH_EmergencyResetReason(IH_RESET_CAUSE_NMI);
  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  /* Unreachable: IH_EmergencyReset() continuously requests system reset. */
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  /* No UART polling or stack decoding here: disable FD2204 and reset. */
  IH_EmergencyResetReason(IH_RESET_CAUSE_HARDFAULT);
  /* USER CODE END HardFault_IRQn 0 */
  /* USER CODE BEGIN W1_HardFault_IRQn 0 */
  /* Unreachable: IH_EmergencyReset() does not return. */
  /* USER CODE END W1_HardFault_IRQn 0 */
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVC_IRQn 0 */

  /* USER CODE END SVC_IRQn 0 */
  /* USER CODE BEGIN SVC_IRQn 1 */

  /* USER CODE END SVC_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
  USBPD_DPM_TimerCounter();  /* PD 1ms 基准: CubeMX 取消 USBPD 后不再自动插入, 放 USER CODE 保命 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32G0xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32g0xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles UCPD1 and UCPD2 interrupts / UCPD1 and UCPD2 wake-up interrupts through EXTI lines 32 and 33.
  */
void UCPD1_2_IRQHandler(void)
{
  /* USER CODE BEGIN UCPD1_2_IRQn 0 */
  /* UCPD2 = PD port 0: 清 UCPD 状态并驱动 TX/RX/CAD 事件。
   * CubeMX 勾选 USBPD 时会生成在 USER CODE 之外, 取消勾选就丢了, 结果是
   * UCPD 中断标志永不清除 -> 中断风暴, 表现成卡在这个 ISR 里。 */
  USBPD_PORT0_IRQHandler();

  /* USER CODE END UCPD1_2_IRQn 0 */
  /* USER CODE BEGIN UCPD1_2_IRQn 1 */

  /* USER CODE END UCPD1_2_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel 1 interrupt.
  */
void DMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel1_IRQn 0 */

  /* USER CODE END DMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi1_tx);
  /* USER CODE BEGIN DMA1_Channel1_IRQn 1 */

  /* USER CODE END DMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel 2 and channel 3 interrupts.
  */
void DMA1_Channel2_3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel2_3_IRQn 0 */

  /* USER CODE END DMA1_Channel2_3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  HAL_DMA_IRQHandler(&hdma_tim1_ch2);
  /* USER CODE BEGIN DMA1_Channel2_3_IRQn 1 */

  /* USER CODE END DMA1_Channel2_3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel 4, channel 5, channel 6, channel 7 and DMAMUX1 interrupts.
  */
void DMA1_Ch4_7_DMAMUX1_OVR_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Ch4_7_DMAMUX1_OVR_IRQn 0 */

  /* USER CODE END DMA1_Ch4_7_DMAMUX1_OVR_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_rx);
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA1_Ch4_7_DMAMUX1_OVR_IRQn 1 */

  /* USER CODE END DMA1_Ch4_7_DMAMUX1_OVR_IRQn 1 */
}

/**
  * @brief This function handles ADC1, COMP1 and COMP2 interrupts (COMP interrupts through EXTI lines 17 and 18).
  */
void ADC1_COMP_IRQHandler(void)
{
  /* USER CODE BEGIN ADC1_COMP_IRQn 0 */
  /* Respond before HAL's callback dispatch. One-shot: mask and clear the
   * asserted AWDx IRQ, gate TIM1 instantly, and defer UI/audio to foreground. */
  ConvProtection_AnalogWatchdogISR();
  /* USER CODE END ADC1_COMP_IRQn 0 */
  HAL_ADC_IRQHandler(&hadc1);
  /* USER CODE BEGIN ADC1_COMP_IRQn 1 */

  /* USER CODE END ADC1_COMP_IRQn 1 */
}

/**
  * @brief This function handles I2C1 event global interrupt / I2C1 wake-up interrupt through EXTI line 23.
  */
void I2C1_IRQHandler(void)
{
  /* USER CODE BEGIN I2C1_IRQn 0 */

  /* USER CODE END I2C1_IRQn 0 */
  if (hi2c1.Instance->ISR & (I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR))
  {
    HAL_I2C_ER_IRQHandler(&hi2c1);
  }
  else
  {
    HAL_I2C_EV_IRQHandler(&hi2c1);
  }
  /* USER CODE BEGIN I2C1_IRQn 1 */

  /* USER CODE END I2C1_IRQn 1 */
}

/**
  * @brief This function handles SPI1 global interrupt.
  */
void SPI1_IRQHandler(void)
{
  /* USER CODE BEGIN SPI1_IRQn 0 */

  /* USER CODE END SPI1_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi1);
  /* USER CODE BEGIN SPI1_IRQn 1 */

  /* USER CODE END SPI1_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt / USART1 wake-up interrupt through EXTI line 25.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
