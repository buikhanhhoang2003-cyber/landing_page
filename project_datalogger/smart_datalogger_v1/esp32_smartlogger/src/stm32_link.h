#ifndef STM32_LINK_H
#define STM32_LINK_H

#include <Arduino.h>

static constexpr uint16_t THERMAL_WIDTH = 32U;
static constexpr uint16_t THERMAL_HEIGHT = 24U;
static constexpr uint16_t THERMAL_PIXEL_COUNT = THERMAL_WIDTH * THERMAL_HEIGHT;
static constexpr uint16_t THERMAL_PACKET_SIZE = 1556U;


struct RelayTimerState
{
    bool active = false;
    uint32_t remainingSeconds = 0U;
    bool targetOn = false;
};

struct SystemState
{
    bool stm32Online = false;
    uint32_t lastSeenMs = 0U;
    uint8_t mode = 0U;
    uint8_t relayMask = 0U;
    uint16_t dacCode = 0U;
    uint16_t dacMv = 0U;
    bool dacRunning = false;
    uint8_t diMask = 0U;
    uint16_t adc1Mv = 0U;
    uint16_t adc2Mv = 0U;
    int16_t temperature10 = 0;
    uint16_t humidity10 = 0U;
    uint16_t pressure10 = 0U;
    uint32_t lightLux = 0U;
    uint16_t noise10 = 0U;
    uint8_t sensorValidMask = 0U;
    bool thermalReady = false;
    uint16_t thermalSequence = 0U;
    int16_t thermalAmbient10 = 0;
    int16_t thermalCenter10 = 0;
    int16_t thermalMin10 = 0;
    int16_t thermalMax10 = 0;
    int16_t thermalLastError = 0;
    RelayTimerState relayTimer[4];
};

struct ThermalCache
{
    bool valid = false;
    uint16_t sequence = 0U;
    int16_t ambient10 = 0;
    int16_t center10 = 0;
    int16_t min10 = 0;
    int16_t max10 = 0;
    uint32_t receivedMs = 0U;
    uint32_t goodFrames = 0U;
    uint32_t crcErrors = 0U;
    uint8_t packet[THERMAL_PACKET_SIZE] = {0};
    int16_t pixels[THERMAL_PIXEL_COUNT] = {0};
};

void STM32Link_Init();
void STM32Link_Task();
void STM32Link_UpdateConnectionState(uint32_t now);
void STM32Link_SendCommand(const char *command);
bool STM32Link_RequestThermalFrame();
bool STM32Link_ThermalRequestPending();
const SystemState &STM32Link_GetState();
const ThermalCache &STM32Link_GetThermalCache();

#endif
