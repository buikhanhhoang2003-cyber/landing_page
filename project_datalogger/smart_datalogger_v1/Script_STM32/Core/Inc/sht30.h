#ifndef INC_SHT30_H_
#define INC_SHT30_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint8_t address;
    uint8_t initialized;
} SHT30_Handle_t;

HAL_StatusTypeDef SHT30_Init(
    SHT30_Handle_t *sensor,
    I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef SHT30_Read(
    SHT30_Handle_t *sensor,
    int16_t *temperature10,
    uint16_t *humidity10);

#endif /* INC_SHT30_H_ */
