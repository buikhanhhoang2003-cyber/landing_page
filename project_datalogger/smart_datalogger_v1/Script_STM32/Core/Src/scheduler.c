#include "scheduler.h"

void Scheduler_Init(
    Scheduler_t *scheduler,
    uint32_t now,
    uint32_t logIntervalMs,
    uint32_t uploadIntervalMs)
{
    if (scheduler == 0)
    {
        return;
    }

    scheduler->logIntervalMs = logIntervalMs;
    scheduler->uploadIntervalMs = uploadIntervalMs;
    scheduler->lastLogTick = now;
    scheduler->lastUploadTick = now;
    scheduler->enabled = 0U;
}

void Scheduler_Enable(
    Scheduler_t *scheduler,
    uint8_t enable)
{
    if (scheduler == 0)
    {
        return;
    }

    scheduler->enabled = enable ? 1U : 0U;
}

void Scheduler_SetIntervals(
    Scheduler_t *scheduler,
    uint32_t logIntervalMs,
    uint32_t uploadIntervalMs)
{
    if (scheduler == 0)
    {
        return;
    }

    scheduler->logIntervalMs = logIntervalMs;
    scheduler->uploadIntervalMs = uploadIntervalMs;
}

uint8_t Scheduler_Update(
    Scheduler_t *scheduler,
    uint32_t now)
{
    uint8_t events = SCHEDULER_EVENT_NONE;

    if ((scheduler == 0) || (scheduler->enabled == 0U))
    {
        return SCHEDULER_EVENT_NONE;
    }

    if ((scheduler->logIntervalMs > 0U) &&
        ((now - scheduler->lastLogTick) >= scheduler->logIntervalMs))
    {
        scheduler->lastLogTick = now;
        events |= SCHEDULER_EVENT_LOG;
    }

    if ((scheduler->uploadIntervalMs > 0U) &&
        ((now - scheduler->lastUploadTick) >= scheduler->uploadIntervalMs))
    {
        scheduler->lastUploadTick = now;
        events |= SCHEDULER_EVENT_UPLOAD;
    }

    return events;
}
