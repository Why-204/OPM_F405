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
#include "stm32f4xx_hal.h"

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
#define USB_CD_Pin GPIO_PIN_2
#define USB_CD_GPIO_Port GPIOA
#define ADC_PWND1_Pin GPIO_PIN_4
#define ADC_PWND1_GPIO_Port GPIOA
#define ADC_PWND2_Pin GPIO_PIN_5
#define ADC_PWND2_GPIO_Port GPIOA
#define ADC_PWDN3_Pin GPIO_PIN_6
#define ADC_PWDN3_GPIO_Port GPIOA
#define ADC_PWND4_Pin GPIO_PIN_7
#define ADC_PWND4_GPIO_Port GPIOA
#define ADC_PWND5_Pin GPIO_PIN_4
#define ADC_PWND5_GPIO_Port GPIOC
#define ADC_PWND6_Pin GPIO_PIN_5
#define ADC_PWND6_GPIO_Port GPIOC
#define ADC_PWND7_Pin GPIO_PIN_0
#define ADC_PWND7_GPIO_Port GPIOB
#define ADC_PWND8_Pin GPIO_PIN_1
#define ADC_PWND8_GPIO_Port GPIOB
#define ADC_SYNC_Pin GPIO_PIN_15
#define ADC_SYNC_GPIO_Port GPIOB
#define ADC_DRDY_Pin GPIO_PIN_6
#define ADC_DRDY_GPIO_Port GPIOC
#define ADC_DRDY_EXTI_IRQn EXTI9_5_IRQn
#define ETH_TCPCS_Pin GPIO_PIN_15
#define ETH_TCPCS_GPIO_Port GPIOA
#define ETH_RSTI_Pin GPIO_PIN_3
#define ETH_RSTI_GPIO_Port GPIOB
#define ETH_CFG_Pin GPIO_PIN_4
#define ETH_CFG_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
