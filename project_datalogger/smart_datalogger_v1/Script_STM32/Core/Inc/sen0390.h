/*
 * sen0390.h
 *
 *  Created on: Sep 19, 2026
 *      Author: root
 */

#ifndef INC_SEN0390_H_
#define INC_SEN0390_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint8_t address;
    uint8_t initialized;
    uint32_t initTick;
} SEN0390_Handle_t;

HAL_StatusTypeDef SEN0390_Init(
    SEN0390_Handle_t *sensor,
    I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef SEN0390_ReadLux(
    SEN0390_Handle_t *sensor,
    uint32_t *lux);

#endif /* INC_SEN0390_H_ */

