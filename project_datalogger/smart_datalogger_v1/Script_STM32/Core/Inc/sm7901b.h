#ifndef INC_SM7901B_H_
#define INC_SM7901B_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct
{
    UART_HandleTypeDef *huart;

    GPIO_TypeDef *dirPort;
    uint16_t dirPin;

    uint8_t slaveAddress;
    uint8_t initialized;

} SM7901B_Handle_t;

HAL_StatusTypeDef SM7901B_Init(
    SM7901B_Handle_t *sensor,
    UART_HandleTypeDef *huart,
    GPIO_TypeDef *dirPort,
    uint16_t dirPin);

HAL_StatusTypeDef SM7901B_ReadNoise(
    SM7901B_Handle_t *sensor,
    uint16_t *noise10);

#endif /* INC_SM7901B_H_ */
