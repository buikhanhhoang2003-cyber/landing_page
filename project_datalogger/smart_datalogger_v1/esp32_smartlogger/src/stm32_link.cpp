#include "stm32_link.h"

static constexpr uint32_t STM32_UART_BAUD = 115200U;
static constexpr int STM32_UART_RX_PIN = 14;
static constexpr int STM32_UART_TX_PIN = 12;
static constexpr uint32_t STM32_OFFLINE_TIMEOUT_MS = 2500U;
static constexpr uint32_t THERMAL_RESPONSE_TIMEOUT_MS = 1200U;

static HardwareSerial stm32Serial(2);
static SystemState state;
static ThermalCache thermal;

static char lineBuffer[256] = {0};
static size_t lineLength = 0U;

static bool thermalRequestPending = false;
static uint32_t thermalRequestMs = 0U;
static bool thermalCandidateT = false;
static bool binaryReceiving = false;
static uint16_t binaryIndex = 0U;

static uint16_t readU16LE(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static int16_t readI16LE(const uint8_t *p)
{
    return (int16_t)readU16LE(p);
}

static uint16_t crc16CcittFalse(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

static void markSeen()
{
    state.lastSeenMs = millis();
    state.stm32Online = true;
}

static void parseLine(const char *line)
{
    if ((line == nullptr) || (line[0] == '\0'))
    {
        return;
    }

    markSeen();

    unsigned int u0, u1, u2, u3, u4;
    unsigned long ul0, ul1;
    int i0, i1, i2, i3, i4;
    char stateText[4] = {0};

    if (sscanf(line, "S,%u,%u,%u,%u,%u,%d,%d", &u0, &u1, &u2, &u3, &u4, &i0, &i1) == 7)
    {
        state.mode = (uint8_t)u0;
        state.adc1Mv = (uint16_t)u1;
        state.adc2Mv = (uint16_t)u2;

        state.diMask = 0U;
        if (u3 != 0U) state.diMask |= 0x01U;
        if (u4 != 0U) state.diMask |= 0x02U;
        if (i0 != 0) state.diMask |= 0x04U;
        if (i1 != 0) state.diMask |= 0x08U;
        return;
    }

    if (sscanf(line, "STATUS,MODE,%u", &u0) == 1)
    {
        state.mode = (uint8_t)u0;
        return;
    }

    if (sscanf(line, "STATUS,IO,%u,%lu,%lu,%u,%u,%u,%u", &u0, &ul0, &ul1, &u1, &u2, &u3, &u4) == 7)
    {
        state.relayMask = (uint8_t)u0;
        state.dacCode = (uint16_t)ul0;
        state.dacMv = (uint16_t)ul1;
        state.dacRunning = (u1 != 0U);
        state.diMask = (uint8_t)u2;
        state.adc1Mv = (uint16_t)u3;
        state.adc2Mv = (uint16_t)u4;
        return;
    }

    if (sscanf(line, "STATUS,SENSOR,%d,%u,%u,%lu,%u,%u", &i0, &u0, &u1, &ul0, &u2, &u3) == 6)
    {
        state.temperature10 = (int16_t)i0;
        state.humidity10 = (uint16_t)u0;
        state.pressure10 = (uint16_t)u1;
        state.lightLux = (uint32_t)ul0;
        state.noise10 = (uint16_t)u2;
        state.sensorValidMask = (uint8_t)u3;
        return;
    }

    if (sscanf(line, "STATUS,THERMAL,%u,%u,%d,%d,%d,%d,%d", &u0, &u1, &i0, &i1, &i2, &i3, &i4) == 7)
    {
        state.thermalReady = (u0 != 0U);
        state.thermalSequence = (uint16_t)u1;
        state.thermalAmbient10 = (int16_t)i0;
        state.thermalCenter10 = (int16_t)i1;
        state.thermalMin10 = (int16_t)i2;
        state.thermalMax10 = (int16_t)i3;
        state.thermalLastError = (int16_t)i4;
        return;
    }


    if (sscanf(line, "STATUS,TIMER,%u,%u,%lu,%3s", &u0, &u1, &ul0, stateText) == 4)
    {
        if ((u0 >= 1U) && (u0 <= 4U))
        {
            RelayTimerState &slot = state.relayTimer[u0 - 1U];
            slot.active = (u1 != 0U);
            slot.remainingSeconds = (uint32_t)ul0;
            slot.targetOn = (strcmp(stateText, "ON") == 0);
        }
        return;
    }

    if (sscanf(line, "OK,TIMER,RELAY,%u,%lu,%3s", &u0, &ul0, stateText) == 3)
    {
        if ((u0 >= 1U) && (u0 <= 4U))
        {
            RelayTimerState &slot = state.relayTimer[u0 - 1U];
            slot.active = true;
            slot.remainingSeconds = (uint32_t)ul0;
            slot.targetOn = (strcmp(stateText, "ON") == 0);
        }
        return;
    }

    if (sscanf(line, "EVENT,TIMER,RELAY,%u,%3s", &u0, stateText) == 2)
    {
        if ((u0 >= 1U) && (u0 <= 4U))
        {
            RelayTimerState &slot = state.relayTimer[u0 - 1U];
            slot.active = false;
            slot.remainingSeconds = 0U;
            slot.targetOn = (strcmp(stateText, "ON") == 0);

            uint8_t mask = (uint8_t)(1U << (u0 - 1U));
            if (slot.targetOn)
            {
                state.relayMask |= mask;
            }
            else
            {
                state.relayMask &= (uint8_t)~mask;
            }
        }

        Serial.print("[TIMER EVENT] ");
        Serial.println(line);
        return;
    }

    if (strcmp(line, "OK,RESET,TIMER") == 0)
    {
        for (uint8_t i = 0U; i < 4U; i++)
        {
            state.relayTimer[i].active = false;
            state.relayTimer[i].remainingSeconds = 0U;
        }
        return;
    }

    if (sscanf(line, "OK,MODE,%u", &u0) == 1)
    {
        state.mode = (uint8_t)u0;
        return;
    }

    if (sscanf(line, "LOG,%lu,", &ul0) == 1)
    {
        char ack[40];
        snprintf(ack, sizeof(ack), "ACK,%lu", ul0);
        STM32Link_SendCommand(ack);
        return;
    }

    if (strncmp(line, "ERR,THERMAL", 11) == 0)
    {
        thermalRequestPending = false;
        thermalCandidateT = false;
        binaryReceiving = false;
        binaryIndex = 0U;
        return;
    }

    Serial.print("[STM32 RX] ");
    Serial.println(line);
}

static void finalizeThermalPacket()
{
    binaryReceiving = false;
    binaryIndex = 0U;
    thermalRequestPending = false;
    thermalCandidateT = false;

    if ((thermal.packet[0] != 'T') || (thermal.packet[1] != 'H') || (thermal.packet[2] != 1U))
    {
        return;
    }

    if ((thermal.packet[6] != THERMAL_WIDTH) || (thermal.packet[7] != THERMAL_HEIGHT))
    {
        return;
    }

    if (readU16LE(&thermal.packet[8]) != (THERMAL_PIXEL_COUNT * 2U))
    {
        return;
    }

    uint16_t expectedCrc = readU16LE(&thermal.packet[THERMAL_PACKET_SIZE - 2U]);
    uint16_t actualCrc = crc16CcittFalse(thermal.packet, THERMAL_PACKET_SIZE - 2U);

    if (expectedCrc != actualCrc)
    {
        thermal.crcErrors++;
        Serial.printf("[THERMAL] CRC error expected=%04X actual=%04X\n", expectedCrc, actualCrc);
        return;
    }

    thermal.sequence = readU16LE(&thermal.packet[4]);
    thermal.ambient10 = readI16LE(&thermal.packet[10]);
    thermal.center10 = readI16LE(&thermal.packet[12]);
    thermal.min10 = readI16LE(&thermal.packet[14]);
    thermal.max10 = readI16LE(&thermal.packet[16]);

    uint16_t offset = 18U;

    for (uint16_t i = 0U; i < THERMAL_PIXEL_COUNT; i++)
    {
        thermal.pixels[i] = readI16LE(&thermal.packet[offset]);
        offset = (uint16_t)(offset + 2U);
    }

    thermal.valid = true;
    thermal.receivedMs = millis();
    thermal.goodFrames++;

    state.thermalReady = true;
    state.thermalSequence = thermal.sequence;
    state.thermalAmbient10 = thermal.ambient10;
    state.thermalCenter10 = thermal.center10;
    state.thermalMin10 = thermal.min10;
    state.thermalMax10 = thermal.max10;

    markSeen();
}

static void pushAsciiByte(uint8_t byteValue)
{
    if (byteValue == '\r')
    {
        return;
    }

    if (byteValue == '\n')
    {
        if (lineLength > 0U)
        {
            lineBuffer[lineLength] = '\0';
            parseLine(lineBuffer);
            lineLength = 0U;
        }

        return;
    }

    if (lineLength < (sizeof(lineBuffer) - 1U))
    {
        lineBuffer[lineLength++] = (char)byteValue;
    }
    else
    {
        lineLength = 0U;
    }
}

void STM32Link_Init()
{
    stm32Serial.setRxBufferSize(4096U);
    stm32Serial.begin(STM32_UART_BAUD, SERIAL_8N1, STM32_UART_RX_PIN, STM32_UART_TX_PIN);
    Serial.println("[STM32] UART2 link initialized");
    Serial.println("[STM32] RX GPIO14 <- STM32 PA2");
    Serial.println("[STM32] TX GPIO12 -> STM32 PA3");
}

void STM32Link_Task()
{
    uint32_t now = millis();

    if (thermalRequestPending && ((uint32_t)(now - thermalRequestMs) >= THERMAL_RESPONSE_TIMEOUT_MS))
    {
        thermalRequestPending = false;
        thermalCandidateT = false;
        binaryReceiving = false;
        binaryIndex = 0U;
        Serial.println("[THERMAL] response timeout");
    }

    while (stm32Serial.available() > 0)
    {
        uint8_t b = (uint8_t)stm32Serial.read();

        if (binaryReceiving)
        {
            thermal.packet[binaryIndex++] = b;

            if (binaryIndex >= THERMAL_PACKET_SIZE)
            {
                finalizeThermalPacket();
            }

            continue;
        }

        if (thermalRequestPending)
        {
            if (thermalCandidateT)
            {
                if (b == (uint8_t)'H')
                {
                    thermal.packet[0] = (uint8_t)'T';
                    thermal.packet[1] = (uint8_t)'H';
                    binaryIndex = 2U;
                    binaryReceiving = true;
                    thermalCandidateT = false;
                    lineLength = 0U;
                    continue;
                }

                pushAsciiByte((uint8_t)'T');
                thermalCandidateT = false;
            }

            if ((lineLength == 0U) && (b == (uint8_t)'T'))
            {
                thermalCandidateT = true;
                continue;
            }
        }

        pushAsciiByte(b);
    }
}

void STM32Link_UpdateConnectionState(uint32_t now)
{
    if ((state.lastSeenMs == 0U) || ((uint32_t)(now - state.lastSeenMs) > STM32_OFFLINE_TIMEOUT_MS))
    {
        state.stm32Online = false;
    }
}

void STM32Link_SendCommand(const char *command)
{
    if ((command == nullptr) || (command[0] == '\0'))
    {
        return;
    }

    stm32Serial.print(command);
    stm32Serial.print("\r\n");

    bool quiet =
        (strncmp(command, "STATUS,", 7) == 0) ||
        (strcmp(command, "THERMAL,FRAME") == 0);

    if (!quiet)
    {
        Serial.print("[STM32 TX] ");
        Serial.println(command);
    }
}

bool STM32Link_RequestThermalFrame()
{
    if (thermalRequestPending)
    {
        return false;
    }

    thermalRequestPending = true;
    thermalRequestMs = millis();
    thermalCandidateT = false;
    binaryReceiving = false;
    binaryIndex = 0U;

    STM32Link_SendCommand("THERMAL,FRAME");
    return true;
}

bool STM32Link_ThermalRequestPending()
{
    return thermalRequestPending;
}

const SystemState &STM32Link_GetState()
{
    return state;
}

const ThermalCache &STM32Link_GetThermalCache()
{
    return thermal;
}
