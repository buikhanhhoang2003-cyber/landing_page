#ifndef __BME280_H
#define __BME280_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define BME280_USE_INT64                1U

#define BME280_ADDR_G                   0x76U
#define BME280_ADDR_V                   0x77U

#define BME280_REG_CALIB00              0x88U
#define BME280_REG_CALIB25              0xA1U
#define BME280_REG_ID                   0xD0U
#define BME280_REG_RESET                0xE0U
#define BME280_REG_CALIB26              0xE1U
#define BME280_REG_CTRL_HUM             0xF2U
#define BME280_REG_STATUS               0xF3U
#define BME280_REG_CTRL_MEAS            0xF4U
#define BME280_REG_CONFIG               0xF5U
#define BME280_REG_PRESS_MSB            0xF7U
#define BME280_REG_TEMP_MSB             0xFAU
#define BME280_REG_HUM_MSB              0xFDU

#define BME280_SOFT_RESET_KEY           0xB6U

#define BME280_STATUS_MSK               0x09U
#define BME280_STATUS_MEASURING         0x08U
#define BME280_STATUS_IM_UPDATE         0x01U

#define BME280_OSRS_H_MSK               0x07U
#define BME280_OSRS_H_SKIP              0x00U
#define BME280_OSRS_H_x1                0x01U
#define BME280_OSRS_H_x2                0x02U
#define BME280_OSRS_H_x4                0x03U
#define BME280_OSRS_H_x8                0x04U
#define BME280_OSRS_H_x16               0x05U

#define BME280_OSRS_T_MSK               0xE0U
#define BME280_OSRS_T_SKIP              0x00U
#define BME280_OSRS_T_x1                0x20U
#define BME280_OSRS_T_x2                0x40U
#define BME280_OSRS_T_x4                0x60U
#define BME280_OSRS_T_x8                0x80U
#define BME280_OSRS_T_x16               0xA0U

#define BME280_OSRS_P_MSK               0x1CU
#define BME280_OSRS_P_SKIP              0x00U
#define BME280_OSRS_P_x1                0x04U
#define BME280_OSRS_P_x2                0x08U
#define BME280_OSRS_P_x4                0x0CU
#define BME280_OSRS_P_x8                0x10U
#define BME280_OSRS_P_x16               0x14U

#define BME280_MODE_MSK                 0x03U
#define BME280_MODE_SLEEP               0x00U
#define BME280_MODE_FORCED              0x01U
#define BME280_MODE_NORMAL              0x03U

#define BME280_STBY_MSK                 0xE0U
#define BME280_STBY_0p5ms               0x00U
#define BME280_STBY_62p5ms              0x20U
#define BME280_STBY_125ms               0x40U
#define BME280_STBY_250ms               0x60U
#define BME280_STBY_500ms               0x80U
#define BME280_STBY_1s                  0xA0U
#define BME280_STBY_10ms                0xC0U
#define BME280_STBY_20ms                0xE0U

#define BME280_FILTER_MSK               0x1CU
#define BME280_FILTER_OFF               0x00U
#define BME280_FILTER_2                 0x04U
#define BME280_FILTER_4                 0x08U
#define BME280_FILTER_8                 0x0CU
#define BME280_FILTER_16                0x10U

typedef enum
{
    BME280_ERROR = 0,
    BME280_SUCCESS = 1
} BME280_RESULT;

typedef struct
{
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} BME280_Compensation_TypeDef;

extern BME280_Compensation_TypeDef cal_param;

void BME280_BindI2C(I2C_HandleTypeDef *hi2c);
void BME280_WriteReg(uint8_t reg, uint8_t value);
uint8_t BME280_ReadReg(uint8_t reg);
BME280_RESULT BME280_Check(void);
void BME280_Reset(void);
uint8_t BME280_GetVersion(void);
uint8_t BME280_GetStatus(void);
uint8_t BME280_GetMode(void);
void BME280_SetMode(uint8_t mode);
void BME280_SetFilter(uint8_t filter);
void BME280_SetStandby(uint8_t tsb);
void BME280_SetOSRST(uint8_t osrs);
void BME280_SetOSRSP(uint8_t osrs);
void BME280_SetOSRSH(uint8_t osrs);
BME280_RESULT BME280_Read_Calibration(void);
BME280_RESULT BME280_Read_UP(int32_t *UP);
BME280_RESULT BME280_Read_UT(int32_t *UT);
BME280_RESULT BME280_Read_UH(int32_t *UH);
BME280_RESULT BME280_Read_UTPH(int32_t *UT, int32_t *UP, int32_t *UH);
int32_t BME280_CalcT(int32_t UT);
uint32_t BME280_CalcP(int32_t UP);
uint32_t BME280_CalcH(int32_t UH);

#endif /* __BME280_H */