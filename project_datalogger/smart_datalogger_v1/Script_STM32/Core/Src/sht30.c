#include "sht30.h"

#define SHT30_ADDRESS_1          0x44U
#define SHT30_ADDRESS_2          0x45U

#define SHT30_CMD_MEASURE_MSB    0x24U
#define SHT30_CMD_MEASURE_LSB    0x00U

#define SHT30_I2C_TIMEOUT_MS     20U
#define SHT30_MEASURE_DELAY_MS   16U

static uint8_t SHT30_CalculateCRC(
    const uint8_t *data,
    uint8_t length)
{
    uint8_t crc = 0xFFU;

    for (uint8_t i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((crc << 1U) ^ 0x31U);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

static HAL_StatusTypeDef SHT30_ProbeAddress(
    I2C_HandleTypeDef *hi2c,
    uint8_t address)
{
    return HAL_I2C_IsDeviceReady(
        hi2c,
        (uint16_t)(address << 1U),
        3U,
        SHT30_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef SHT30_Init(
    SHT30_Handle_t *sensor,
    I2C_HandleTypeDef *hi2c)
{
    if ((sensor == NULL) || (hi2c == NULL))
    {
        return HAL_ERROR;
    }

    sensor->hi2c = hi2c;
    sensor->initialized = 0U;

    if (SHT30_ProbeAddress(hi2c, SHT30_ADDRESS_1) == HAL_OK)
    {
        sensor->address = SHT30_ADDRESS_1;
    }
    else if (SHT30_ProbeAddress(hi2c, SHT30_ADDRESS_2) == HAL_OK)
    {
        sensor->address = SHT30_ADDRESS_2;
    }
    else
    {
        return HAL_ERROR;
    }

    sensor->initialized = 1U;

    return HAL_OK;
}

HAL_StatusTypeDef SHT30_Read(
    SHT30_Handle_t *sensor,
    int16_t *temperature10,
    uint16_t *humidity10)
{
    uint8_t command[2];
    uint8_t data[6];

    uint16_t rawTemperature;
    uint16_t rawHumidity;

    int32_t temperatureValue10;
    uint32_t humidityValue10;

    if ((sensor == NULL) ||
        (sensor->hi2c == NULL) ||
        (temperature10 == NULL) ||
        (humidity10 == NULL) ||
        (sensor->initialized == 0U))
    {
        return HAL_ERROR;
    }

    command[0] = SHT30_CMD_MEASURE_MSB;
    command[1] = SHT30_CMD_MEASURE_LSB;

    if (HAL_I2C_Master_Transmit(
            sensor->hi2c,
            (uint16_t)(sensor->address << 1U),
            command,
            sizeof(command),
            SHT30_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    HAL_Delay(SHT30_MEASURE_DELAY_MS);

    if (HAL_I2C_Master_Receive(
            sensor->hi2c,
            (uint16_t)(sensor->address << 1U),
            data,
            sizeof(data),
            SHT30_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (SHT30_CalculateCRC(&data[0], 2U) != data[2])
    {
        return HAL_ERROR;
    }

    if (SHT30_CalculateCRC(&data[3], 2U) != data[5])
    {
        return HAL_ERROR;
    }

    rawTemperature =
        (uint16_t)(((uint16_t)data[0] << 8U) |
                   (uint16_t)data[1]);

    rawHumidity =
        (uint16_t)(((uint16_t)data[3] << 8U) |
                   (uint16_t)data[4]);

    /*
     * SHT30:
     *
     * Temperature:
     *   T = -45 + 175 * raw / 65535
     *
     * Output is T x 10.
     */
    temperatureValue10 =
        -450 +
        (int32_t)(
            (((uint32_t)1750U * rawTemperature) + 32767U)
            / 65535U);

    /*
     * Relative humidity:
     *   RH = 100 * raw / 65535
     *
     * Output is RH x 10.
     */
    humidityValue10 =
        (((uint32_t)1000U * rawHumidity) + 32767U)
        / 65535U;

    if (humidityValue10 > 1000U)
    {
        humidityValue10 = 1000U;
    }

    *temperature10 = (int16_t)temperatureValue10;
    *humidity10 = (uint16_t)humidityValue10;

    return HAL_OK;
}
