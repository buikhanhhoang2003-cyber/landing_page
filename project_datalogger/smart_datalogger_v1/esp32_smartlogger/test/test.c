#include <Arduino.h>

HardwareSerial stm32(2);

static constexpr uint32_t MODE_INTERVAL_MS = 5000U;

static uint32_t lastModeTick = 0U;
static uint8_t mode = 0U;

void setup()
{
    Serial.begin(115200);

    // ESP32 RX = GPIO14 <- STM32 PA2 / USART2_TX
    // ESP32 TX = GPIO12 -> STM32 PA3 / USART2_RX
    stm32.begin(115200, SERIAL_8N1, 14, 12);

    delay(2000U);

    Serial.println();
    Serial.println("ESP32 AUTO MODE TEST");
    Serial.println("Change mode every 5 seconds");
}

void loop()
{
    uint32_t now = millis();

    // Print the STM32 data returned for debugging
    while (stm32.available() > 0)
    {
        Serial.write(stm32.read());
    }

    if ((uint32_t)(now - lastModeTick) >= MODE_INTERVAL_MS)
    {
        lastModeTick = now;

        String command = String("MODE,") + mode;

        Serial.print("[TX] ");
        Serial.println(command);

        stm32.print(command);
        stm32.print("\r\n");

        mode++;

        if (mode > 4U)
        {
            mode = 0U;
        }
    }
}
