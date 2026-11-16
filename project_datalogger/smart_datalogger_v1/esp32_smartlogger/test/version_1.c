#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include "stm32_link.h"

/* =========================================================
 * Provisioning AP
 * ========================================================= */

static const char *SETUP_AP_SSID = "YOUR_WIFI_SSID";
static const char *SETUP_AP_PASSWORD = "YOUR_WIFI_PASSWORD";

/* =========================================================
 * Timing
 * ========================================================= */

static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 20000U;
static constexpr uint32_t STATUS_POLL_MS = 1000U;
static constexpr uint32_t AP_HEARTBEAT_MS = 5000U;
static constexpr uint32_t STA_HEARTBEAT_MS = 10000U;
static constexpr uint32_t RECONNECT_MS = 10000U;

static uint32_t lastStatusPollMs = 0U;
static uint32_t lastHeartbeatMs = 0U;
static uint32_t lastReconnectMs = 0U;

/* =========================================================
 * Network state
 * ========================================================= */

enum NetworkMode_t
{
    NETWORK_PROVISION_AP = 0,
    NETWORK_STA = 1
};

static NetworkMode_t networkMode = NETWORK_PROVISION_AP;

static String savedSSID;
static String savedPassword;

/* =========================================================
 * Objects
 * ========================================================= */

WebServer server(80);
Preferences preferences;

/* =========================================================
 * Provisioning page
 * ========================================================= */

static const char SETUP_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>

<head>

<meta charset="utf-8">

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>Smart Datalogger Setup</title>

<style>

* {
    box-sizing: border-box;
}

body {
    margin: 0;
    padding: 24px;
    background: #f4f5f7;
    font-family: Arial, sans-serif;
}

.card {
    max-width: 460px;
    margin: auto;
    background: white;
    padding: 24px;
    border-radius: 14px;
    border: 1px solid #ddd;
}

h1 {
    margin-top: 0;
}

p {
    color: #666;
}

label {
    display: block;
    margin-top: 16px;
    font-weight: bold;
}

input {
    width: 100%;
    padding: 12px;
    margin-top: 6px;
    border: 1px solid #bbb;
    border-radius: 8px;
    font-size: 16px;
}

button {
    width: 100%;
    margin-top: 20px;
    padding: 13px;
    border: 0;
    border-radius: 8px;
    background: #1565c0;
    color: white;
    font-size: 16px;
    font-weight: bold;
}

.info {
    margin-top: 20px;
    padding: 12px;
    background: #f5f5f5;
    border-radius: 8px;
    font-size: 13px;
}

</style>

</head>

<body>

<div class="card">

<h1>Smart Datalogger</h1>

<h3>Wi-Fi Setup</h3>

<p>
Enter the Wi-Fi network that ESP32 should connect to.
</p>

<form method="POST" action="/wifi/save">

<label>
Wi-Fi SSID
</label>

<input
    type="text"
    name="ssid"
    placeholder="Wi-Fi name"
    required>

<label>
Wi-Fi Password
</label>

<input
    type="password"
    name="password"
    placeholder="Wi-Fi password">

<button type="submit">
Save Wi-Fi
</button>

</form>

<div class="info">
After saving, ESP32 will restart and connect to the selected Wi-Fi.
</div>

</div>

</body>

</html>
)HTML";

/* =========================================================
 * Main control page
 * ========================================================= */

static const char CONTROL_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>

<head>

<meta charset="utf-8">

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>Smart Datalogger</title>

<style>

* {
    box-sizing: border-box;
}

body {
    margin: 0;
    padding: 20px;
    background: #f4f5f7;
    font-family: Arial, sans-serif;
}

.container {
    max-width: 520px;
    margin: auto;
}

.card {
    background: white;
    padding: 18px;
    border-radius: 12px;
    margin-bottom: 14px;
    border: 1px solid #ddd;
}

h1 {
    margin-top: 0;
}

.status {
    font-weight: bold;
}

.online {
    color: #188038;
}

.offline {
    color: #c62828;
}

.mode-grid {
    display: grid;
    grid-template-columns: repeat(3,1fr);
    gap: 8px;
    margin-top: 12px;
}

.relay-row {
    display: grid;
    grid-template-columns: 1fr auto auto;
    gap: 8px;
    align-items: center;
    padding: 12px 0;
    border-bottom: 1px solid #eee;
}

button {
    border: 0;
    padding: 11px 14px;
    border-radius: 8px;
    color: white;
    font-weight: bold;
    cursor: pointer;
}

.mode {
    background: #1565c0;
}

.on {
    background: #188038;
}

.off {
    background: #c62828;
}

