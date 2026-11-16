#include <Arduino.h>

HardwareSerial stm32(2);

static constexpr uint32_t COMMAND_INTERVAL_MS = 5000U;

static uint32_t lastCommandTick = 0U;
static uint8_t step = 0U;

void setup()
{
    Serial.begin(115200);

    // ESP32 RX GPIO14 <- STM32 PA2 / USART2_TX
    // ESP32 TX GPIO12 -> STM32 PA3 / USART2_RX
    stm32.begin(115200, SERIAL_8N1, 14, 12);

    delay(2000U);

    Serial.println();
    Serial.println("ESP32 RELAY TEST");
}

void loop()
{
    uint32_t now = millis();

    // Always read the STM32 response
    while (stm32.available() > 0)
    {
        Serial.write(stm32.read());
    }

    if ((uint32_t)(now - lastCommandTick) >= COMMAND_INTERVAL_MS)
    {
        lastCommandTick = now;

        switch (step)
        {
            case 0:
                Serial.println("[TX] MODE,3");
                stm32.print("MODE,3\r\n");
                break;

            case 1:
                Serial.println("[TX] RELAY,1,ON");
                stm32.print("RELAY,1,ON\r\n");
                break;

            case 2:
                Serial.println("[TX] RELAY,2,ON");
                stm32.print("RELAY,2,ON\r\n");
                break;

            case 3:
                Serial.println("[TX] RELAY,3,ON");
                stm32.print("RELAY,3,ON\r\n");
                break;

            case 4:
                Serial.println("[TX] RELAY,4,ON");
                stm32.print("RELAY,4,ON\r\n");
                break;

            case 5:
                Serial.println("[TX] RELAY,1,OFF");
                stm32.print("RELAY,1,OFF\r\n");
                break;

            case 6:
                Serial.println("[TX] RELAY,2,OFF");
                stm32.print("RELAY,2,OFF\r\n");
                break;

            case 7:
                Serial.println("[TX] RELAY,3,OFF");
                stm32.print("RELAY,3,OFF\r\n");
                break;

            case 8:
                Serial.println("[TX] RELAY,4,OFF");
                stm32.print("RELAY,4,OFF\r\n");
                break;
        }

        step++;

        if (step > 8U)
        {
            step = 0U;
        }
    }
}
