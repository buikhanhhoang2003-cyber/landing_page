#include <Arduino.h>

HardwareSerial stm32(2);

static uint32_t lastTick = 0U;
static uint8_t step = 0U;

void sendCommand(const char *command)
{
    Serial.print("[TX] ");
    Serial.println(command);

    stm32.print(command);
    stm32.print("\r\n");
}

void setup()
{
    Serial.begin(115200);

    // ESP32 RX GPIO14 <- STM32 PA2 / USART2_TX
    // ESP32 TX GPIO12 -> STM32 PA3 / USART2_RX
    stm32.begin(115200, SERIAL_8N1, 14, 12);

    delay(2000U);

    Serial.println();
    Serial.println("==============================");
    Serial.println(" RELAY TIMER TEST");
    Serial.println("==============================");

    lastTick = millis();
}

void loop()
{
    uint32_t now = millis();

    // Always read the response from STM32
    while (stm32.available() > 0)
    {
        Serial.write(stm32.read());
    }

    switch (step)
    {
        // Switch to MODE 4
        case 0:
            if ((uint32_t)(now - lastTick) >= 5000U)
            {
                lastTick = now;

                sendCommand("MODE,4");

                step = 1U;
            }
            break;

        // Turn off all relays
        case 1:
            if ((uint32_t)(now - lastTick) >= 5000U)
            {
                lastTick = now;

                sendCommand("RELAY,ALL,OFF");

                step = 2U;
            }
            break;

        // Schedule Relay 1 ON after 10 seconds
        case 2:
            if ((uint32_t)(now - lastTick) >= 5000U)
            {
                lastTick = now;

                sendCommand("TIMER RELAY 1 10s ON");

                Serial.println("[TEST] Waiting 10 seconds for Relay 1...");

                step = 3U;
            }
            break;

        // Wait for Relay 1 to execute
        case 3:
            if ((uint32_t)(now - lastTick) >= 15000U)
            {
                lastTick = now;

                Serial.println("[TEST] Relay 1 timer finished.");

                // Schedule Relay 3 ON after 10 seconds
                sendCommand("TIMER RELAY 3 10s ON");

                Serial.println("[TEST] Waiting 10 seconds for Relay 3...");

                step = 4U;
            }
            break;

        // Wait for Relay 3 to execute
        case 4:
            if ((uint32_t)(now - lastTick) >= 15000U)
            {
                lastTick = now;

                Serial.println("[TEST] Relay 3 timer finished.");
                Serial.println("[TEST] Test complete.");
                Serial.println();

                step = 5U;
            }
            break;

        // Hold the state for observation
        case 5:
            break;
    }
}
