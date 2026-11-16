/*
 * sen0390.c
 *
 *  Created on: Sep 19, 2026
 *      Author: root
 */


#include "sen0390.h"

#define SEN0390_ADDRESS            0x4AU

#define SEN0390_REG_DATA           0x00U
#define SEN0390_REG_CONFIG         0x04U

#define SEN0390_CONFIG_VALUE       0x00U

#define SEN0390_I2C_TIMEOUT_MS     50U
#define SEN0390_WARMUP_MS          1000U

static HAL_StatusTypeDef SEN0390_WriteReg(
    SEN0390_Handle_t *sensor,
    uint8_t reg,
    uint8_t value)
{
    return HAL_I2C_Mem_Write(
        sensor->hi2c,
        (uint16_t)(sensor->address << 1U),
        reg,
        I2C_MEMADD_SIZE_8BIT,
        &value,
        1U,
        SEN0390_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef SEN0390_ReadRegs(
    SEN0390_Handle_t *sensor,
    uint8_t reg,
    uint8_t *data,
    uint16_t length)
{
    return HAL_I2C_Mem_Read(
        sensor->hi2c,
        (uint16_t)(sensor->address << 1U),
        reg,
        I2C_MEMADD_SIZE_8BIT,
        data,
        length,
        SEN0390_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef SEN0390_Init(
    SEN0390_Handle_t *sensor,
    I2C_HandleTypeDef *hi2c)
{
    if ((sensor == NULL) || (hi2c == NULL))
    {
        return HAL_ERROR;
    }

    sensor->hi2c = hi2c;
    sensor->address = SEN0390_ADDRESS;
    sensor->initialized = 0U;

    if (HAL_I2C_IsDeviceReady(
            hi2c,
            (uint16_t)(SEN0390_ADDRESS << 1U),
            3U,
            SEN0390_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (SEN0390_WriteReg(
            sensor,
            SEN0390_REG_CONFIG,
            SEN0390_CONFIG_VALUE) != HAL_OK)
    {
        return HAL_ERROR;
    }

    sensor->initTick = HAL_GetTick();
    sensor->initialized = 1U;

    return HAL_OK;
}

HAL_StatusTypeDef SEN0390_ReadLux(
    SEN0390_Handle_t *sensor,
    uint32_t *lux)
{
    uint8_t data[4];
    uint32_t raw;
    uint64_t scaled;

    if ((sensor == NULL) ||
        (sensor->hi2c == NULL) ||
        (lux == NULL) ||
        (sensor->initialized == 0U))
    {
        return HAL_ERROR;
    }

    /*
     * Give the sensor enough time after configuration
     * before accepting the first reading.
     */
    if ((HAL_GetTick() - sensor->initTick) < SEN0390_WARMUP_MS)
    {
        return HAL_BUSY;
    }

    if (SEN0390_ReadRegs(
            sensor,
            SEN0390_REG_DATA,
            data,
            sizeof(data)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /*
     * SEN0390 data is little-endian.
     */
    raw =
        ((uint32_t)data[0]) |
        ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) |
        ((uint32_t)data[3] << 24U);

    /*
     * DFRobot conversion:
     *
     * lux = raw * 1.4 / 1000
     *
     * Integer version:
     * lux = raw * 14 / 10000
     */
    scaled = ((uint64_t)raw * 14ULL) + 5000ULL;
    scaled /= 10000ULL;

    if (scaled > 0xFFFFFFFFULL)
    {
        return HAL_ERROR;
    }

    *lux = (uint32_t)scaled;

    return HAL_OK;
}