.secondary {
    background: #555;
}

.warning {
    background: #e65100;
}

.actions {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    margin-top: 14px;
}

.small {
    color: #666;
    font-size: 13px;
    margin-top: 8px;
}

</style>

</head>

<body>

<div class="container">

<div class="card">

<h1>Smart Datalogger</h1>

<div id="status" class="status offline">
STM32 OFFLINE
</div>

<div id="info" class="small">
Waiting...
</div>

</div>

<div class="card">

<b>Mode</b>

<div class="mode-grid">

<button class="mode" onclick="setMode(0)">
MODE 0
</button>

<button class="mode" onclick="setMode(1)">
MODE 1
</button>

<button class="mode" onclick="setMode(2)">
MODE 2
</button>

<button class="mode" onclick="setMode(3)">
MODE 3
</button>

<button class="mode" onclick="setMode(4)">
MODE 4
</button>

</div>

</div>

<div class="card">

<b>Relay Control</b>

<div id="relayList"></div>

<div class="actions">

<button class="on" onclick="relay('ALL','ON')">
ALL ON
</button>

<button class="off" onclick="relay('ALL','OFF')">
ALL OFF
</button>

</div>

</div>

<div class="card">

<b>Network</b>

<div id="network" class="small">
Loading...
</div>

<button class="warning"
        style="margin-top:14px;width:100%"
        onclick="forgetWiFi()">

Forget WiFi

</button>

</div>

</div>

<script>

async function send(url)
{
    try
    {
        const r = await fetch(
            url,
            {
                method: "POST",
                cache: "no-store"
            }
        );

        return await r.json();
    }
    catch(e)
    {
        console.log(e);
    }
}

function setMode(mode)
{
    send(
        "/api/mode?mode=" + mode
    );

    setTimeout(
        refresh,
        250
    );
}

function relay(number, action)
{
    send(
        "/api/relay?relay=" +
        number +
        "&action=" +
        action
    );

    setTimeout(
        refresh,
        250
    );
}

function drawRelays(mask)
{
    let html = "";

    for(let i = 0; i < 4; i++)
    {
        let isOn =
            (mask & (1 << i)) !== 0;

        html += `
        <div class="relay-row">

            <div>
                <b>Relay ${i + 1}</b>
                <div class="small">
                    ${isOn ? "ON" : "OFF"}
                </div>
            </div>

            <button
                class="on"
                onclick="relay(${i + 1},'ON')">
                ON
            </button>

            <button
                class="off"
                onclick="relay(${i + 1},'OFF')">
                OFF
            </button>

        </div>
        `;
    }

    document.getElementById(
        "relayList"
    ).innerHTML = html;
}

async function refresh()
{
    try
    {
        const r =
            await fetch(
                "/api/status",
                {
                    cache: "no-store"
                }
            );

        const s =
            await r.json();

        const status =
            document.getElementById(
                "status"
            );

        status.textContent =
            s.online
            ? "STM32 ONLINE"
            : "STM32 OFFLINE";

        status.className =
            s.online
            ? "status online"
            : "status offline";

        document.getElementById(
            "info"
        ).textContent =
            "Mode: " +
            s.mode +
            " | Relay mask: 0x" +
            Number(s.relays)
                .toString(16)
                .toUpperCase();

        document.getElementById(
            "network"
        ).textContent =
            "SSID: " +
            s.ssid +
            " | IP: " +
            s.ip +
            " | RSSI: " +
            s.rssi +
            " dBm";

        drawRelays(
            s.relays
        );
    }
    catch(e)
    {
        console.log(e);
    }
}

async function forgetWiFi()
{
    if(!confirm(
        "Forget Wi-Fi configuration?"
    ))
    {
        return;
    }

    await fetch(
        "/wifi/reset",
        {
            method: "POST"
        }
    );

    document.body.innerHTML =
        "<h2>Wi-Fi erased. ESP32 restarting...</h2>";
}

refresh();

setInterval(
    refresh,
    1000
);

</script>

</body>

</html>
)HTML";

/* =========================================================
 * Preferences
 * ========================================================= */

static bool loadWiFiCredentials()
{
    preferences.begin("wifi", true);

    savedSSID =
        preferences.getString(
            "ssid",
            ""
        );

    savedPassword =
        preferences.getString(
            "password",
            ""
        );

    preferences.end();

    return savedSSID.length() > 0U;
}

static void saveWiFiCredentials(const String &ssid, const String &password)
{
    preferences.begin("wifi", false);

    preferences.putString(
        "ssid",
        ssid
    );

    preferences.putString(
        "password",
        password
    );

    preferences.end();
}

