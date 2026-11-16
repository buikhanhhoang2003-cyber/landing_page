#include "bme280.h"

#define BME280_I2C_TIMEOUT_MS 50U

BME280_Compensation_TypeDef cal_param;

static I2C_HandleTypeDef *bme280Hi2c = NULL;
static uint8_t bme280Address = BME280_ADDR_V;
static int32_t t_fine = 0;

static HAL_StatusTypeDef BME280_HAL_Read(uint8_t reg, uint8_t *data, uint16_t length)
{
    if ((bme280Hi2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(bme280Hi2c, (uint16_t)(bme280Address << 1U), reg, I2C_MEMADD_SIZE_8BIT, data, length, BME280_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef BME280_HAL_Write(uint8_t reg, const uint8_t *data, uint16_t length)
{
    if ((bme280Hi2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(bme280Hi2c, (uint16_t)(bme280Address << 1U), reg, I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, length, BME280_I2C_TIMEOUT_MS);
}

void BME280_BindI2C(I2C_HandleTypeDef *hi2c)
{
    bme280Hi2c = hi2c;
}

void BME280_WriteReg(uint8_t reg, uint8_t value)
{
    (void)BME280_HAL_Write(reg, &value, 1U);
}

uint8_t BME280_ReadReg(uint8_t reg)
{
    uint8_t value = 0U;
    (void)BME280_HAL_Read(reg, &value, 1U);
    return value;
}

BME280_RESULT BME280_Check(void)
{
    uint8_t chipId = 0U;
    static const uint8_t addresses[2] = {BME280_ADDR_G, BME280_ADDR_V};

    if (bme280Hi2c == NULL)
    {
        return BME280_ERROR;
    }

    for (uint8_t i = 0U; i < 2U; i++)
    {
        bme280Address = addresses[i];

        if (HAL_I2C_IsDeviceReady(bme280Hi2c, (uint16_t)(bme280Address << 1U), 3U, BME280_I2C_TIMEOUT_MS) != HAL_OK)
        {
            continue;
        }

        if (BME280_HAL_Read(BME280_REG_ID, &chipId, 1U) == HAL_OK && chipId == 0x60U)
        {
            return BME280_SUCCESS;
        }
    }

    return BME280_ERROR;
}

void BME280_Reset(void)
{
    BME280_WriteReg(BME280_REG_RESET, BME280_SOFT_RESET_KEY);
}

uint8_t BME280_GetVersion(void)
{
    return BME280_ReadReg(BME280_REG_ID);
}

uint8_t BME280_GetStatus(void)
{
    return BME280_ReadReg(BME280_REG_STATUS) & BME280_STATUS_MSK;
}

uint8_t BME280_GetMode(void)
{
    return BME280_ReadReg(BME280_REG_CTRL_MEAS) & BME280_MODE_MSK;
}

void BME280_SetMode(uint8_t mode)
{
    uint8_t reg = BME280_ReadReg(BME280_REG_CTRL_MEAS) & (uint8_t)~BME280_MODE_MSK;
    reg |= mode & BME280_MODE_MSK;
    BME280_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetFilter(uint8_t filter)
{
    uint8_t reg = BME280_ReadReg(BME280_REG_CONFIG) & (uint8_t)~BME280_FILTER_MSK;
    reg |= filter & BME280_FILTER_MSK;
    BME280_WriteReg(BME280_REG_CONFIG, reg);
}

void BME280_SetStandby(uint8_t tsb)
{
    uint8_t reg = BME280_ReadReg(BME280_REG_CONFIG) & (uint8_t)~BME280_STBY_MSK;
    reg |= tsb & BME280_STBY_MSK;
    BME280_WriteReg(BME280_REG_CONFIG, reg);
}

void BME280_SetOSRST(uint8_t osrs)
{
    uint8_t reg = BME280_ReadReg(BME280_REG_CTRL_MEAS) & (uint8_t)~BME280_OSRS_T_MSK;
    reg |= osrs & BME280_OSRS_T_MSK;
    BME280_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetOSRSP(uint8_t osrs)
{
    uint8_t reg = BME280_ReadReg(BME280_REG_CTRL_MEAS) & (uint8_t)~BME280_OSRS_P_MSK;
    reg |= osrs & BME280_OSRS_P_MSK;
    BME280_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetOSRSH(uint8_t osrs)
{
    uint8_t reg = BME280_ReadReg(BME280_REG_CTRL_HUM) & (uint8_t)~BME280_OSRS_H_MSK;
    reg |= osrs & BME280_OSRS_H_MSK;
    BME280_WriteReg(BME280_REG_CTRL_HUM, reg);

    reg = BME280_ReadReg(BME280_REG_CTRL_MEAS);
    BME280_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

BME280_RESULT BME280_Read_Calibration(void)
{
    uint8_t tp[24];
    uint8_t h[7];

    if (BME280_HAL_Read(BME280_REG_CALIB00, tp, sizeof(tp)) != HAL_OK)
    {
        return BME280_ERROR;
    }

    cal_param.dig_T1 = (uint16_t)(((uint16_t)tp[1] << 8) | tp[0]);
    cal_param.dig_T2 = (int16_t)(((uint16_t)tp[3] << 8) | tp[2]);
    cal_param.dig_T3 = (int16_t)(((uint16_t)tp[5] << 8) | tp[4]);
    cal_param.dig_P1 = (uint16_t)(((uint16_t)tp[7] << 8) | tp[6]);
    cal_param.dig_P2 = (int16_t)(((uint16_t)tp[9] << 8) | tp[8]);
    cal_param.dig_P3 = (int16_t)(((uint16_t)tp[11] << 8) | tp[10]);
    cal_param.dig_P4 = (int16_t)(((uint16_t)tp[13] << 8) | tp[12]);
    cal_param.dig_P5 = (int16_t)(((uint16_t)tp[15] << 8) | tp[14]);
    cal_param.dig_P6 = (int16_t)(((uint16_t)tp[17] << 8) | tp[16]);
    cal_param.dig_P7 = (int16_t)(((uint16_t)tp[19] << 8) | tp[18]);
    cal_param.dig_P8 = (int16_t)(((uint16_t)tp[21] << 8) | tp[20]);
    cal_param.dig_P9 = (int16_t)(((uint16_t)tp[23] << 8) | tp[22]);

    if (BME280_HAL_Read(BME280_REG_CALIB25, &cal_param.dig_H1, 1U) != HAL_OK)
    {
        return BME280_ERROR;
    }

    if (BME280_HAL_Read(BME280_REG_CALIB26, h, sizeof(h)) != HAL_OK)
    {
        return BME280_ERROR;
    }

    cal_param.dig_H2 = (int16_t)(((uint16_t)h[1] << 8) | h[0]);
    cal_param.dig_H3 = h[2];
    cal_param.dig_H4 = (int16_t)(((int16_t)(int8_t)h[3] << 4) | (h[4] & 0x0FU));
    cal_param.dig_H5 = (int16_t)(((int16_t)(int8_t)h[5] << 4) | (h[4] >> 4));
    cal_param.dig_H6 = (int8_t)h[6];

    return ((cal_param.dig_T1 != 0U) && (cal_param.dig_P1 != 0U)) ? BME280_SUCCESS : BME280_ERROR;
}

BME280_RESULT BME280_Read_UP(int32_t *UP)
{
    uint8_t buf[3];

    if (UP == NULL)
    {
        return BME280_ERROR;
    }

    *UP = 0x80000L;

    if (BME280_HAL_Read(BME280_REG_PRESS_MSB, buf, sizeof(buf)) != HAL_OK)
    {
        return BME280_ERROR;
    }

    *UP = (int32_t)(((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | ((uint32_t)buf[2] >> 4));
    return BME280_SUCCESS;
}

BME280_RESULT BME280_Read_UT(int32_t *UT)
{
    uint8_t buf[3];

    if (UT == NULL)
    {
        return BME280_ERROR;
    }

    *UT = 0x80000L;

    if (BME280_HAL_Read(BME280_REG_TEMP_MSB, buf, sizeof(buf)) != HAL_OK)
    {
        return BME280_ERROR;
    }

    *UT = (int32_t)(((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | ((uint32_t)buf[2] >> 4));
    return BME280_SUCCESS;
}

BME280_RESULT BME280_Read_UH(int32_t *UH)
{
    uint8_t buf[2];

    if (UH == NULL)
    {
        return BME280_ERROR;
    }

    *UH = 0x8000L;

    if (BME280_HAL_Read(BME280_REG_HUM_MSB, buf, sizeof(buf)) != HAL_OK)
    {
        return BME280_ERROR;
    }

    *UH = (int32_t)(((uint32_t)buf[0] << 8) | buf[1]);
    return BME280_SUCCESS;
}

BME280_RESULT BME280_Read_UTPH(int32_t *UT, int32_t *UP, int32_t *UH)
{
    uint8_t buf[8];

    if ((UT == NULL) || (UP == NULL) || (UH == NULL))
    {
        return BME280_ERROR;
    }

    *UT = 0x80000L;
    *UP = 0x80000L;
    *UH = 0x8000L;

    if (BME280_HAL_Read(BME280_REG_PRESS_MSB, buf, sizeof(buf)) != HAL_OK)
    {
        return BME280_ERROR;
    }

    *UP = (int32_t)(((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | ((uint32_t)buf[2] >> 4));
    *UT = (int32_t)(((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | ((uint32_t)buf[5] >> 4));
    *UH = (int32_t)(((uint32_t)buf[6] << 8) | buf[7]);

    return BME280_SUCCESS;
}

int32_t BME280_CalcT(int32_t UT)
{
    t_fine = ((((UT >> 3) - ((int32_t)cal_param.dig_T1 << 1))) * (int32_t)cal_param.dig_T2) >> 11;
    t_fine += (((((UT >> 4) - (int32_t)cal_param.dig_T1) * ((UT >> 4) - (int32_t)cal_param.dig_T1)) >> 12) * (int32_t)cal_param.dig_T3) >> 14;

    return ((t_fine * 5) + 128) >> 8;
}

uint32_t BME280_CalcP(int32_t UP)
{
#if (BME280_USE_INT64)
    int64_t v1;
    int64_t v2;
    int64_t p;

    v1 = (int64_t)t_fine - 128000LL;
    v2 = v1 * v1 * (int64_t)cal_param.dig_P6;
    v2 += (v1 * (int64_t)cal_param.dig_P5) << 17;
    v2 += (int64_t)cal_param.dig_P4 << 35;
    v1 = ((v1 * v1 * (int64_t)cal_param.dig_P3) >> 8) + ((v1 * (int64_t)cal_param.dig_P2) << 12);
    v1 = (((((int64_t)1 << 47) + v1) * (int64_t)cal_param.dig_P1) >> 33);

    if (v1 == 0)
    {
        return 0U;
    }

    p = 1048576LL - (int64_t)UP;
    p = (((p << 31) - v2) * 3125LL) / v1;
    v1 = ((int64_t)cal_param.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    v2 = ((int64_t)cal_param.dig_P8 * p) >> 19;
    p = ((p + v1 + v2) >> 8) + ((int64_t)cal_param.dig_P7 << 4);

    return (p > 0) ? (uint32_t)p : 0U;
#else
    return 0U;
#endif
}

uint32_t BME280_CalcH(int32_t UH)
{
    int32_t value;

    value = t_fine - 76800;
    value = (((((UH << 14) - ((int32_t)cal_param.dig_H4 << 20) - ((int32_t)cal_param.dig_H5 * value)) + 16384) >> 15) *
            (((((((value * (int32_t)cal_param.dig_H6) >> 10) * (((value * (int32_t)cal_param.dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
            (int32_t)cal_param.dig_H2 + 8192) >> 14));
    value -= ((((value >> 15) * (value >> 15)) >> 7) * (int32_t)cal_param.dig_H1) >> 4;

    if (value < 0)
    {
        value = 0;
    }
    else if (value > 419430400)
    {
        value = 419430400;
    }

    return (uint32_t)(value >> 12);
}
