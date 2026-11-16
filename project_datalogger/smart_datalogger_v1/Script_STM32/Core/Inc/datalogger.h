/*
 * datalogger.h
 *
 *  Created on: Sep 19, 2026
 *      Author: root
 */

#ifndef INC_DATALOGGER_H_
#define INC_DATALOGGER_H_

#include <stdint.h>

#define DATALOGGER_CAPACITY            32U

#define DATALOGGER_VALID_SHT30         0x01U
#define DATALOGGER_VALID_BME280        0x02U
#define DATALOGGER_VALID_SEN0390       0x04U
#define DATALOGGER_VALID_SM7901B       0x08U

typedef struct
{
    uint32_t sequence;
    uint32_t tickMs;

    int16_t temperature10;
    uint16_t humidity10;
    uint16_t pressure10;
    uint32_t lightLux;
    uint16_t noise10;

    uint16_t adc1Mv;
    uint16_t adc2Mv;
    uint8_t digitalInputs;
    uint8_t validMask;
} DataLogRecord_t;

typedef struct
{
    DataLogRecord_t records[DATALOGGER_CAPACITY];
    uint16_t head;
    uint16_t tail;
    uint16_t count;

    uint32_t nextSequence;
    uint32_t totalLogged;
    uint32_t totalAcked;
    uint32_t dropped;
} DataLogger_t;

void DataLogger_Init(DataLogger_t *logger);

uint8_t DataLogger_Push(
    DataLogger_t *logger,
    DataLogRecord_t *record);

const DataLogRecord_t *DataLogger_Peek(
    const DataLogger_t *logger);

uint8_t DataLogger_AckOldest(
    DataLogger_t *logger,
    uint32_t sequence);

uint16_t DataLogger_Count(
    const DataLogger_t *logger);

#endif /* INC_DATALOGGER_H_ */

