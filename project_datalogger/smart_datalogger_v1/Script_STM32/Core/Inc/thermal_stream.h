#ifndef THERMAL_STREAM_H
#define THERMAL_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define THERMAL_STREAM_WIDTH             32U
#define THERMAL_STREAM_HEIGHT            24U
#define THERMAL_STREAM_PIXEL_COUNT       (THERMAL_STREAM_WIDTH * THERMAL_STREAM_HEIGHT)
#define THERMAL_STREAM_PAYLOAD_BYTES     (THERMAL_STREAM_PIXEL_COUNT * 2U)
#define THERMAL_STREAM_HEADER_BYTES      18U
#define THERMAL_STREAM_CRC_BYTES         2U
#define THERMAL_STREAM_PACKET_BYTES      (THERMAL_STREAM_HEADER_BYTES + THERMAL_STREAM_PAYLOAD_BYTES + THERMAL_STREAM_CRC_BYTES)
#define THERMAL_STREAM_PROTOCOL_VERSION  1U
#define THERMAL_STREAM_TX_TIMEOUT_MS     250U

int16_t ThermalStream_EncodeTemperature10(float temperature);
uint16_t ThermalStream_Crc16CcittFalse(const uint8_t *data, uint16_t length);
HAL_StatusTypeDef ThermalStream_SendFrame(UART_HandleTypeDef *huart, const float *pixels, uint16_t sequence, float ambientTemp, float centerTemp, float minTemp, float maxTemp);

#ifdef __cplusplus
}
#endif

#endif