static void clearWiFiCredentials()
{
    preferences.begin("wifi", false);

    preferences.clear();

    preferences.end();

    savedSSID = "";
    savedPassword = "";
}

/* =========================================================
 * Provisioning AP
 * ========================================================= */

static void startProvisioningAP()
{
    networkMode =
        NETWORK_PROVISION_AP;

    WiFi.disconnect(
        true
    );

    delay(
        200U
    );

    WiFi.mode(
        WIFI_AP
    );

    bool ok =
        WiFi.softAP(
            SETUP_AP_SSID,
            SETUP_AP_PASSWORD
        );

    Serial.println();
    Serial.println("====================================");
    Serial.println(" WIFI PROVISIONING MODE");
    Serial.println("====================================");

    if (!ok)
    {
        Serial.println(
            "[AP] FAILED TO START"
        );

        return;
    }

    Serial.print(
        "SSID     : "
    );

    Serial.println(
        SETUP_AP_SSID
    );

    Serial.print(
        "PASSWORD : "
    );

    Serial.println(
        SETUP_AP_PASSWORD
    );

    Serial.print(
        "IP       : "
    );

    Serial.println(
        WiFi.softAPIP()
    );

    Serial.print(
        "WEB      : http://"
    );

    Serial.print(
        WiFi.softAPIP()
    );

    Serial.println(
        "/"
    );

    Serial.println(
        "===================================="
    );
}

/* =========================================================
 * STA connect
 * ========================================================= */

static bool connectToSavedWiFi()
{
    if (savedSSID.length() == 0U)
    {
        return false;
    }

    networkMode =
        NETWORK_STA;

    WiFi.mode(
        WIFI_STA
    );

    WiFi.setAutoReconnect(
        true
    );

    WiFi.setSleep(
        false
    );

    Serial.println();
    Serial.println("====================================");
    Serial.println(" CONNECTING TO SAVED WIFI");
    Serial.println("====================================");

    Serial.print(
        "SSID : "
    );

    Serial.println(
        savedSSID
    );

    WiFi.begin(
        savedSSID.c_str(),
        savedPassword.c_str()
    );

    uint32_t startMs =
        millis();

    Serial.print(
        "[WIFI] Connecting"
    );

    while (WiFi.status() != WL_CONNECTED)
    {
        STM32Link_Task();

        Serial.print(
            "."
        );

        delay(
            500U
        );

        if ((uint32_t)(
                millis() -
                startMs)
            >= STA_CONNECT_TIMEOUT_MS)
        {
            Serial.println();

            Serial.println(
                "[WIFI] CONNECTION TIMEOUT"
            );

            return false;
        }
    }

    Serial.println();
    Serial.println();
    Serial.println(
        "[WIFI] CONNECTED"
    );

    Serial.print(
        "SSID     : "
    );

    Serial.println(
        WiFi.SSID()
    );

    Serial.print(
        "IP       : "
    );

    Serial.println(
        WiFi.localIP()
    );

    Serial.print(
        "RSSI     : "
    );

    Serial.print(
        WiFi.RSSI()
    );

    Serial.println(
        " dBm"
    );

    Serial.print(
        "WEB      : http://"
    );

    Serial.print(
        WiFi.localIP()
    );

    Serial.println(
        "/"
    );

    return true;
}

/* =========================================================
 * HTTP root
 * ========================================================= */

static void handleRoot()
{
    if (networkMode == NETWORK_PROVISION_AP)
    {
        server.send_P(
            200,
            "text/html",
            SETUP_PAGE
        );

        return;
    }

    server.send_P(
        200,
        "text/html",
        CONTROL_PAGE
    );
}

/* =========================================================
 * Save Wi-Fi
 * ========================================================= */

static void handleWiFiSave()
{
    if (!server.hasArg("ssid"))
    {
        server.send(
            400,
            "text/plain",
            "SSID missing"
        );

        return;
    }

    String ssid =
        server.arg("ssid");

    String password =
        server.arg("password");

    ssid.trim();

    if (ssid.length() == 0U)
    {
        server.send(
            400,
            "text/plain",
            "SSID invalid"
        );

        return;
    }

    saveWiFiCredentials(
        ssid,
        password
    );

    Serial.println();
    Serial.println(
        "[WIFI] NEW CONFIG SAVED"
    );

    Serial.print(
        "[WIFI] SSID: "
    );

    Serial.println(
        ssid
    );

    /*
     * Do NOT print password.
     */

    server.send(
        200,
        "text/html",
        R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
</head>
<body style="font-family:Arial;text-align:center;padding:40px">
<h2>Wi-Fi Saved</h2>
<p>ESP32 is restarting...</p>
<p>Connect your phone/computer back to your normal Wi-Fi.</p>
</body>
</html>
)HTML"
    );

    delay(
        1500U
    );

    ESP.restart();
}

