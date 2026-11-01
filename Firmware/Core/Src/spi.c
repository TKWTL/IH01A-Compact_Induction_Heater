/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.c
  * @brief   This file provides code for the configuration
  *          of the SPI instances.
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
#include "spi.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

/* SPI1 init function */
void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

void HAL_SPI_MspInit(SPI_HandleTypeDef* spiHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(spiHandle->Instance==SPI1)
  {
  /* USER CODE BEGIN SPI1_MspInit 0 */

  /* USER CODE END SPI1_MspInit 0 */
    /* SPI1 clock enable */
    __HAL_RCC_SPI1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SPI1 GPIO Configuration
    PA15     ------> SPI1_NSS
    PB3     ------> SPI1_SCK
    PB5     ------> SPI1_MOSI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SPI1 DMA Init */
    /* SPI1_TX Init */
    hdma_spi1_tx.Instance = DMA1_Channel1;
    hdma_spi1_tx.Init.Request = DMA_REQUEST_SPI1_TX;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle,hdmatx,hdma_spi1_tx);

    /* SPI1 interrupt Init */
    HAL_NVIC_SetPriority(SPI1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
  /* USER CODE BEGIN SPI1_MspInit 1 */

  /* USER CODE END SPI1_MspInit 1 */
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef* spiHandle)
{

  if(spiHandle->Instance==SPI1)
  {
  /* USER CODE BEGIN SPI1_MspDeInit 0 */

  /* USER CODE END SPI1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI1_CLK_DISABLE();

    /**SPI1 GPIO Configuration
    PA15     ------> SPI1_NSS
    PB3     ------> SPI1_SCK
    PB5     ------> SPI1_MOSI
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_15);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_3|GPIO_PIN_5);

    /* SPI1 DMA DeInit */
    HAL_DMA_DeInit(spiHandle->hdmatx);

    /* SPI1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
  /* USER CODE BEGIN SPI1_MspDeInit 1 */

  /* USER CODE END SPI1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/* SPI1 DMA TX transport. Only this CubeMX user region invokes
 * HAL_SPI_* transfers. Caller owns the DMA source until DONE/ERROR.
 * No blocking HAL_Delay, HAL_SPI_Transmit, or HAL_SPI_Abort.
 */
static volatile SPI1_DmaState s_spi1_tx_state=SPI1_DMA_IDLE;
uint8_t SPI1_DmaTxStart(const uint8_t *bytes,uint16_t count)
{
    if(bytes==0 || count==0u || s_spi1_tx_state!=SPI1_DMA_IDLE ||
       HAL_SPI_GetState(&hspi1)!=HAL_SPI_STATE_READY)
        return 0u;
    s_spi1_tx_state=SPI1_DMA_BUSY;
    if(HAL_SPI_Transmit_DMA(&hspi1,(uint8_t *)(void *)bytes,count)!=HAL_OK)
    {
        s_spi1_tx_state=SPI1_DMA_ERROR;
        return 0u;
    }
    return 1u;
}
SPI1_DmaState SPI1_DmaTxState(void)
{
    return s_spi1_tx_state;
}
uint8_t SPI1_DmaTxRelease(void)
{
    if(s_spi1_tx_state!=SPI1_DMA_DONE)return 0u;
    s_spi1_tx_state=SPI1_DMA_IDLE;
    return 1u;
}
uint8_t SPI1_DmaTxAbortAsync(void)
{
    if(s_spi1_tx_state!=SPI1_DMA_BUSY &&
       s_spi1_tx_state!=SPI1_DMA_ERROR)return 0u;
    s_spi1_tx_state=SPI1_DMA_ABORTING;
    if(HAL_SPI_Abort_IT(&hspi1)!=HAL_OK)
    {
        s_spi1_tx_state=SPI1_DMA_ERROR;
        return 0u;
    }
    return 1u;
}
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance==SPI1 && s_spi1_tx_state==SPI1_DMA_BUSY)
        s_spi1_tx_state=SPI1_DMA_DONE;
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance==SPI1 && s_spi1_tx_state==SPI1_DMA_BUSY)
        s_spi1_tx_state=SPI1_DMA_ERROR;
}
void HAL_SPI_AbortCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance==SPI1 && s_spi1_tx_state==SPI1_DMA_ABORTING)
        s_spi1_tx_state=SPI1_DMA_IDLE;
}
/* USER CODE END 1 */

