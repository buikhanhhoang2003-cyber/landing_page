/**
 * @copyright (C) 2017 Melexis N.V.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "MLX90640_I2C_Driver.h"
#include "main.h"
#include <stddef.h>

extern I2C_HandleTypeDef hi2c1;

#define MLX90640_I2C_TIMEOUT_MS 500U
#define MLX90640_I2C_HANDLE (&hi2c1)
#define MLX90640_I2C_CHUNK_WORDS 32U

static uint8_t mlx90640RxBuffer[MLX90640_I2C_CHUNK_WORDS * 2U];

void MLX90640_I2CInit(void)
{
    /* I2C1 is initialized by CubeMX in main(). */
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data)
{
    uint16_t currentAddress = startAddress;
    uint16_t *destination = data;
    uint16_t remainingWords = nMemAddressRead;

    if ((data == NULL) || (nMemAddressRead == 0U))
    {
        return -1;
    }

    while (remainingWords > 0U)
    {
        uint16_t chunkWords = (remainingWords > MLX90640_I2C_CHUNK_WORDS) ? MLX90640_I2C_CHUNK_WORDS : remainingWords;
        uint16_t byteCount = (uint16_t)(chunkWords * 2U);

        if (HAL_I2C_Mem_Read(MLX90640_I2C_HANDLE, (uint16_t)((uint16_t)slaveAddr << 1), currentAddress, I2C_MEMADD_SIZE_16BIT, mlx90640RxBuffer, byteCount, MLX90640_I2C_TIMEOUT_MS) != HAL_OK)
        {
            return -1;
        }

        for (uint16_t i = 0U; i < chunkWords; i++)
        {
            destination[i] = ((uint16_t)mlx90640RxBuffer[2U * i] << 8) | (uint16_t)mlx90640RxBuffer[(2U * i) + 1U];
        }

        currentAddress = (uint16_t)(currentAddress + chunkWords);
        destination += chunkWords;
        remainingWords = (uint16_t)(remainingWords - chunkWords);
    }

    return 0;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
    uint8_t buffer[2];
    uint16_t dataCheck;

    buffer[0] = (uint8_t)(data >> 8);
    buffer[1] = (uint8_t)(data & 0xFFU);

    if (HAL_I2C_Mem_Write(MLX90640_I2C_HANDLE, (uint16_t)((uint16_t)slaveAddr << 1), writeAddress, I2C_MEMADD_SIZE_16BIT, buffer, 2U, MLX90640_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return -1;
    }

    if (MLX90640_I2CRead(slaveAddr, writeAddress, 1U, &dataCheck) != 0)
    {
        return -1;
    }

    if (dataCheck != data)
    {
        return -2;
    }

    return 0;
}

void MLX90640_I2CFreqSet(int freq)
{
    /* Hardware I2C frequency is configured by CubeMX. */
    (void)freq;
}
