#include "line_uplink.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <time.h>

#if __has_include("line_config.h")
#include "line_config.h"
#endif

#ifndef LINE_ENABLED
#define LINE_ENABLED false
#endif
#ifndef LINE_SERVER_URL
#define LINE_SERVER_URL ""
#endif
#ifndef LINE_API_KEY
#define LINE_API_KEY ""
#endif
#ifndef LINE_DEVICE_ID
#define LINE_DEVICE_ID ""
#endif
#ifndef LINE_SEND_INTERVAL_MS
#define LINE_SEND_INTERVAL_MS 300000UL
#endif
#ifndef LINE_HTTP_TIMEOUT_MS
#define LINE_HTTP_TIMEOUT_MS 10000UL
#endif
#ifndef LINE_MAX_ATTEMPTS
#define LINE_MAX_ATTEMPTS 4U
#endif
#ifndef LINE_ROOT_CA
#define LINE_ROOT_CA ""
#endif

static_assert(LINE_SEND_INTERVAL_MS >= 1000UL && LINE_SEND_INTERVAL_MS <= 0x7FFFFFFFUL,
              "LINE_SEND_INTERVAL_MS must be between 1000 and 2147483647");
static_assert(LINE_HTTP_TIMEOUT_MS >= 1000UL && LINE_HTTP_TIMEOUT_MS <= 60000UL,
              "LINE_HTTP_TIMEOUT_MS must be between 1000 and 60000");
static_assert(LINE_MAX_ATTEMPTS >= 1U && LINE_MAX_ATTEMPTS <= 6U,
              "LINE_MAX_ATTEMPTS must be between 1 and 6");

namespace
{
struct Snapshot
{
    SystemState state;
    uint32_t uptimeMs;
};

QueueHandle_t snapshotQueue = nullptr;
String endpoint;
String deviceId;
bool useTls = false;
bool connectionObserved = false;
bool firstSnapshotQueued = false;
uint32_t connectedSinceMs = 0U;
uint32_t lastSnapshotMs = 0U;

bool validDeviceId(const String &value)
{
    if (value.isEmpty() || value.length() > 64U) return false;
    for (size_t i = 0U; i < value.length(); i++)
    {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':'))
            return false;
    }
    return true;
}

String buildPayload(const Snapshot &snapshot)
{
    const SystemState &s = snapshot.state;
    // deviceId is validated to contain no JSON metacharacters; all other fields
    // are numbers/booleans. No String or live STM32 state crosses task boundaries.
    char body[1024];
    const int length = snprintf(
        body, sizeof(body),
        "{\"device_id\":\"%s\",\"uptime_ms\":%lu,\"online\":%s,\"mode\":%u,"
        "\"temperature10\":%d,\"humidity10\":%u,\"pressure10\":%u,"
        "\"lightLux\":%lu,\"noise10\":%u,\"validMask\":%u,"
        "\"adc1Mv\":%u,\"adc2Mv\":%u,\"diMask\":%u,\"relays\":%u,"
        "\"thermalReady\":%s,\"thermalAmbient10\":%d,\"thermalCenter10\":%d,"
        "\"thermalMin10\":%d,\"thermalMax10\":%d}",
        deviceId.c_str(), (unsigned long)snapshot.uptimeMs,
        s.stm32Online ? "true" : "false", (unsigned int)s.mode,
        (int)s.temperature10, (unsigned int)s.humidity10, (unsigned int)s.pressure10,
        (unsigned long)s.lightLux, (unsigned int)s.noise10, (unsigned int)s.sensorValidMask,
        (unsigned int)s.adc1Mv, (unsigned int)s.adc2Mv,
        (unsigned int)s.diMask, (unsigned int)s.relayMask,
        (s.stm32Online && s.mode == 2U && s.thermalReady && s.thermalLastError == 0)
            ? "true" : "false", (int)s.thermalAmbient10,
        (int)s.thermalCenter10, (int)s.thermalMin10, (int)s.thermalMax10);
    if (length < 0 || (size_t)length >= sizeof(body)) return String();
    return String(body);
}

