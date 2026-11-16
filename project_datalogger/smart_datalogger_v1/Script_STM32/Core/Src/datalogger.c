/*
 * datalogger.c
 *
 *  Created on: Sep 19, 2026
 *      Author: root
 */


#include "datalogger.h"

void DataLogger_Init(DataLogger_t *logger)
{
    if (logger == 0)
    {
        return;
    }

    logger->head = 0U;
    logger->tail = 0U;
    logger->count = 0U;
    logger->nextSequence = 1U;
    logger->totalLogged = 0U;
    logger->totalAcked = 0U;
    logger->dropped = 0U;
}

uint8_t DataLogger_Push(
    DataLogger_t *logger,
    DataLogRecord_t *record)
{
    if ((logger == 0) || (record == 0))
    {
        return 0U;
    }

    /*
     * Do not overwrite unacknowledged data. If the ring is full,
     * reject the new sample and account it as dropped.
     */
    if (logger->count >= DATALOGGER_CAPACITY)
    {
        logger->dropped++;
        return 0U;
    }

    record->sequence = logger->nextSequence++;
    logger->records[logger->head] = *record;

    logger->head++;
    if (logger->head >= DATALOGGER_CAPACITY)
    {
        logger->head = 0U;
    }

    logger->count++;
    logger->totalLogged++;

    return 1U;
}

const DataLogRecord_t *DataLogger_Peek(
    const DataLogger_t *logger)
{
    if ((logger == 0) || (logger->count == 0U))
    {
        return 0;
    }

    return &logger->records[logger->tail];
}

uint8_t DataLogger_AckOldest(
    DataLogger_t *logger,
    uint32_t sequence)
{
    const DataLogRecord_t *record;

    if (logger == 0)
    {
        return 0U;
    }

    record = DataLogger_Peek(logger);
    if ((record == 0) || (record->sequence != sequence))
    {
        return 0U;
    }

    logger->tail++;
    if (logger->tail >= DATALOGGER_CAPACITY)
    {
        logger->tail = 0U;
    }

    logger->count--;
    logger->totalAcked++;

    return 1U;
}

uint16_t DataLogger_Count(
    const DataLogger_t *logger)
{
    if (logger == 0)
    {
        return 0U;
    }

    return logger->count;
}
