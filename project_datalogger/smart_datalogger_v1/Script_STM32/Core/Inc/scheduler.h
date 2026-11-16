#ifndef INC_SCHEDULER_H_
#define INC_SCHEDULER_H_

#include <stdint.h>

#define SCHEDULER_EVENT_NONE      0x00U
#define SCHEDULER_EVENT_LOG       0x01U
#define SCHEDULER_EVENT_UPLOAD    0x02U

typedef struct
{
    uint32_t logIntervalMs;
    uint32_t uploadIntervalMs;

    uint32_t lastLogTick;
    uint32_t lastUploadTick;

    uint8_t enabled;
} Scheduler_t;

void Scheduler_Init(
    Scheduler_t *scheduler,
    uint32_t now,
    uint32_t logIntervalMs,
    uint32_t uploadIntervalMs);

void Scheduler_Enable(
    Scheduler_t *scheduler,
    uint8_t enable);

void Scheduler_SetIntervals(
    Scheduler_t *scheduler,
    uint32_t logIntervalMs,
    uint32_t uploadIntervalMs);

uint8_t Scheduler_Update(
    Scheduler_t *scheduler,
    uint32_t now);

#endif /* INC_SCHEDULER_H_ */
