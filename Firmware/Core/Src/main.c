/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "adc.h"
#include "comp.h"
#include "crc.h"
#include "dac.h"
#include "dma.h"
#include "i2c.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "ucpd.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbpd.h"
#include "app_tasks.h"
#include "framework/time_service.h"
#include "conv_controller.h"
#include "usart.h"
#include "ih_reset_diag.h"
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

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
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* IH01A 生成护栏: uvprojx 里没有 FULL 的定义,说明有人在 CubeMX 里重新生成过工程
 * (CubeMX 会按固件包 v1.6.3 的旧记录把 PD 配置打回 MINSNK)。
 * 处理办法: git checkout HEAD -- Firmware/MDK-ARM/Compact_Induction_Heater.uvprojx
 *          (CubeMX 6.17 会保留工程里的自定义 IncludePath/Define/文件列表,
 *           只把 PD 库目录和它自认为"多余"的驱动源文件删掉;库已搬到
 *           Firmware/ST_USBPD_Library_v541 不再受它管理)。 */
#if !defined(USBPDCORE_LIB_PD3_FULL)
#error "CubeMX regenerated the project without the FULL USB-PD stack. Run: git checkout HEAD -- Firmware/MDK-ARM/Compact_Induction_Heater.uvprojx, then Rebuild."
#endif
/* SRAM VTOR relocation lives in main.c, not in the PowerControl module.
 * Flash RESET stays at 0x08000000; .ih_vectors is reserved in scatter.
 * 64 vector entries = 256 bytes, and the destination is 256-aligned. */
static uint32_t s_sram_vectors[64]
    __attribute__((section(".ih_vectors"), aligned(256), used));
/* The scatter file reserves a separate 32-byte UNINIT SRAM region within
 * the 0x20008E00..0x20008EFF gap (away from RAM ISR, stack and VTOR).
 * C startup must never erase this marker on an NVIC_SystemReset(). */
static volatile IH_ResetDiagRecord s_reset_diag
    __attribute__((section(".ih_reset_diag"), aligned(4), used));
extern const uint32_t __Vectors[];
extern const uint32_t __Vectors_End[];

