/*
 * sm7901b.c
 *
 *  Created on: Sep 19, 2026
 *      Author: root
 */


#include "sm7901b.h"

#define SM7901B_DEFAULT_ADDRESS       0x01U

#define SM7901B_FUNC_READ_HOLDING     0x03U

#define SM7901B_NOISE_REG             0x0000U
#define SM7901B_NOISE_REG_COUNT       0x0001U

#define SM7901B_TX_TIMEOUT_MS         50U
#define SM7901B_RX_TIMEOUT_MS         50U

#define SM7901B_REQUEST_SIZE          8U
#define SM7901B_RESPONSE_SIZE         7U

static uint16_t SM7901B_CRC16(
    const uint8_t *data,
    uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc >>= 1U;
                crc ^= 0xA001U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

static void SM7901B_SetTransmit(
    SM7901B_Handle_t *sensor)
{
    HAL_GPIO_WritePin(
        sensor->dirPort,
        sensor->dirPin,
        GPIO_PIN_SET);
}

static void SM7901B_SetReceive(
    SM7901B_Handle_t *sensor)
{
    HAL_GPIO_WritePin(
        sensor->dirPort,
        sensor->dirPin,
        GPIO_PIN_RESET);
}

HAL_StatusTypeDef SM7901B_Init(
    SM7901B_Handle_t *sensor,
    UART_HandleTypeDef *huart,
    GPIO_TypeDef *dirPort,
    uint16_t dirPin)
{
    if ((sensor == NULL) ||
        (huart == NULL) ||
        (dirPort == NULL))
    {
        return HAL_ERROR;
    }

    sensor->huart = huart;
    sensor->dirPort = dirPort;
    sensor->dirPin = dirPin;
    sensor->slaveAddress = SM7901B_DEFAULT_ADDRESS;
    sensor->initialized = 0U;

    /*
     * Default bus state = receive.
     */
    SM7901B_SetReceive(sensor);

    sensor->initialized = 1U;

    return HAL_OK;
}

HAL_StatusTypeDef SM7901B_ReadNoise(
    SM7901B_Handle_t *sensor,
    uint16_t *noise10)
{
    uint8_t request[SM7901B_REQUEST_SIZE];
    uint8_t response[SM7901B_RESPONSE_SIZE];

    uint16_t crc;
    uint16_t receivedCrc;
    uint16_t value;
    uint32_t txCompleteStartTick;

    HAL_StatusTypeDef status;

    if ((sensor == NULL) ||
        (sensor->huart == NULL) ||
        (noise10 == NULL) ||
        (sensor->initialized == 0U))
    {
        return HAL_ERROR;
    }

    /*
     * Modbus RTU request:
     *
     * Slave    : 0x01
     * Function : 0x03
     * Register : 0x0000
     * Count    : 0x0001
     *
     * Expected frame:
     * 01 03 00 00 00 01 84 0A
     */

    request[0] = sensor->slaveAddress;
    request[1] = SM7901B_FUNC_READ_HOLDING;

    request[2] = (uint8_t)(SM7901B_NOISE_REG >> 8U);
    request[3] = (uint8_t)(SM7901B_NOISE_REG & 0xFFU);

    request[4] = (uint8_t)(SM7901B_NOISE_REG_COUNT >> 8U);
    request[5] = (uint8_t)(SM7901B_NOISE_REG_COUNT & 0xFFU);

    crc = SM7901B_CRC16(request, 6U);

    request[6] = (uint8_t)(crc & 0xFFU);
    request[7] = (uint8_t)(crc >> 8U);

    /*
     * TX mode.
     */
    SM7901B_SetTransmit(sensor);

    status = HAL_UART_Transmit(
        sensor->huart,
        request,
        sizeof(request),
        SM7901B_TX_TIMEOUT_MS);

    if (status != HAL_OK)
    {
        SM7901B_SetReceive(sensor);
        return status;
    }

    /*
     * Wait until the final UART bit has physically left the peripheral.
     */
    txCompleteStartTick = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(sensor->huart, UART_FLAG_TC) == RESET)
    {
        if ((uint32_t)(HAL_GetTick() - txCompleteStartTick) >= SM7901B_TX_TIMEOUT_MS)
        {
            SM7901B_SetReceive(sensor);
            return HAL_TIMEOUT;
        }
    }

    /*
     * Switch RS485 transceiver back to RX.
     */
    SM7901B_SetReceive(sensor);

    status = HAL_UART_Receive(
        sensor->huart,
        response,
        sizeof(response),
        SM7901B_RX_TIMEOUT_MS);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Normal response:
     *
     * [0] Slave
     * [1] Function = 0x03
     * [2] Byte count = 2
     * [3] Data high
     * [4] Data low
     * [5] CRC low
     * [6] CRC high
     */

    if (response[0] != sensor->slaveAddress)
    {
        return HAL_ERROR;
    }

    /*
     * Modbus exception response has bit 7 set.
     */
    if ((response[1] & 0x80U) != 0U)
    {
        return HAL_ERROR;
    }

    if (response[1] != SM7901B_FUNC_READ_HOLDING)
    {
        return HAL_ERROR;
    }

    if (response[2] != 2U)
    {
        return HAL_ERROR;
    }

    crc = SM7901B_CRC16(response, 5U);

    receivedCrc =
        ((uint16_t)response[6] << 8U) |
        (uint16_t)response[5];

    if (crc != receivedCrc)
    {
        return HAL_ERROR;
    }

    value =
        ((uint16_t)response[3] << 8U) |
        (uint16_t)response[4];

    /*
     * SM7901B reports noise with 0.1 dB resolution.
     *
     * Example:
     * value = 585 -> 58.5 dB
     */
    *noise10 = value;

    return HAL_OK;
}
