/*
 * thermal_stream.c
 *
 *  Created on: Sep 21, 2026
 *      Author: hoangb
 */


#include "thermal_stream.h"

static uint8_t thermalTxBuffer[THERMAL_STREAM_PACKET_BYTES];

static void ThermalStream_WriteU16LE(uint8_t *buffer, uint16_t offset, uint16_t value)
{
    buffer[offset] = (uint8_t)(value & 0x00FFU);
    buffer[offset + 1U] = (uint8_t)((value >> 8U) & 0x00FFU);
}

static void ThermalStream_WriteI16LE(uint8_t *buffer, uint16_t offset, int16_t value)
{
    ThermalStream_WriteU16LE(buffer, offset, (uint16_t)value);
}

int16_t ThermalStream_EncodeTemperature10(float temperature)
{
    float scaled = temperature * 10.0f;

    if (scaled > 32767.0f)
    {
        return 32767;
    }

    if (scaled < -32768.0f)
    {
        return -32768;
    }

    if (scaled >= 0.0f)
    {
        scaled += 0.5f;
    }
    else
    {
        scaled -= 0.5f;
    }

    return (int16_t)scaled;
}

uint16_t ThermalStream_Crc16CcittFalse(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    if (data == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

HAL_StatusTypeDef ThermalStream_SendFrame(UART_HandleTypeDef *huart, const float *pixels, uint16_t sequence, float ambientTemp, float centerTemp, float minTemp, float maxTemp)
{
    uint16_t pixelIndex;
    uint16_t offset;
    uint16_t crc;

    if ((huart == NULL) || (pixels == NULL))
    {
        return HAL_ERROR;
    }

    thermalTxBuffer[0] = (uint8_t)'T';
    thermalTxBuffer[1] = (uint8_t)'H';
    thermalTxBuffer[2] = THERMAL_STREAM_PROTOCOL_VERSION;
    thermalTxBuffer[3] = 0x01U;

    ThermalStream_WriteU16LE(thermalTxBuffer, 4U, sequence);

    thermalTxBuffer[6] = THERMAL_STREAM_WIDTH;
    thermalTxBuffer[7] = THERMAL_STREAM_HEIGHT;

    ThermalStream_WriteU16LE(thermalTxBuffer, 8U, THERMAL_STREAM_PAYLOAD_BYTES);
    ThermalStream_WriteI16LE(thermalTxBuffer, 10U, ThermalStream_EncodeTemperature10(ambientTemp));
    ThermalStream_WriteI16LE(thermalTxBuffer, 12U, ThermalStream_EncodeTemperature10(centerTemp));
    ThermalStream_WriteI16LE(thermalTxBuffer, 14U, ThermalStream_EncodeTemperature10(minTemp));
    ThermalStream_WriteI16LE(thermalTxBuffer, 16U, ThermalStream_EncodeTemperature10(maxTemp));

    offset = THERMAL_STREAM_HEADER_BYTES;

    for (pixelIndex = 0U; pixelIndex < THERMAL_STREAM_PIXEL_COUNT; pixelIndex++)
    {
        ThermalStream_WriteI16LE(thermalTxBuffer, offset, ThermalStream_EncodeTemperature10(pixels[pixelIndex]));
        offset = (uint16_t)(offset + 2U);
    }

    crc = ThermalStream_Crc16CcittFalse(thermalTxBuffer, (uint16_t)(THERMAL_STREAM_PACKET_BYTES - THERMAL_STREAM_CRC_BYTES));
    ThermalStream_WriteU16LE(thermalTxBuffer, (uint16_t)(THERMAL_STREAM_PACKET_BYTES - THERMAL_STREAM_CRC_BYTES), crc);

    return HAL_UART_Transmit(huart, thermalTxBuffer, THERMAL_STREAM_PACKET_BYTES, THERMAL_STREAM_TX_TIMEOUT_MS);
}