static void RelocateVectorTable(void)
{
    uint32_t i;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    uint32_t count=(uint32_t)(((uintptr_t)__Vectors_End-
                              (uintptr_t)__Vectors)/sizeof(uint32_t));
    for(i=0u;i<64u;++i)
        s_sram_vectors[i]=(i<count) ? __Vectors[i] : 0u;
    __DSB();
    SCB->VTOR=(uint32_t)s_sram_vectors;
    __DSB();
    __ISB();
    if((primask & 1u)==0u) __enable_irq();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* Capture/reset evidence BEFORE any HAL, RTC or RCC initialization.
   * Only a validated RAM marker together with SFTRSTF (and no PWRRSTF)
   * may identify our own emergency software reset. */
  uint32_t boot_reset_flags=RCC->CSR;
  uint32_t boot_reset_stage=0u;
  uint32_t boot_reset_cause=IH_ResetDiag_PriorCause(
      boot_reset_flags,&s_reset_diag,&boot_reset_stage);
  s_reset_diag.magic=0u; /* consume evidence exactly once */
  __DSB();
  __HAL_RCC_CLEAR_RESET_FLAGS();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* Copy Flash vector table to the reserved 256B SRAM slot, then switch VTOR.
   * Must happen before peripherals with enabled DMA interrupts start. */
  RelocateVectorTable();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_COMP1_Init();
  MX_COMP2_Init();
  MX_DAC1_Init();
  MX_RTC_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_TIM15_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM6_Init();
  MX_UCPD2_Init();
  MX_USART1_UART_Init();
  MX_CRC_Init();
  MX_TIM14_Init();
  /* USER CODE BEGIN 2 */
  /* RTC was reset to a relative calendar origin by MX_RTC_Init().
   * Record the boot epoch after all CubeMX peripheral initialization. */
  Time_Init();
  /* Debug UART ring/DMA layer: ours, not CubeMX's - keep it inside USER CODE so a
   * regeneration cannot delete the call (that is what killed the console once). */
  g_uart_boot_stage=2u;
  DebugUART_Init(boot_reset_flags,boot_reset_cause,boot_reset_stage);
  g_uart_boot_stage=3u;
  /* USB-PD FULL stack: the whole Firmware/USBPD tree is hand-maintained (USBPD is
   * unchecked in CubeMX), so this call has to live here as well. */
  MX_USBPD_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  APP_Tasks_Init();
  /* ADC/TIM6 and zero capture start once in TempService_Poll(). TIM1
   * 200kHz logic PWM is then started with PC6 LOW; only the explicit,
   * independently checked 12V bench RUN may enable FD2204 for <=10s. */
  g_uart_boot_stage=4u;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* PD 栈 + coroOS 的主驱动, 链路:
     *   USBPD_DPM_Run() -> USBPD_DPM_UserExecute() -> APP_Tasks_RunOnce()
     *                   -> CoroOS_RunOnce() + SSD1315_Display_Service()
     * CubeMX 取消 USBPD 后不再自动插入这一行, 必须留在 USER CODE, 否则调度器
     * 永远不会被调用(表现: 停在 while(1), UI/ADC/PD 全部无反应)。 */
    USBPD_DPM_Run();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
  while(LL_FLASH_GetLatency() != LL_FLASH_LATENCY_2)
  {
  }

  /* HSI configuration and activation */
  LL_RCC_HSI_Enable();
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_PWR_EnableBkUpAccess();
  /* LSE configuration and activation */
  LL_RCC_LSE_SetDriveCapability(LL_RCC_LSEDRIVE_LOW);
  LL_RCC_LSE_Enable();
  while(LL_RCC_LSE_IsReady() != 1)
  {
  }

  /* Main PLL configuration and activation */
  LL_RCC_PLL_ConfigDomain_TIM1(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_1, 16, LL_RCC_PLLQ_DIV_2);
  LL_RCC_PLL_ConfigDomain_TIM15(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_1, 16, LL_RCC_PLLQ_DIV_2);
  LL_RCC_PLL_EnableDomain_TIM1();
  LL_RCC_PLL_EnableDomain_TIM15();
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_1, 16, LL_RCC_PLLR_DIV_4);
  LL_RCC_PLL_Enable();
  LL_RCC_PLL_EnableDomain_SYS();
  while(LL_RCC_PLL_IsReady() != 1)
  {
  }

  /* Set AHB prescaler*/
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);

  /* Sysclk activation on the main PLL */
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
  {
  }

  /* Set APB1 prescaler*/
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
  /* Update CMSIS variable (which can be updated also through SystemCoreClockUpdate function) */
  LL_SetSystemCoreClock(64000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* Fatal path: PC6 LOW first; do not use UART, HAL delays, locks, heap or
 * scheduler. A four-word retained SRAM marker lets the NEXT boot separate
 * our own NVIC_SystemReset from a different software-reset caller. */
void IH_EmergencyResetReason(uint32_t cause)
{
    __disable_irq();
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOC);
    LL_GPIO_ResetOutputPin(PWM_EN_GPIO_Port,PWM_EN_Pin);
    LL_GPIO_SetPinMode(PWM_EN_GPIO_Port,PWM_EN_Pin,LL_GPIO_MODE_OUTPUT);
    __DSB();
    /* Two-phase record: invalidate old data first, commit MAGIC last. */
    s_reset_diag.magic=0u;
    s_reset_diag.cause=cause;
    s_reset_diag.inverted_cause=~cause;
    s_reset_diag.stage=g_uart_boot_stage;
    __DSB();
    s_reset_diag.magic=IH_RESET_DIAG_MAGIC;
    __DSB();
    for(;;)NVIC_SystemReset();
}
void IH_EmergencyReset(void)
{
    IH_EmergencyResetReason(IH_RESET_CAUSE_EMERGENCY);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Hardware/clock/ADC failures must never leave the FD2204 gate enabled. */
  IH_EmergencyResetReason(IH_RESET_CAUSE_ERROR_HANDLER);
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  (void)file;
  (void)line;
  IH_EmergencyResetReason(IH_RESET_CAUSE_ASSERT);
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