void createEventId(char (&eventId)[37])
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0FU) | 0x40U; // UUID v4
    bytes[8] = (bytes[8] & 0x3FU) | 0x80U;
    snprintf(eventId, sizeof(eventId),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

bool waitForTlsClock()
{
    // Start SNTP only after STA has connected. Provisioning changes Wi-Fi mode
    // during setup, before the worker has a connection to reach an NTP server.
    if (time(nullptr) < 1704067200)
        configTime(0, 0, "pool.ntp.org", "time.google.com");
    const uint32_t started = millis();
    while (time(nullptr) < 1704067200) // 2024-01-01 UTC
    {
        if (WiFi.status() != WL_CONNECTED || (uint32_t)(millis() - started) >= 15000U)
            return false;
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
    return true;
}

int postPayload(const String &payload, const char *eventId)
{
    if (WiFi.status() != WL_CONNECTED) return -1;

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    HTTPClient http;
    if (useTls)
    {
        if (!waitForTlsClock())
        {
            Serial.println("[LINE] TLS clock not synchronized; check Internet/NTP access");
            return -1;
        }
        secureClient.setCACert(LINE_ROOT_CA);
        secureClient.setHandshakeTimeout((LINE_HTTP_TIMEOUT_MS + 999UL) / 1000UL);
        if (!http.begin(secureClient, endpoint)) return -1;
    }
    else if (!http.begin(plainClient, endpoint))
    {
        return -1;
    }

    http.setConnectTimeout(LINE_HTTP_TIMEOUT_MS);
    http.setTimeout(LINE_HTTP_TIMEOUT_MS);
    // Never forward the API key to a redirect destination.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", LINE_API_KEY);
    http.addHeader("X-Event-ID", eventId);
    const int code = http.POST(payload);
    http.end();
    return code;
}

void senderTask(void *)
{
    Snapshot snapshot;
    for (;;)
    {
        if (xQueueReceive(snapshotQueue, &snapshot, portMAX_DELAY) != pdTRUE) continue;
        const String payload = buildPayload(snapshot);
        if (payload.isEmpty())
        {
            Serial.println("[LINE] Snapshot could not be serialized");
            continue;
        }

        // Both body and ID stay identical through a retry, allowing the server
        // and LINE Messaging API to avoid sending duplicate notifications.
        char eventId[37];
        createEventId(eventId);
        for (unsigned int attempt = 0U; attempt < LINE_MAX_ATTEMPTS; attempt++)
        {
            const int code = postPayload(payload, eventId);
            if (code >= 200 && code < 300)
            {
                Serial.printf("[LINE] Telemetry accepted by server (HTTP %d)\n", code);
                break;
            }

            Serial.printf("[LINE] Send failed (HTTP %d, attempt %u/%u)\n",
                          code, attempt + 1U, (unsigned int)LINE_MAX_ATTEMPTS);
            const bool retryable = code <= 0 || code == 408 || code == 429 || code >= 500;
            if (!retryable || attempt + 1U >= LINE_MAX_ATTEMPTS)
            {
                Serial.println("[LINE] Snapshot dropped; next scheduled snapshot will still be sent");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2000UL << attempt));
        }
    }
}
} // namespace

void LineUplink_Init()
{
    if (!LINE_ENABLED)
    {
        Serial.println("[LINE] Uplink disabled; configure include/line_config.h to enable");
        return;
    }
    endpoint = LINE_SERVER_URL;
    while (endpoint.endsWith("/")) endpoint.remove(endpoint.length() - 1U);
    useTls = endpoint.startsWith("https://");
    const String key = LINE_API_KEY;
    if ((!useTls && !endpoint.startsWith("http://")) ||
        key.isEmpty() || key.indexOf('\r') >= 0 || key.indexOf('\n') >= 0 ||
        (useTls && strlen(LINE_ROOT_CA) == 0U))
    {
        Serial.println("[LINE] Invalid config: set server base URL, API key and HTTPS root CA");
        return;
    }
    endpoint += "/api/telemetry";
    deviceId = LINE_DEVICE_ID;
    if (deviceId.isEmpty())
    {
        const uint64_t mac = ESP.getEfuseMac();
        char id[32];
        snprintf(id, sizeof(id), "esp32-%04x%08lx", (unsigned int)(mac >> 32U),
                 (unsigned long)(mac & 0xFFFFFFFFULL));
        deviceId = id;
    }
    if (!validDeviceId(deviceId))
    {
        Serial.println("[LINE] Invalid device ID: use 1-64 letters, digits, '.', '_', ':' or '-'");
        return;
    }

    snapshotQueue = xQueueCreate(1U, sizeof(Snapshot));
    if (snapshotQueue == nullptr)
    {
        Serial.println("[LINE] Unable to allocate snapshot queue");
        return;
    }
    if (xTaskCreate(senderTask, "line-uplink", 8192U, nullptr, 1U, nullptr) != pdPASS)
    {
        vQueueDelete(snapshotQueue);
        snapshotQueue = nullptr;
        Serial.println("[LINE] Unable to start sender task");
        return;
    }
    Serial.printf("[LINE] Uplink enabled for %s; interval=%lu ms\n", deviceId.c_str(),
                  (unsigned long)LINE_SEND_INTERVAL_MS);
}

void LineUplink_Task(uint32_t now, bool networkConnected, const SystemState &state)
{
    if (snapshotQueue == nullptr) return;
    if (!networkConnected)
    {
        connectionObserved = false;
        return;
    }
    if (!connectionObserved)
    {
        connectedSinceMs = now;
        connectionObserved = true;
    }
    // Give STATUS polling five seconds after connecting to populate the data.
    if ((uint32_t)(now - connectedSinceMs) < 5000U) return;
    if (firstSnapshotQueued && (uint32_t)(now - lastSnapshotMs) < LINE_SEND_INTERVAL_MS) return;

    Snapshot snapshot = {state, now};
    // Only the newest waiting snapshot is retained, avoiding unbounded memory
    // use and stale-message bursts when the server/network is unavailable.
    xQueueOverwrite(snapshotQueue, &snapshot);
    firstSnapshotQueued = true;
    lastSnapshotMs = now;
}
