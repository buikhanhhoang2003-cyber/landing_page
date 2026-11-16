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
#include "stm32f1xx_hal.h"

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
#define ADC1_IN7_Pin GPIO_PIN_7
#define ADC1_IN7_GPIO_Port GPIOA
#define Y1_OUTPUT_Pin GPIO_PIN_4
#define Y1_OUTPUT_GPIO_Port GPIOC
#define Y2_OUTPUT_Pin GPIO_PIN_5
#define Y2_OUTPUT_GPIO_Port GPIOC
#define Y3_OUTPUT_Pin GPIO_PIN_0
#define Y3_OUTPUT_GPIO_Port GPIOB
#define Y4_OUTPUT_Pin GPIO_PIN_1
#define Y4_OUTPUT_GPIO_Port GPIOB
#define SEN_BME_SCL_Pin GPIO_PIN_10
#define SEN_BME_SCL_GPIO_Port GPIOB
#define SEN_BME_SDA_Pin GPIO_PIN_11
#define SEN_BME_SDA_GPIO_Port GPIOB
#define DC_Pin GPIO_PIN_6
#define DC_GPIO_Port GPIOC
#define RST_Pin GPIO_PIN_7
#define RST_GPIO_Port GPIOC
#define BUSY_Pin GPIO_PIN_8
#define BUSY_GPIO_Port GPIOC
#define X1_INPUT_Pin GPIO_PIN_9
#define X1_INPUT_GPIO_Port GPIOC
#define X2_INPUT_Pin GPIO_PIN_8
#define X2_INPUT_GPIO_Port GPIOA
#define X3_INPUT_Pin GPIO_PIN_9
#define X3_INPUT_GPIO_Port GPIOA
#define X4_INPUT_Pin GPIO_PIN_10
#define X4_INPUT_GPIO_Port GPIOA
#define RS485_NOISE_DIR_Pin GPIO_PIN_5
#define RS485_NOISE_DIR_GPIO_Port GPIOB
#define SHT_MLX_SCL_Pin GPIO_PIN_8
#define SHT_MLX_SCL_GPIO_Port GPIOB
#define SHT_MLX_SDA_Pin GPIO_PIN_9
#define SHT_MLX_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
