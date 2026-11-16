#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PACKET_TYPE_SENSOR 'S'
#define PACKET_TYPE_THERMAL_FRAME 'T'

typedef struct
{
    uint8_t mode;
    int16_t temperature10;
    uint16_t humidity10;
    uint16_t pressure10;
    uint32_t lightLux;
    uint16_t noise10;
    uint16_t adc1Mv;
    uint16_t adc2Mv;
    uint8_t di1;
    uint8_t di2;
    uint8_t di3;
    uint8_t di4;
    uint8_t validMask;
} SensorPacket_t;

#endif