/* =========================================================
 * Wi-Fi reset
 * ========================================================= */

static void handleWiFiReset()
{
    Serial.println();
    Serial.println(
        "[WIFI] CLEARING SAVED CONFIG"
    );

    clearWiFiCredentials();

    server.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );

    delay(
        1000U
    );

    ESP.restart();
}

/* =========================================================
 * API status
 * ========================================================= */

static void handleStatus()
{
    const SystemState &state =
        STM32Link_GetState();

    String json =
        "{";

    json +=
        "\"online\":";

    json +=
        state.stm32Online
        ? "true"
        : "false";

    json +=
        ",\"mode\":";

    json +=
        String(
            state.mode
        );

    json +=
        ",\"relays\":";

    json +=
        String(
            state.relayMask
        );

    json +=
        ",\"ssid\":\"";

    json +=
        WiFi.SSID();

    json +=
        "\"";

    json +=
        ",\"ip\":\"";

    json +=
        WiFi.localIP().toString();

    json +=
        "\"";

    json +=
        ",\"rssi\":";

    json +=
        String(
            WiFi.RSSI()
        );

    json +=
        "}";

    server.send(
        200,
        "application/json",
        json
    );
}

/* =========================================================
 * API mode
 * ========================================================= */

static void handleMode()
{
    if (!server.hasArg("mode"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"mode missing\"}"
        );

        return;
    }

    int mode =
        server.arg("mode").toInt();

    if (mode < 0 ||
        mode > 4)
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"invalid mode\"}"
        );

        return;
    }

    String command =
        "MODE,";

    command +=
        String(
            mode
        );

    Serial.print(
        "[STM32 TX] "
    );

    Serial.println(
        command
    );

    STM32Link_SendCommand(
        command.c_str()
    );

    server.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

/* =========================================================
 * API relay
 * ========================================================= */

