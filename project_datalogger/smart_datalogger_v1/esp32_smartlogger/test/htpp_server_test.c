#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

/* =========================================================
 * Existing Wi-Fi
 * ========================================================= */

static const char *WIFI_SSID = "YOUR_WIFI_SSID";
static const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

/* =========================================================
 * Timing
 * ========================================================= */

static constexpr uint32_t HEARTBEAT_MS = 5000U;
static constexpr uint32_t DOT_INTERVAL_MS = 1000U;
static constexpr uint32_t RECONNECT_INTERVAL_MS = 10000U;

static uint32_t lastHeartbeatMs = 0U;
static uint32_t lastDotMs = 0U;
static uint32_t lastReconnectMs = 0U;

static uint32_t httpRequestCount = 0U;

static bool serverStarted = false;

/* =========================================================
 * HTTP Server
 * ========================================================= */

WebServer server(80);

/* =========================================================
 * Web page
 * ========================================================= */

static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>

<head>

<meta charset="utf-8">

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>ESP32 WiFi Test</title>

<style>

body {
    font-family: Arial, sans-serif;
    background: #f4f5f7;
    margin: 0;
    padding: 30px;
    text-align: center;
}

.card {
    max-width: 450px;
    margin: auto;
    background: white;
    border: 1px solid #ddd;
    border-radius: 14px;
    padding: 24px;
}

.ok {
    color: #188038;
    font-size: 22px;
    font-weight: bold;
}

.info {
    color: #555;
    margin-top: 15px;
}

a {
    display: block;
    margin: 15px;
}

</style>

</head>

<body>

<div class="card">

<h1>ESP32 HTTP Server</h1>

<p class="ok">
WIFI + HTTP OK
</p>

<p>
ESP32 is connected to existing Wi-Fi.
</p>

<div class="info">
SSID: Hang Thanh
</div>

<a href="/ping">
Test /ping
</a>

<a href="/diag">
WiFi Diagnostic
</a>

</div>

</body>

</html>
)HTML";

/* =========================================================
 * WiFi status
 * ========================================================= */

static const char *wifiStatusName(wl_status_t status)
{
    switch (status)
    {
        case WL_IDLE_STATUS:
            return "IDLE";

        case WL_NO_SSID_AVAIL:
            return "SSID NOT FOUND";

        case WL_SCAN_COMPLETED:
            return "SCAN COMPLETED";

        case WL_CONNECTED:
            return "CONNECTED";

        case WL_CONNECT_FAILED:
            return "CONNECT FAILED";

        case WL_CONNECTION_LOST:
            return "CONNECTION LOST";

        case WL_DISCONNECTED:
            return "DISCONNECTED";

        default:
            return "UNKNOWN";
    }
}

/* =========================================================
 * Wi-Fi Events
 * ========================================================= */

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    (void)info;

    switch (event)
    {
        case ARDUINO_EVENT_WIFI_STA_START:
        {
            Serial.println();
            Serial.println("[WIFI EVENT] STA START");
            break;
        }

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        {
            Serial.println();
            Serial.println("[WIFI EVENT] CONNECTED TO ACCESS POINT");
            break;
        }

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        {
            Serial.println();
            Serial.println("[WIFI EVENT] GOT IP");

            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());

            Serial.print("[HTTP] WEB: http://");
            Serial.print(WiFi.localIP());
            Serial.println("/");

            break;
        }

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        {
            Serial.println();
            Serial.println("[WIFI EVENT] DISCONNECTED");
            break;
        }

        default:
            break;
    }
}

/* =========================================================
 * HTTP /
 * ========================================================= */

static void handleRoot()
{
    httpRequestCount++;

    Serial.println();
    Serial.println("[HTTP] GET /");

    server.send_P(
        200,
        "text/html",
        PAGE
    );
}

/* =========================================================
 * HTTP /ping
 * ========================================================= */

static void handlePing()
{
    httpRequestCount++;

    Serial.println();
    Serial.println("[HTTP] GET /ping");

    server.send(
        200,
        "text/plain",
        "ESP32 HTTP SERVER OK"
    );
}

/* =========================================================
 * HTTP /diag
 * ========================================================= */

static void handleDiag()
{
    httpRequestCount++;

    Serial.println();
    Serial.println("[HTTP] GET /diag");

    String text;

    text.reserve(1000);

    text += "ESP32 WIFI DIAGNOSTIC\n";
    text += "============================\n";

    text += "STATUS: ";
    text += wifiStatusName(WiFi.status());
    text += "\n";

    text += "SSID: ";
    text += WiFi.SSID();
    text += "\n";

    text += "IP: ";
    text += WiFi.localIP().toString();
    text += "\n";

    text += "GATEWAY: ";
    text += WiFi.gatewayIP().toString();
    text += "\n";

    text += "SUBNET: ";
    text += WiFi.subnetMask().toString();
    text += "\n";

    text += "DNS: ";
    text += WiFi.dnsIP().toString();
    text += "\n";

    text += "ESP MAC: ";
    text += WiFi.macAddress();
    text += "\n";

    text += "AP BSSID: ";
    text += WiFi.BSSIDstr();
    text += "\n";

    text += "CHANNEL: ";
    text += String(WiFi.channel());
    text += "\n";

    text += "RSSI: ";
    text += String(WiFi.RSSI());
    text += " dBm\n";

    text += "HTTP REQUESTS: ";
    text += String(httpRequestCount);
    text += "\n";

    server.send(
        200,
        "text/plain",
        text
    );
}

