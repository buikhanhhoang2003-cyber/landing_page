#include "ethernet.h"

#include <Arduino.h>
#include <ETH.h>
#include <esp_arduino_version.h>

namespace
{
bool gEthernetReady = false;
}

void Ethernet_Init(void)
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    // Arduino ESP32 3.x moved the PHY type to the first argument.
    ETH.begin(
        ETH_PHY_LAN8720,
        1,
        23,
        18,
        -1,
        ETH_CLOCK_GPIO0_IN
    );
#else
    ETH.begin(
        1,
        -1,
        23,
        18,
        ETH_PHY_LAN8720,
        ETH_CLOCK_GPIO0_IN
    );
#endif

    Serial.println("Ethernet initialized");
}

void Ethernet_Task(void)
{
    if (!ETH.linkUp())
    {
        gEthernetReady = false;
        return;
    }

    if (!gEthernetReady)
    {
        gEthernetReady = true;
        Serial.print("Ethernet connected: ");
        Serial.println(ETH.localIP());
    }
}