static void handleRelay()
{
    if (!server.hasArg("relay") ||
        !server.hasArg("action"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"relay/action missing\"}"
        );

        return;
    }

    String relay =
        server.arg("relay");

    String action =
        server.arg("action");

    bool validRelay =
        relay == "ALL";

    if (!validRelay)
    {
        int relayIndex =
            relay.toInt();

        validRelay =
            relayIndex >= 1 &&
            relayIndex <= 4;
    }

    if (!validRelay ||
        (action != "ON" &&
         action != "OFF"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"invalid relay command\"}"
        );

        return;
    }

    String command =
        "RELAY,";

    command +=
        relay;

    command +=
        ",";

    command +=
        action;

    Serial.print(
        "[STM32 TX] "
    );

    Serial.println(
        command
    );

    STM32Link_SendCommand(
        command.c_str()
    );

    server.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

/* =========================================================
 * HTTP setup
 * ========================================================= */

static void setupHttpServer()
{
    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    server.on(
        "/wifi/save",
        HTTP_POST,
        handleWiFiSave
    );

    server.on(
        "/wifi/reset",
        HTTP_POST,
        handleWiFiReset
    );

    server.on(
        "/api/status",
        HTTP_GET,
        handleStatus
    );

    server.on(
        "/api/mode",
        HTTP_POST,
        handleMode
    );

    server.on(
        "/api/relay",
        HTTP_POST,
        handleRelay
    );

    server.on(
        "/ping",
        HTTP_GET,
        []()
        {
            server.send(
                200,
                "text/plain",
                "ESP32 HTTP SERVER OK"
            );
        }
    );

    server.onNotFound(
        []()
        {
            server.send(
                404,
                "text/plain",
                "404 Not Found"
            );
        }
    );

    server.begin();

    Serial.println(
        "[HTTP] Server started"
    );
}

/* =========================================================
 * AP heartbeat
 * ========================================================= */

static void printAPHeartbeat()
{
    Serial.println();
    Serial.println(
        "======= WIFI SETUP ======="
    );

    Serial.println(
        "MODE    : PROVISION AP"
    );

    Serial.print(
        "SSID    : "
    );

    Serial.println(
        SETUP_AP_SSID
    );

    Serial.print(
        "Clients : "
    );

    Serial.println(
        WiFi.softAPgetStationNum()
    );

    Serial.print(
        "WEB     : http://"
    );

    Serial.print(
        WiFi.softAPIP()
    );

    Serial.println(
        "/"
    );

    Serial.println(
        "Waiting for Wi-Fi configuration..."
    );

    Serial.println(
        "=========================="
    );
}

/* =========================================================
 * STA heartbeat
 * ========================================================= */

static void printSTAHeartbeat()
{
    const SystemState &state =
        STM32Link_GetState();

    Serial.println();
    Serial.println(
        "========= HEARTBEAT ========="
    );

    Serial.println(
        "NETWORK : STA"
    );

    Serial.print(
        "SSID    : "
    );

    Serial.println(
        WiFi.SSID()
    );

    Serial.print(
        "IP      : "
    );

    Serial.println(
        WiFi.localIP()
    );

    Serial.print(
        "RSSI    : "
    );

    Serial.print(
        WiFi.RSSI()
    );

    Serial.println(
        " dBm"
    );

    Serial.print(
        "WEB     : http://"
    );

    Serial.print(
        WiFi.localIP()
    );

    Serial.println(
        "/"
    );

    Serial.print(
        "STM32   : "
    );

    Serial.println(
        state.stm32Online
        ? "ONLINE"
        : "OFFLINE"
    );

    Serial.print(
        "MODE    : "
    );

    Serial.println(
        state.mode
    );

    Serial.print(
        "RELAY   : 0x"
    );

    Serial.println(
        state.relayMask,
        HEX
    );

    Serial.println(
        "============================="
    );
}

/* =========================================================
 * Setup
 * ========================================================= */

void setup()
{
    Serial.begin(
        115200
    );

    delay(
        1000U
    );

    Serial.println();
    Serial.println(
        "===================================="
    );

    Serial.println(
        " SMART DATALOGGER BOOT"
    );

    Serial.println(
        "===================================="
    );

    /*
     * STM32 UART.
     */
    STM32Link_Init();

    /*
     * Read saved Wi-Fi.
     */
    bool credentialsExist =
        loadWiFiCredentials();

    if (!credentialsExist)
    {
        Serial.println(
            "[WIFI] No saved credentials"
        );

        startProvisioningAP();
    }
    else
    {
        Serial.println(
            "[WIFI] Saved credentials found"
        );

        if (!connectToSavedWiFi())
        {
            Serial.println(
                "[WIFI] Saved network unavailable"
            );

            Serial.println(
                "[WIFI] Falling back to setup AP"
            );

            startProvisioningAP();
        }
    }

    /*
     * Start HTTP server after network is ready.
     */
    setupHttpServer();

    lastHeartbeatMs =
        millis();

    if (networkMode == NETWORK_STA)
    {
        printSTAHeartbeat();
    }
    else
    {
        printAPHeartbeat();
    }
}

/* =========================================================
 * Main Loop
 * ========================================================= */

void loop()
{
    uint32_t now =
        millis();

    /*
     * HTTP.
     */
    server.handleClient();

    /*
     * STM32 UART.
     */
    STM32Link_Task();

    STM32Link_UpdateConnectionState(
        now
    );

    /* =====================================================
     * Provisioning mode
     * ===================================================== */

    if (networkMode == NETWORK_PROVISION_AP)
    {
        if ((uint32_t)(
                now -
                lastHeartbeatMs)
            >= AP_HEARTBEAT_MS)
        {
            lastHeartbeatMs =
                now;

            printAPHeartbeat();
        }

        return;
    }

    /* =====================================================
     * STA mode
     * ===================================================== */

    if (WiFi.status() == WL_CONNECTED)
    {
        /*
         * STM32 status polling.
         */
        if ((uint32_t)(
                now -
                lastStatusPollMs)
            >= STATUS_POLL_MS)
        {
            lastStatusPollMs =
                now;

            STM32Link_SendCommand(
                "STATUS,MODE"
            );

            STM32Link_SendCommand(
                "STATUS,IO"
            );
        }

        /*
         * Network heartbeat.
         */
        if ((uint32_t)(
                now -
                lastHeartbeatMs)
            >= STA_HEARTBEAT_MS)
        {
            lastHeartbeatMs =
                now;

            printSTAHeartbeat();
        }

        return;
    }

    /* =====================================================
     * STA disconnected
     * ===================================================== */

    if ((uint32_t)(
            now -
            lastReconnectMs)
        >= RECONNECT_MS)
    {
        lastReconnectMs =
            now;

        Serial.println();
        Serial.print(
            "[WIFI] LOST CONNECTION - reconnecting to "
        );

        Serial.println(
            savedSSID
        );

        WiFi.disconnect();

        delay(
            100U
        );

        WiFi.begin(
            savedSSID.c_str(),
            savedPassword.c_str()
        );
    }
}