/* =========================================================
 * Start HTTP server
 * ========================================================= */

static void startHttpServer()
{
    if (serverStarted)
    {
        return;
    }

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    server.on(
        "/ping",
        HTTP_GET,
        handlePing
    );

    server.on(
        "/diag",
        HTTP_GET,
        handleDiag
    );

    server.onNotFound(
        []()
        {
            httpRequestCount++;

            Serial.print("[HTTP] 404 ");
            Serial.println(server.uri());

            server.send(
                404,
                "text/plain",
                "404 Not Found"
            );
        }
    );

    server.begin();

    serverStarted = true;

    Serial.println();
    Serial.println("[HTTP] SERVER STARTED");

    Serial.print("[HTTP] WEB  : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");

    Serial.print("[HTTP] PING : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/ping");

    Serial.print("[HTTP] DIAG : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/diag");
}

/* =========================================================
 * Heartbeat
 * ========================================================= */

static void printHeartbeat()
{
    Serial.println();
    Serial.println("========== HEARTBEAT ==========");

    Serial.print("STATUS     : ");
    Serial.println(wifiStatusName(WiFi.status()));

    Serial.print("SSID       : ");
    Serial.println(WiFi.SSID());

    Serial.print("ESP MAC    : ");
    Serial.println(WiFi.macAddress());

    Serial.print("AP BSSID   : ");
    Serial.println(WiFi.BSSIDstr());

    Serial.print("IP         : ");
    Serial.println(WiFi.localIP());

    Serial.print("GATEWAY    : ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("SUBNET     : ");
    Serial.println(WiFi.subnetMask());

    Serial.print("DNS        : ");
    Serial.println(WiFi.dnsIP());

    Serial.print("CHANNEL    : ");
    Serial.println(WiFi.channel());

    Serial.print("RSSI       : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("SIGNAL     : ");

    int32_t rssi = WiFi.RSSI();

    if (rssi >= -50)
    {
        Serial.println("EXCELLENT");
    }
    else if (rssi >= -60)
    {
        Serial.println("VERY GOOD");
    }
    else if (rssi >= -70)
    {
        Serial.println("GOOD");
    }
    else if (rssi >= -80)
    {
        Serial.println("WEAK");
    }
    else
    {
        Serial.println("VERY WEAK");
    }

    Serial.print("WEB        : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");

    Serial.print("PING       : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/ping");

    Serial.print("DIAG       : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/diag");

    Serial.print("HTTP REQ   : ");
    Serial.println(httpRequestCount);

    Serial.println("===============================");
}

/* =========================================================
 * Connect Wi-Fi
 * ========================================================= */

static void connectWiFi()
{
    Serial.println();
    Serial.println("================================");
    Serial.println(" CONNECTING TO WIFI");
    Serial.println("================================");

    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    Serial.print("[WIFI] Connecting ");
}

/* =========================================================
 * Setup
 * ========================================================= */

void setup()
{
    Serial.begin(115200);

    delay(1000U);

    Serial.println();
    Serial.println("================================");
    Serial.println(" ESP-32S STA + HTTP TEST");
    Serial.println("================================");

    /*
     * WiFi events
     */
    WiFi.onEvent(onWiFiEvent);

    /*
     * Do not save credentials to flash.
     */
    WiFi.persistent(false);

    /*
     * Station only.
     *
     * NO SoftAP.
     */
    WiFi.mode(WIFI_STA);

    /*
     * Auto reconnect.
     */
    WiFi.setAutoReconnect(true);

    /*
     * Disable WiFi sleep while testing.
     */
    WiFi.setSleep(false);

    /*
     * Start connection.
     */
    connectWiFi();
}

/* =========================================================
 * Loop
 * ========================================================= */

void loop()
{
    uint32_t now = millis();

    /* =====================================================
     * Wi-Fi connected
     * ===================================================== */

    if (WiFi.status() == WL_CONNECTED)
    {
        /*
         * Start server once IP is available.
         */
        if (!serverStarted)
        {
            Serial.println();

            startHttpServer();

            printHeartbeat();
        }

        /*
         * Process HTTP.
         */
        server.handleClient();

        /*
         * Heartbeat every 5 seconds.
         */
        if ((uint32_t)(now - lastHeartbeatMs) >= HEARTBEAT_MS)
        {
            lastHeartbeatMs = now;

            printHeartbeat();
        }

        return;
    }

    /* =====================================================
     * Wi-Fi disconnected
     * ===================================================== */

    if ((uint32_t)(now - lastDotMs) >= DOT_INTERVAL_MS)
    {
        lastDotMs = now;

        Serial.print(".");
    }

    /*
     * Retry connection every 10 seconds.
     */
    if ((uint32_t)(now - lastReconnectMs) >= RECONNECT_INTERVAL_MS)
    {
        lastReconnectMs = now;

        Serial.println();

        Serial.print("[WIFI] Status: ");
        Serial.println(wifiStatusName(WiFi.status()));

        Serial.println("[WIFI] Reconnecting...");

        WiFi.disconnect();

        delay(100U);

        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );
    }
}
