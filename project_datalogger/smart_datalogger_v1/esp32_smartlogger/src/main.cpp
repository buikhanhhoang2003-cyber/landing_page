#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>

#include "stm32_link.h"
#include "line_uplink.h"

/* =========================================================
 * Wi-Fi
 * ========================================================= */

static const char *SETUP_AP_SSID = "SmartDatalogger-Setup";
static const char *SETUP_AP_PASSWORD = "12345678";

static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000U;
static constexpr uint32_t SAVED_WIFI_RETRY_MS = 60000U;
static constexpr uint32_t STATUS_POLL_MS = 1000U;
static constexpr uint32_t THERMAL_REQUEST_MS = 400U;
static constexpr uint32_t THERMAL_WEB_ACTIVE_MS = 2500U;
static constexpr uint32_t AP_HEARTBEAT_MS = 5000U;
static constexpr uint32_t STA_HEARTBEAT_MS = 10000U;
static constexpr uint32_t AP_RETRY_MS = 1000U;
static constexpr uint32_t MODE_TRANSITION_RETRY_MS = 180U;
static constexpr uint32_t MODE_TRANSITION_TIMEOUT_MS = 5000U;

enum NetworkMode_t
{
    NETWORK_PROVISION_AP = 0,
    NETWORK_STA = 1
};

enum NetworkState_t
{
    NET_INIT = 0,
    NET_STA_CONNECTING,
    NET_STA_CONNECTED,
    NET_AP_ACTIVE,
    NET_AP_RETRY
};

static NetworkMode_t networkMode = NETWORK_PROVISION_AP;
static NetworkState_t networkState = NET_INIT;

static String savedSSID;
static String savedPassword;

static bool webIpSent = false;

static uint32_t lastStatusPollMs = 0U;
static uint32_t lastThermalRequestMs = 0U;
static uint32_t lastThermalWebAccessMs = 0U;
static uint32_t lastHeartbeatMs = 0U;
static uint32_t networkStateStartedMs = 0U;
static uint32_t lastApRetryMs = 0U;
static uint32_t wifiReconnectCount = 0U;
static bool httpServerRunning = false;
static bool httpRoutesConfigured = false;
static bool modeTransitionActive = false;
static uint8_t requestedMode = 0U;
static uint8_t modeTransitionRetryCount = 0U;
static uint32_t modeTransitionStartMs = 0U;
static uint32_t lastModeTransitionTxMs = 0U;

/* =========================================================
 * Objects
 * ========================================================= */

WebServer server(80);
Preferences preferences;

/* =========================================================
 * Wi-Fi setup page
 * ========================================================= */

static const char SETUP_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Datalogger Setup</title>
<style>
*{box-sizing:border-box}
body{margin:0;padding:24px;background:#f2f4f7;font-family:Arial,sans-serif;color:#111}
.card{max-width:460px;margin:auto;background:white;padding:24px;border:1px solid #ddd;border-radius:14px}
h1{margin-top:0}
label{display:block;margin-top:16px;font-weight:bold}
input{width:100%;padding:12px;margin-top:6px;border:1px solid #bbb;border-radius:8px;font-size:16px}
button{width:100%;padding:13px;margin-top:20px;border:0;border-radius:8px;background:#1565c0;color:white;font-size:16px;font-weight:bold}
.info{margin-top:18px;padding:12px;background:#f5f5f5;border-radius:8px;color:#555;font-size:13px}
</style>
</head>
<body>
<div class="card">
<h1>Smart Datalogger</h1>
<h3>Wi-Fi Setup</h3>
<form method="POST" action="/wifi/save">
<label>Wi-Fi SSID</label>
<input name="ssid" type="text" required>
<label>Wi-Fi Password</label>
<input name="password" type="password">
<button type="submit">Save Wi-Fi</button>
</form>
<div class="info">After saving, the ESP32 restarts and connects to the Wi-Fi router.</div>
</div>
</body>
</html>
)HTML";

/* =========================================================
 * Dashboard page
 *
 * IMPORTANT:
 * All visible UI is static HTML.
 * Even if JavaScript/API/STM32 fails, the page itself still renders.
 * ========================================================= */

static const char CONTROL_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Datalogger</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#eef1f5;color:#17202a;font-family:Arial,sans-serif}
main{max-width:1180px;margin:auto;padding:16px}
h1{margin:0 0 4px;font-size:26px}
h2{margin:0;font-size:17px}
.sub{color:#67717d;font-size:13px;margin-bottom:15px}
.panel{background:white;border:1px solid #d7dde5;border-radius:12px;padding:15px;margin-bottom:12px}
.title{display:flex;justify-content:space-between;gap:10px;align-items:center;margin-bottom:12px}
.hint{color:#73808e;font-size:12px}
.statusRow{display:flex;flex-wrap:wrap;gap:8px}
.badge{padding:8px 11px;background:#f4f6f8;border:1px solid #dce1e7;border-radius:999px;font-size:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(155px,1fr));gap:9px}
.card{background:#f7f9fb;border:1px solid #dce2e8;border-radius:10px;padding:13px}
.label{font-size:11px;color:#6f7c89;text-transform:uppercase}
.value{font-size:22px;font-weight:bold;margin-top:7px}
.meta{font-size:11px;color:#75808b;margin-top:5px}
.ok{color:#14833b!important}
.bad{color:#c62828!important}
.modeGrid{display:grid;grid-template-columns:repeat(5,1fr);gap:7px}
button{border:0;border-radius:8px;padding:10px 11px;color:white;background:#455a64;font-weight:bold;cursor:pointer}
button.active{background:#1565c0}
button.green{background:#2e7d32}
button.red{background:#c62828}
button.blue{background:#1565c0}
button.orange{background:#e56b00}
.relayGrid{display:grid;grid-template-columns:repeat(4,1fr);gap:9px}
.relayButtons{display:grid;grid-template-columns:1fr 1fr;gap:5px;margin-top:9px}
.diGrid{display:grid;grid-template-columns:repeat(4,1fr);gap:9px}
.center{text-align:center}
.camControls{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
#thermalArea{display:none;margin-top:12px;grid-template-columns:minmax(300px,2fr) minmax(190px,1fr);gap:12px}
#thermalArea.show{display:grid}
.canvasBox{background:#111;border-radius:10px;overflow:hidden;min-height:260px;display:flex;align-items:center;justify-content:center}
#thermalCanvas{width:100%;max-width:700px;aspect-ratio:4/3;image-rendering:pixelated;background:black}
#errorBox{display:none;background:#fff3cd;border:1px solid #ffe69c;color:#664d03;padding:10px;border-radius:8px;margin-bottom:12px;font-size:13px}
.timerGrid{display:grid;grid-template-columns:repeat(2,1fr);gap:9px}
.timerCard{background:#f7f9fb;border:1px solid #dce2e8;border-radius:10px;padding:13px}
.timerInputs{display:grid;grid-template-columns:repeat(3,1fr) 1.15fr;gap:6px;margin-top:10px}
.timerInputs input,.timerInputs select{width:100%;padding:9px 7px;border:1px solid #cbd3dc;border-radius:7px;background:white;color:#17202a}
.timerState{font-size:14px;font-weight:bold;margin-top:8px}
.timerActions{display:flex;gap:7px;flex-wrap:wrap;margin-top:9px}
.timerNote{font-size:12px;color:#75808b;margin-top:9px}
@media(max-width:780px){
  .modeGrid{grid-template-columns:repeat(2,1fr)}
  .relayGrid,.timerGrid{grid-template-columns:1fr}
  #thermalArea.show{grid-template-columns:1fr}
}
@media(max-width:460px){
  main{padding:10px}
  .modeGrid{grid-template-columns:1fr}
  .relayGrid,.diGrid{grid-template-columns:repeat(2,1fr)}
  .timerGrid{grid-template-columns:1fr}
  .timerInputs{grid-template-columns:repeat(2,1fr)}
}
</style>
</head>

<body>
<main>

<h1>Smart Datalogger</h1>
<div class="sub">Sensors • IO • Relay • Mode • MLX90640 Thermal Camera • <b>UI V5 TIMER</b></div>

<div id="errorBox"></div>

<section class="panel">
  <div class="statusRow">
    <div class="badge">STM32: <b id="stm32">OFFLINE</b></div>
    <div class="badge">Mode: <b id="modeText">--</b></div>
    <div class="badge">Wi-Fi: <b id="wifiText">--</b></div>
    <div class="badge">IP: <b id="ipText">--</b></div>
    <div class="badge">RSSI: <b id="rssiText">--</b></div>
  </div>
</section>

<section class="panel">
  <div class="title">
    <h2>Mode</h2>
    <span class="hint">Switch STM32 mode</span>
  </div>
  <div class="modeGrid">
    <button id="m0" onclick="setMode(0)">0 MENU</button>
    <button id="m1" onclick="setMode(1)">1 SENSOR</button>
    <button id="m2" onclick="setMode(2)">2 THERMAL</button>
    <button id="m3" onclick="setMode(3)">3 OUTPUT</button>
    <button id="m4" onclick="setMode(4)">4 TIMER</button>
  </div>
</section>

<section class="panel">
  <div class="title">
    <h2>Sensor Data</h2>
    <span class="hint">STATUS,SENSOR</span>
  </div>
  <div class="grid">
    <div class="card">
      <div class="label">Temperature</div>
      <div class="value" id="temp">--</div>
      <div class="meta" id="shtStatus">SHT30 --</div>
    </div>
    <div class="card">
      <div class="label">Humidity</div>
      <div class="value" id="hum">--</div>
      <div class="meta">SHT30</div>
    </div>
    <div class="card">
      <div class="label">Pressure</div>
      <div class="value" id="pressure">--</div>
      <div class="meta" id="bmeStatus">BME280 --</div>
    </div>
    <div class="card">
      <div class="label">Light</div>
      <div class="value" id="light">--</div>
      <div class="meta" id="lightStatus">SEN0390 --</div>
    </div>
    <div class="card">
      <div class="label">Noise</div>
      <div class="value" id="noise">--</div>
      <div class="meta" id="noiseStatus">SM7901B --</div>
    </div>
  </div>
</section>

<section class="panel">
  <div class="title">
    <h2>IO Status</h2>
    <span class="hint">STATUS,IO</span>
  </div>

  <div class="grid" style="margin-bottom:9px">
    <div class="card">
      <div class="label">ADC_1</div>
      <div class="value" id="adc1">--</div>
      <div class="meta">mV</div>
    </div>
    <div class="card">
      <div class="label">ADC_2</div>
      <div class="value" id="adc2">--</div>
      <div class="meta">mV</div>
    </div>
    <div class="card">
      <div class="label">DAC Output</div>
      <div class="value" id="dacMv">--</div>
      <div class="meta" id="dacMeta">--</div>
    </div>
  </div>

  <div class="diGrid">
    <div class="card center"><div class="label">DI1</div><div class="value" id="di1">--</div></div>
    <div class="card center"><div class="label">DI2</div><div class="value" id="di2">--</div></div>
    <div class="card center"><div class="label">DI3</div><div class="value" id="di3">--</div></div>
    <div class="card center"><div class="label">DI4</div><div class="value" id="di4">--</div></div>
  </div>
</section>

<section class="panel">
  <div class="title">
    <h2>Relay Control</h2>
    <span class="hint">RELAY,n,ON/OFF</span>
  </div>

  <div class="relayGrid">
    <div class="card">
      <div class="label">Relay 1</div>
      <div class="value" id="r1state">--</div>
      <div class="relayButtons">
        <button class="green" onclick="setRelay(1,'ON')">ON</button>
        <button class="red" onclick="setRelay(1,'OFF')">OFF</button>
      </div>
    </div>

    <div class="card">
      <div class="label">Relay 2</div>
      <div class="value" id="r2state">--</div>
      <div class="relayButtons">
        <button class="green" onclick="setRelay(2,'ON')">ON</button>
        <button class="red" onclick="setRelay(2,'OFF')">OFF</button>
      </div>
    </div>

    <div class="card">
      <div class="label">Relay 3</div>
      <div class="value" id="r3state">--</div>
      <div class="relayButtons">
        <button class="green" onclick="setRelay(3,'ON')">ON</button>
        <button class="red" onclick="setRelay(3,'OFF')">OFF</button>
      </div>
    </div>

    <div class="card">
      <div class="label">Relay 4</div>
      <div class="value" id="r4state">--</div>
      <div class="relayButtons">
        <button class="green" onclick="setRelay(4,'ON')">ON</button>
        <button class="red" onclick="setRelay(4,'OFF')">OFF</button>
      </div>
    </div>
  </div>

  <div style="margin-top:10px">
    <button class="green" onclick="setRelay('ALL','ON')">ALL ON</button>
    <button class="red" onclick="setRelay('ALL','OFF')">ALL OFF</button>
  </div>
</section>


<section class="panel">
  <div class="title">
    <h2>Relay Timer</h2>
    <span class="hint">RELAY TIMER n seconds ON/OFF • up to 24 hours</span>
  </div>

  <div class="timerGrid">
    <div class="timerCard">
      <div class="label">Relay 1 Timer</div>
      <div id="timer1State" class="timerState">IDLE</div>
      <div class="timerInputs">
        <input id="t1h" type="number" min="0" max="24" value="0" placeholder="Hours">
        <input id="t1m" type="number" min="0" max="59" value="0" placeholder="Minutes">
        <input id="t1s" type="number" min="0" max="59" value="10" placeholder="Seconds">
        <select id="t1action"><option value="ON">ON</option><option value="OFF">OFF</option></select>
      </div>
      <div class="timerActions"><button class="blue" onclick="armTimer(1)">ARM RELAY 1</button></div>
    </div>

    <div class="timerCard">
      <div class="label">Relay 2 Timer</div>
      <div id="timer2State" class="timerState">IDLE</div>
      <div class="timerInputs">
        <input id="t2h" type="number" min="0" max="24" value="0" placeholder="Hours">
        <input id="t2m" type="number" min="0" max="59" value="0" placeholder="Minutes">
        <input id="t2s" type="number" min="0" max="59" value="10" placeholder="Seconds">
        <select id="t2action"><option value="ON">ON</option><option value="OFF">OFF</option></select>
      </div>
      <div class="timerActions"><button class="blue" onclick="armTimer(2)">ARM RELAY 2</button></div>
    </div>

    <div class="timerCard">
      <div class="label">Relay 3 Timer</div>
      <div id="timer3State" class="timerState">IDLE</div>
      <div class="timerInputs">
        <input id="t3h" type="number" min="0" max="24" value="0" placeholder="Hours">
        <input id="t3m" type="number" min="0" max="59" value="0" placeholder="Minutes">
        <input id="t3s" type="number" min="0" max="59" value="10" placeholder="Seconds">
        <select id="t3action"><option value="ON">ON</option><option value="OFF">OFF</option></select>
      </div>
      <div class="timerActions"><button class="blue" onclick="armTimer(3)">ARM RELAY 3</button></div>
    </div>

    <div class="timerCard">
      <div class="label">Relay 4 Timer</div>
      <div id="timer4State" class="timerState">IDLE</div>
      <div class="timerInputs">
        <input id="t4h" type="number" min="0" max="24" value="0" placeholder="Hours">
        <input id="t4m" type="number" min="0" max="59" value="0" placeholder="Minutes">
        <input id="t4s" type="number" min="0" max="59" value="10" placeholder="Seconds">
        <select id="t4action"><option value="ON">ON</option><option value="OFF">OFF</option></select>
      </div>
      <div class="timerActions"><button class="blue" onclick="armTimer(4)">ARM RELAY 4</button></div>
    </div>
  </div>

  <div class="timerActions" style="margin-top:12px">
    <button class="red" onclick="resetTimers()">RESET ALL TIMERS</button>
    <button onclick="setMode(4)">OPEN MODE 4 TIMER</button>
  </div>
  <div class="timerNote">
    Arming the same relay again replaces the old timer. RESET only cancels the pending timer and does not change the current relay state.
    The timer continues running when switching to another mode.
  </div>
</section>

<section class="panel">
  <div class="title">
    <h2>MLX90640 Thermal Camera</h2>
    <div class="camControls">
      <button id="camBtn" class="blue" onclick="toggleCamera()">TURN ON THERMAL CAMERA</button>
      <span class="hint" id="thermalMsg">Camera is off.</span>
    </div>
  </div>

  <div id="thermalArea">
    <div class="canvasBox">
      <canvas id="thermalCanvas" width="320" height="240"></canvas>
    </div>

    <div class="grid">
      <div class="card"><div class="label">Ambient</div><div class="value" id="tamb">--</div></div>
      <div class="card"><div class="label">Center</div><div class="value" id="tcenter">--</div></div>
      <div class="card"><div class="label">Minimum</div><div class="value" id="tmin">--</div></div>
      <div class="card"><div class="label">Maximum</div><div class="value" id="tmax">--</div></div>
      <div class="card"><div class="label">Frame</div><div class="value" id="tframe">--</div></div>
      <div class="card"><div class="label">CRC Errors</div><div class="value" id="tcrc">--</div></div>
    </div>
  </div>
</section>

<section class="panel">
  <button class="orange" onclick="forgetWiFi()">Forget Wi-Fi</button>
</section>

</main>

<script>
"use strict";

let currentMode = 0;
let cameraEnabled = false;
let thermalFetchBusy = false;

const canvas = document.getElementById("thermalCanvas");
const ctx = canvas.getContext("2d");

const pixelCanvas = document.createElement("canvas");
pixelCanvas.width = 32;
pixelCanvas.height = 24;
const pixelCtx = pixelCanvas.getContext("2d");

window.onerror = function(message, source, line)
{
    showError("JS ERROR: " + message + " line " + line);
};

function showError(message)
{
    const box = document.getElementById("errorBox");
    box.style.display = "block";
    box.textContent = message;
}

function clearError()
{
    const box = document.getElementById("errorBox");
    box.style.display = "none";
    box.textContent = "";
}

function isValid(mask, bit)
{
    return (Number(mask) & (1 << bit)) !== 0;
}

function value10(v)
{
    return (Number(v) / 10.0).toFixed(1);
}

function updateHealth(id, online, name)
{
    const e = document.getElementById(id);
    e.textContent = name + " " + (online ? "ONLINE" : "ERROR");
    e.className = "meta " + (online ? "ok" : "bad");
}

function updateDI(id, on)
{
    const e = document.getElementById(id);
    e.textContent = on ? "ON" : "OFF";
    e.className = "value " + (on ? "ok" : "bad");
}

function updateRelay(id, on)
{
    const e = document.getElementById(id);
    e.textContent = on ? "ON" : "OFF";
    e.className = "value " + (on ? "ok" : "bad");
}

function updateModeButtons(mode)
{
    for (let i = 0; i <= 4; i++)
    {
        const e = document.getElementById("m" + i);
        e.classList.toggle("active", i === Number(mode));
    }
}

async function apiPost(url)
{
    const response = await fetch(url,
    {
        method: "POST",
        cache: "no-store"
    });

    const text = await response.text();

    if (!response.ok)
    {
        throw new Error(text);
    }

    return text;
}

async function setMode(mode)
{
    try
    {
        clearError();
        await apiPost("/api/mode?mode=" + mode);
        currentMode = Number(mode);
        updateModeButtons(currentMode);

        if (currentMode !== 2 && cameraEnabled)
        {
            stopCamera("Left MODE 2. Camera stopped.");
        }

        setTimeout(refreshStatus, 200);
    }
    catch (error)
    {
        showError("MODE ERROR: " + error.message);
    }
}

async function setRelay(relay, action)
{
    try
    {
        clearError();
        await apiPost("/api/relay?relay=" + relay + "&action=" + action);
        setTimeout(refreshStatus, 150);
    }
    catch (error)
    {
        showError("RELAY ERROR: " + error.message);
    }
}


function formatTimerSeconds(totalSeconds)
{
    let total = Math.max(0, Number(totalSeconds) || 0);
    const hours = Math.floor(total / 3600);
    total %= 3600;
    const minutes = Math.floor(total / 60);
    const seconds = total % 60;

    return String(hours).padStart(2, "0") + ":" +
           String(minutes).padStart(2, "0") + ":" +
           String(seconds).padStart(2, "0");
}

function updateTimerSlot(relayNumber, timer)
{
    const e = document.getElementById("timer" + relayNumber + "State");

    if (!timer || !timer.active)
    {
        e.textContent = "IDLE";
        e.className = "timerState";
        return;
    }

    e.textContent =
        "ARMED • " + timer.action +
        " sau " + formatTimerSeconds(timer.remaining);

    e.className = "timerState ok";
}

async function armTimer(relayNumber)
{
    try
    {
        clearError();

        const h = Math.max(0, Number(document.getElementById("t" + relayNumber + "h").value) || 0);
        const min = Math.max(0, Number(document.getElementById("t" + relayNumber + "m").value) || 0);
        const sec = Math.max(0, Number(document.getElementById("t" + relayNumber + "s").value) || 0);
        const action = document.getElementById("t" + relayNumber + "action").value;

        if (h > 24 || min > 59 || sec > 59)
        {
            throw new Error("Invalid time");
        }

        const totalSeconds = Math.floor(h * 3600 + min * 60 + sec);

        if (totalSeconds < 1 || totalSeconds > 86400)
        {
            throw new Error("Timer must be from 1 second to 24 hours");
        }

        await apiPost(
            "/api/timer?relay=" + relayNumber +
            "&seconds=" + totalSeconds +
            "&action=" + action);

        document.getElementById("timer" + relayNumber + "State").textContent =
            "ARMED • " + action + " sau " + formatTimerSeconds(totalSeconds);

        setTimeout(refreshStatus, 150);
    }
    catch (error)
    {
        showError("TIMER ERROR: " + error.message);
    }
}

async function resetTimers()
{
    if (!confirm("Cancel all pending relay timers?"))
    {
        return;
    }

    try
    {
        clearError();
        await apiPost("/api/timer/reset");
        for (let i = 1; i <= 4; i++)
        {
            updateTimerSlot(i, null);
        }
        setTimeout(refreshStatus, 150);
    }
    catch (error)
    {
        showError("RESET TIMER ERROR: " + error.message);
    }
}

async function refreshStatus()
{
    try
    {
        const response = await fetch("/api/status?_=" + Date.now(),
        {
            cache: "no-store"
        });

        if (!response.ok)
        {
            throw new Error("HTTP " + response.status);
        }

        const s = await response.json();

        currentMode = Number(s.mode);
        updateModeButtons(currentMode);

        const stm = document.getElementById("stm32");
        stm.textContent = s.online ? "ONLINE" : "OFFLINE";
        stm.className = s.online ? "ok" : "bad";

        document.getElementById("modeText").textContent = s.mode + " " + s.modeName;
        document.getElementById("wifiText").textContent = s.ssid || "--";
        document.getElementById("ipText").textContent = s.ip || "--";
        document.getElementById("rssiText").textContent = s.rssi + " dBm";

        document.getElementById("temp").textContent =
            isValid(s.validMask, 0) ? value10(s.temperature10) + " °C" : "ERR";

        document.getElementById("hum").textContent =
            isValid(s.validMask, 0) ? value10(s.humidity10) + " %RH" : "ERR";

        document.getElementById("pressure").textContent =
            isValid(s.validMask, 1) ? value10(s.pressure10) + " hPa" : "ERR";

        document.getElementById("light").textContent =
            isValid(s.validMask, 2) ? s.lightLux + " lux" : "ERR";

        document.getElementById("noise").textContent =
            isValid(s.validMask, 3) ? value10(s.noise10) + " dB" : "ERR";

        updateHealth("shtStatus", isValid(s.validMask, 0), "SHT30");
        updateHealth("bmeStatus", isValid(s.validMask, 1), "BME280");
        updateHealth("lightStatus", isValid(s.validMask, 2), "SEN0390");
        updateHealth("noiseStatus", isValid(s.validMask, 3), "SM7901B");

        document.getElementById("adc1").textContent = s.adc1Mv;
        document.getElementById("adc2").textContent = s.adc2Mv;
        document.getElementById("dacMv").textContent = s.dacMv + " mV";
        document.getElementById("dacMeta").textContent =
            "Code " + s.dacCode + " | " + (s.dacRunning ? "RUNNING" : "STOPPED");

        updateDI("di1", (Number(s.diMask) & 0x01) !== 0);
        updateDI("di2", (Number(s.diMask) & 0x02) !== 0);
        updateDI("di3", (Number(s.diMask) & 0x04) !== 0);
        updateDI("di4", (Number(s.diMask) & 0x08) !== 0);

        updateRelay("r1state", (Number(s.relays) & 0x01) !== 0);
        updateRelay("r2state", (Number(s.relays) & 0x02) !== 0);
        updateRelay("r3state", (Number(s.relays) & 0x04) !== 0);
        updateRelay("r4state", (Number(s.relays) & 0x08) !== 0);

        if (Array.isArray(s.timers))
        {
            for (let i = 0; i < 4; i++)
            {
                updateTimerSlot(i + 1, s.timers[i]);
            }
        }

        document.getElementById("tamb").textContent = value10(s.thermalAmbient10) + " °C";
        document.getElementById("tcenter").textContent = value10(s.thermalCenter10) + " °C";
        document.getElementById("tmin").textContent = value10(s.thermalMin10) + " °C";
        document.getElementById("tmax").textContent = value10(s.thermalMax10) + " °C";
        document.getElementById("tframe").textContent = s.thermalSequence;
        document.getElementById("tcrc").textContent = s.thermalCrcErrors;

        if (cameraEnabled && currentMode !== 2)
        {
            stopCamera("STM32 is no longer in MODE 2. Camera stopped.");
        }

        clearError();
    }
    catch (error)
    {
        const stm = document.getElementById("stm32");
        stm.textContent = "API ERROR";
        stm.className = "bad";
        showError("/api/status ERROR: " + error.message);
    }
}

function startCameraUI()
{
    cameraEnabled = true;

    document.getElementById("camBtn").textContent = "TURN OFF THERMAL CAMERA";
    document.getElementById("camBtn").className = "red";
    document.getElementById("thermalArea").classList.add("show");
    document.getElementById("thermalMsg").textContent = "Waiting for thermal frame...";
}

function stopCamera(message)
{
    cameraEnabled = false;

    document.getElementById("camBtn").textContent = "TURN ON THERMAL CAMERA";
    document.getElementById("camBtn").className = "blue";
    document.getElementById("thermalArea").classList.remove("show");
    document.getElementById("thermalMsg").textContent =
        message || "Camera is off.";
}

async function toggleCamera()
{
    if (cameraEnabled)
    {
        stopCamera("Camera turned off. ESP32 stopped requesting thermal frames.");
        return;
    }

    try
    {
        clearError();
        startCameraUI();

        if (currentMode !== 2)
        {
            await apiPost("/api/mode?mode=2");
            currentMode = 2;
            updateModeButtons(2);

            await new Promise(resolve => setTimeout(resolve, 350));
            await refreshStatus();
        }

        await fetchThermalFrame();
    }
    catch (error)
    {
        stopCamera("Failed to start the camera.");
        showError("THERMAL START ERROR: " + error.message);
    }
}

function colorMap(x)
{
    x = Math.max(0, Math.min(1, x));

    let r = 0;
    let g = 0;
    let b = 0;

    if (x < 0.25)
    {
        const t = x / 0.25;
        r = 0;
        g = Math.round(255 * t);
        b = 255;
    }
    else if (x < 0.50)
    {
        const t = (x - 0.25) / 0.25;
        r = 0;
        g = 255;
        b = Math.round(255 * (1 - t));
    }
    else if (x < 0.75)
    {
        const t = (x - 0.50) / 0.25;
        r = Math.round(255 * t);
        g = 255;
        b = 0;
    }
    else
    {
        const t = (x - 0.75) / 0.25;
        r = 255;
        g = Math.round(255 * (1 - t));
        b = 0;
    }

    return [r, g, b];
}

async function fetchThermalFrame()
{
    if (!cameraEnabled || thermalFetchBusy || currentMode !== 2)
    {
        return;
    }

    thermalFetchBusy = true;

    try
    {
        const response = await fetch("/api/thermal/frame?_=" + Date.now(),
        {
            cache: "no-store"
        });

        if (!response.ok)
        {
            document.getElementById("thermalMsg").textContent =
                "Waiting for frame from STM32...";
            return;
        }

        const buffer = await response.arrayBuffer();

        if (buffer.byteLength !== 1556)
        {
            throw new Error("packet size = " + buffer.byteLength);
        }

        const data = new DataView(buffer);

        if (data.getUint8(0) !== 84 || data.getUint8(1) !== 72)
        {
            throw new Error("invalid TH header");
        }

        const sequence = data.getUint16(4, true);
        const minTemp = data.getInt16(14, true) / 10.0;
        const maxTemp = data.getInt16(16, true) / 10.0;
        const span = Math.max(1.0, maxTemp - minTemp);

        const image = pixelCtx.createImageData(32, 24);

        let offset = 18;

        for (let i = 0; i < 768; i++)
        {
            const temp = data.getInt16(offset, true) / 10.0;
            offset += 2;

            const color = colorMap((temp - minTemp) / span);
            const p = i * 4;

            image.data[p] = color[0];
            image.data[p + 1] = color[1];
            image.data[p + 2] = color[2];
            image.data[p + 3] = 255;
        }

        pixelCtx.putImageData(image, 0, 0);

        ctx.imageSmoothingEnabled = false;
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.drawImage(
            pixelCanvas,
            0,
            0,
            32,
            24,
            0,
            0,
            canvas.width,
            canvas.height);

        document.getElementById("thermalMsg").textContent =
            "LIVE | Frame " + sequence +
            " | " + minTemp.toFixed(1) +
            " -> " + maxTemp.toFixed(1) + " °C";
    }
    catch (error)
    {
        showError("THERMAL FRAME ERROR: " + error.message);
    }
    finally
    {
        thermalFetchBusy = false;
    }
}

async function forgetWiFi()
{
    if (!confirm("Delete Wi-Fi configuration?"))
    {
        return;
    }

    try
    {
        await fetch("/wifi/reset",
        {
            method: "POST"
        });

        document.body.innerHTML =
            "<h2 style='font-family:Arial;padding:30px'>Wi-Fi deleted. ESP32 is restarting...</h2>";
    }
    catch (error)
    {
        showError("Wi-Fi reset error: " + error.message);
    }
}

stopCamera("Camera is off.");
refreshStatus();

setInterval(refreshStatus, 1000);
setInterval(fetchThermalFrame, 400);
</script>

</body>
</html>
)HTML";

/* =========================================================
 * Helpers
 * ========================================================= */

static const char *modeName(uint8_t mode)
{
    switch (mode)
    {
        case 0U: return "MENU";
        case 1U: return "SENSOR";
        case 2U: return "THERMAL";
        case 3U: return "OUTPUT";
        case 4U: return "TIMER";
        default: return "UNKNOWN";
    }
}

static bool loadWiFiCredentials()
{
    preferences.begin("wifi", true);

    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("password", "");

    preferences.end();

    return savedSSID.length() > 0U;
}

static void saveWiFiCredentials(const String &ssid, const String &password)
{
    preferences.begin("wifi", false);

    preferences.putString("ssid", ssid);
    preferences.putString("password", password);

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

static void PowerOn_ResetOnce()
{
    esp_reset_reason_t reason = esp_reset_reason();

    Serial.print("[BOOT] reset reason = ");
    Serial.println((int)reason);

    if (reason == ESP_RST_POWERON)
    {
        Serial.println("[BOOT] physical power-on detected");
        Serial.println("[BOOT] software resetting ESP32 once");
        Serial.flush();
        delay(100U);
        ESP.restart();
    }

    if (reason == ESP_RST_SW)
    {
        Serial.println("[BOOT] normal application boot after software reset");
    }
    else
    {
        Serial.println("[BOOT] normal application boot");
    }
}

static void sendWebIpToSTM32Once()
{
    if (networkMode != NETWORK_STA)
    {
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (webIpSent)
    {
        return;
    }

    String command = "WEBIP,";
    command += WiFi.localIP().toString();

    Serial.print("[STM32 TX] ");
    Serial.println(command);

    STM32Link_SendCommand(command.c_str());

    webIpSent = true;
}

/* =========================================================
 * Networking
 * ========================================================= */

static void stopHttpServer()
{
    if (!httpServerRunning)
    {
        return;
    }

    server.stop();
    httpServerRunning = false;
    Serial.println("[HTTP] Listener stopped");
}

static void restartHttpServer()
{
    if (!httpRoutesConfigured)
    {
        return;
    }

    stopHttpServer();
    delay(10U);

    server.begin();
    httpServerRunning = true;

    if (networkMode == NETWORK_STA)
    {
        Serial.print("[HTTP] Dashboard listener: http://");
        Serial.print(WiFi.localIP());
        Serial.println("/dashboard?v=5");
    }
    else
    {
        Serial.print("[HTTP] Setup listener: http://");
        Serial.print(WiFi.softAPIP());
        Serial.println("/");
    }
}

static void startProvisioningAP()
{
    stopHttpServer();

    networkMode = NETWORK_PROVISION_AP;
    webIpSent = false;

    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(100U);
    WiFi.mode(WIFI_AP);

    Serial.println();
    Serial.println("====================================");
    Serial.println(" WIFI PROVISIONING MODE");
    Serial.println("====================================");

    bool ok = WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);

    if (!ok)
    {
        networkState = NET_AP_RETRY;
        networkStateStartedMs = millis();
        lastApRetryMs = networkStateStartedMs;
        Serial.println("[AP] FAILED - will retry");
        return;
    }

    networkState = NET_AP_ACTIVE;
    networkStateStartedMs = millis();

    Serial.println("[NET] NET_AP_ACTIVE");
    Serial.print("[AP] SSID: ");
    Serial.println(SETUP_AP_SSID);
    Serial.print("[AP] IP  : ");
    Serial.println(WiFi.softAPIP());
    Serial.print("[AP] WEB : http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/");

    restartHttpServer();
}

static void beginStaConnection()
{
    stopHttpServer();

    networkMode = NETWORK_STA;
    networkState = NET_STA_CONNECTING;
    networkStateStartedMs = millis();
    webIpSent = false;

    WiFi.setAutoReconnect(false);
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(50U);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    Serial.println();
    Serial.println("====================================");
    Serial.println(" CONNECTING TO SAVED WIFI");
    Serial.println("====================================");
    Serial.print("[WIFI] SSID: ");
    Serial.println(savedSSID);
    Serial.println("[WIFI] Connecting for max 15 seconds...");

    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
}

static bool connectToSavedWiFi()
{
    if (savedSSID.length() == 0U)
    {
        return false;
    }

    beginStaConnection();
    return true;
}

static void processNetworkState(uint32_t now)
{
    switch (networkState)
    {
        case NET_STA_CONNECTING:
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                networkState = NET_STA_CONNECTED;
                networkStateStartedMs = now;
                wifiReconnectCount++;
                webIpSent = false;

                Serial.println("[WIFI] CONNECTED");
                Serial.print("[WIFI] IP  : ");
                Serial.println(WiFi.localIP());
                Serial.println("[NET] NET_STA_CONNECTED");

                restartHttpServer();
                sendWebIpToSTM32Once();
                return;
            }

            if ((uint32_t)(now - networkStateStartedMs) >= STA_CONNECT_TIMEOUT_MS)
            {
                Serial.println("[WIFI] 15 SECOND CONNECTION TIMEOUT");
                Serial.println("[WIFI] Falling back to setup AP");
                startProvisioningAP();
            }
            return;
        }

        case NET_STA_CONNECTED:
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                Serial.println("[WIFI] connection lost");
                Serial.println("[WIFI] reconnect window started");
                webIpSent = false;
                beginStaConnection();
                return;
            }

            sendWebIpToSTM32Once();
            return;
        }

        case NET_AP_RETRY:
        {
            if ((uint32_t)(now - lastApRetryMs) >= AP_RETRY_MS)
            {
                lastApRetryMs = now;
                Serial.println("[AP] retrying provisioning AP...");
                startProvisioningAP();
            }
            return;
        }

        case NET_AP_ACTIVE:
        {
            // Recover unattended after an outage, while leaving a connected
            // provisioning client uninterrupted and allowing time to save.
            if (WiFi.softAPgetStationNum() > 0U)
            {
                networkStateStartedMs = now;
                return;
            }
            if (!savedSSID.isEmpty() &&
                (uint32_t)(now - networkStateStartedMs) >= SAVED_WIFI_RETRY_MS)
            {
                Serial.println("[WIFI] Retrying saved network after AP idle window");
                beginStaConnection();
            }
            return;
        }
        case NET_INIT:
        default:
            return;
    }
}

/* =========================================================
 * HTTP
 * ========================================================= */

static void addNoCacheHeaders()
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
}

static void handleRoot()
{
    Serial.print("[HTTP] GET / mode=");
    Serial.println(networkMode == NETWORK_STA ? "STA" : "AP");

    addNoCacheHeaders();

    if (networkMode == NETWORK_PROVISION_AP)
    {
        server.send_P(
            200,
            "text/html; charset=utf-8",
            SETUP_PAGE);

        return;
    }

    /*
     * Never serve the dashboard from the old cached "/" URL.
     * Redirect to a versioned URL so browsers are forced to fetch
     * the current UI after every firmware/UI revision.
     */
    server.sendHeader("Location", "/dashboard?v=5", true);
    server.send(
        302,
        "text/plain",
        "");
}

static void handleDashboard()
{
    Serial.println("[HTTP] GET /dashboard -> UI V5 TIMER");

    addNoCacheHeaders();

    server.send_P(
        200,
        "text/html; charset=utf-8",
        CONTROL_PAGE);
}

static void handleVersion()
{
    addNoCacheHeaders();

    server.send(
        200,
        "application/json",
        "{\"firmware\":\"ESP32 Dashboard V5 Timer\",\"ui\":\"V5 TIMER\",\"thermal\":true}");
}

static void handlePing()
{
    addNoCacheHeaders();

    server.send(
        200,
        "text/plain",
        "ESP32 HTTP SERVER OK - UI V5 TIMER");
}

static void handleWiFiSave()
{
    if (!server.hasArg("ssid"))
    {
        server.send(400, "text/plain", "SSID missing");
        return;
    }

    String ssid = server.arg("ssid");
    String password = server.arg("password");

    ssid.trim();

    if (ssid.length() == 0U)
    {
        server.send(400, "text/plain", "SSID invalid");
        return;
    }

    saveWiFiCredentials(
        ssid,
        password);

    server.send(
        200,
        "text/html; charset=utf-8",
        "<h2>Wi-Fi saved. ESP32 restarting...</h2>");

    delay(1200U);

    ESP.restart();
}

static void handleWiFiReset()
{
    clearWiFiCredentials();

    server.send(
        200,
        "application/json",
        "{\"ok\":true}");

    delay(800U);

    ESP.restart();
}

static void handleStatus()
{
    addNoCacheHeaders();

    const SystemState &s = STM32Link_GetState();
    const ThermalCache &t = STM32Link_GetThermalCache();

    String json;
    json.reserve(1100);

    json += "{";

    json += "\"online\":";
    json += s.stm32Online ? "true" : "false";

    json += ",\"mode\":";
    json += String(s.mode);

    json += ",\"modeName\":\"";
    json += modeName(s.mode);
    json += "\"";

    json += ",\"relays\":";
    json += String(s.relayMask);

    json += ",\"dacCode\":";
    json += String(s.dacCode);

    json += ",\"dacMv\":";
    json += String(s.dacMv);

    json += ",\"dacRunning\":";
    json += s.dacRunning ? "true" : "false";

    json += ",\"diMask\":";
    json += String(s.diMask);

    json += ",\"adc1Mv\":";
    json += String(s.adc1Mv);

    json += ",\"adc2Mv\":";
    json += String(s.adc2Mv);

    json += ",\"temperature10\":";
    json += String(s.temperature10);

    json += ",\"humidity10\":";
    json += String(s.humidity10);

    json += ",\"pressure10\":";
    json += String(s.pressure10);

    json += ",\"lightLux\":";
    json += String(s.lightLux);

    json += ",\"noise10\":";
    json += String(s.noise10);

    json += ",\"validMask\":";
    json += String(s.sensorValidMask);

    json += ",\"timers\":[";

    for (uint8_t i = 0U; i < 4U; i++)
    {
        if (i > 0U)
        {
            json += ",";
        }

        json += "{\"relay\":";
        json += String(i + 1U);

        json += ",\"active\":";
        json += s.relayTimer[i].active ? "true" : "false";

        json += ",\"remaining\":";
        json += String(s.relayTimer[i].remainingSeconds);

        json += ",\"action\":\"";
        json += s.relayTimer[i].targetOn ? "ON" : "OFF";
        json += "\"}";
    }

    json += "]";

    json += ",\"thermalReady\":";
    json += s.thermalReady ? "true" : "false";

    json += ",\"thermalSequence\":";
    json += String(t.valid ? t.sequence : s.thermalSequence);

    json += ",\"thermalAmbient10\":";
    json += String(t.valid ? t.ambient10 : s.thermalAmbient10);

    json += ",\"thermalCenter10\":";
    json += String(t.valid ? t.center10 : s.thermalCenter10);

    json += ",\"thermalMin10\":";
    json += String(t.valid ? t.min10 : s.thermalMin10);

    json += ",\"thermalMax10\":";
    json += String(t.valid ? t.max10 : s.thermalMax10);

    json += ",\"thermalCrcErrors\":";
    json += String(t.crcErrors);

    json += ",\"ssid\":\"";
    json += WiFi.SSID();
    json += "\"";

    json += ",\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\"";

    json += ",\"rssi\":";
    json += String(WiFi.RSSI());

    json += "}";

    server.sendHeader(
        "Cache-Control",
        "no-store");

    server.send(
        200,
        "application/json",
        json);
}

static void sendRequestedModeNow()
{
    char command[20];
    snprintf(command, sizeof(command), "MODE,%u", (unsigned int)requestedMode);

    Serial.print("[MODE TRANSITION -> STM32] ");
    Serial.println(command);
    STM32Link_SendCommand(command);

    lastModeTransitionTxMs = millis();
    modeTransitionRetryCount++;

    if ((modeTransitionRetryCount % 3U) == 0U)
    {
        STM32Link_SendCommand("STATUS,MODE");
    }
}

static void beginModeTransition(uint8_t targetMode)
{
    requestedMode = targetMode;
    modeTransitionActive = true;
    modeTransitionStartMs = millis();
    lastModeTransitionTxMs = 0U;
    modeTransitionRetryCount = 0U;

    if (targetMode != 2U)
    {
        lastThermalWebAccessMs = 0U;
    }

    sendRequestedModeNow();
}

static void processModeTransition(uint32_t now)
{
    if (!modeTransitionActive)
    {
        return;
    }

    const SystemState &state = STM32Link_GetState();

    if (state.mode == requestedMode)
    {
        Serial.print("[MODE TRANSITION] confirmed MODE ");
        Serial.println(requestedMode);
        modeTransitionActive = false;
        return;
    }

    if ((uint32_t)(now - modeTransitionStartMs) >= MODE_TRANSITION_TIMEOUT_MS)
    {
        Serial.print("[MODE TRANSITION] timeout waiting for MODE ");
        Serial.println(requestedMode);
        modeTransitionActive = false;
        return;
    }

    if ((uint32_t)(now - lastModeTransitionTxMs) >= MODE_TRANSITION_RETRY_MS)
    {
        sendRequestedModeNow();
    }
}

static void handleMode()
{
    if (!server.hasArg("mode"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"mode missing\"}");

        return;
    }

    int mode = server.arg("mode").toInt();

    if ((mode < 0) || (mode > 4))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"invalid mode\"}");

        return;
    }

    Serial.print("[WEB] requested MODE ");
    Serial.println(mode);

    beginModeTransition((uint8_t)mode);

    server.send(
        200,
        "application/json",
        "{\"ok\":true}");
}

static void handleRelay()
{
    if (!server.hasArg("relay") ||
        !server.hasArg("action"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"relay/action missing\"}");

        return;
    }

    String relay = server.arg("relay");
    String action = server.arg("action");

    bool validRelay =
        relay == "ALL";

    if (!validRelay)
    {
        int relayNumber =
            relay.toInt();

        validRelay =
            (relayNumber >= 1) &&
            (relayNumber <= 4);
    }

    if (!validRelay ||
        ((action != "ON") &&
         (action != "OFF")))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"invalid relay command\"}");

        return;
    }

    String command = "RELAY,";
    command += relay;
    command += ",";
    command += action;

    Serial.print("[WEB -> STM32] ");
    Serial.println(command);

    STM32Link_SendCommand(
        command.c_str());

    server.send(
        200,
        "application/json",
        "{\"ok\":true}");
}


static void handleTimerArm()
{
    if (!server.hasArg("relay") ||
        !server.hasArg("seconds") ||
        !server.hasArg("action"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"relay/seconds/action missing\"}");
        return;
    }

    int relayNumber = server.arg("relay").toInt();
    uint32_t seconds = (uint32_t)server.arg("seconds").toInt();
    String action = server.arg("action");
    action.toUpperCase();

    if ((relayNumber < 1) || (relayNumber > 4))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"relay must be 1..4\"}");
        return;
    }

    if ((seconds < 1U) || (seconds > 86400U))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"seconds must be 1..86400\"}");
        return;
    }

    if ((action != "ON") && (action != "OFF"))
    {
        server.send(
            400,
            "application/json",
            "{\"error\":\"action must be ON or OFF\"}");
        return;
    }

    String command = "TIMER RELAY ";
    command += String(relayNumber);
    command += " ";
    command += String(seconds);
    command += "s ";
    command += action;

    Serial.print("[WEB -> STM32] ");
    Serial.println(command);

    STM32Link_SendCommand(
        command.c_str());

    server.send(
        200,
        "application/json",
        "{\"ok\":true}");
}

static void handleTimerReset()
{
    Serial.println("[WEB -> STM32] RESET");

    STM32Link_SendCommand(
        "RESET");

    server.send(
        200,
        "application/json",
        "{\"ok\":true}");
}

static void handleThermalFrame()
{
    lastThermalWebAccessMs =
        millis();

    const ThermalCache &thermal =
        STM32Link_GetThermalCache();

    if (!thermal.valid)
    {
        server.send(
            503,
            "text/plain",
            "THERMAL NOT READY");

        return;
    }

    server.sendHeader(
        "Cache-Control",
        "no-store");

    server.setContentLength(
        THERMAL_PACKET_SIZE);

    server.send(
        200,
        "application/octet-stream",
        "");

    WiFiClient client =
        server.client();

    client.write(
        thermal.packet,
        THERMAL_PACKET_SIZE);
}

static void setupHttpServer()
{
    server.on(
        "/",
        HTTP_GET,
        handleRoot);

    server.on(
        "/dashboard",
        HTTP_GET,
        handleDashboard);

    server.on(
        "/version",
        HTTP_GET,
        handleVersion);

    server.on(
        "/ping",
        HTTP_GET,
        handlePing);

    server.on(
        "/wifi/save",
        HTTP_POST,
        handleWiFiSave);

    server.on(
        "/wifi/reset",
        HTTP_POST,
        handleWiFiReset);

    server.on(
        "/api/status",
        HTTP_GET,
        handleStatus);

    server.on(
        "/api/mode",
        HTTP_POST,
        handleMode);

    server.on(
        "/api/relay",
        HTTP_POST,
        handleRelay);

    server.on(
        "/api/timer",
        HTTP_POST,
        handleTimerArm);

    server.on(
        "/api/timer/reset",
        HTTP_POST,
        handleTimerReset);

    server.on(
        "/api/thermal/frame",
        HTTP_GET,
        handleThermalFrame);

    server.onNotFound(
        []()
        {
            Serial.print("[HTTP] 404 ");
            Serial.println(server.uri());

            server.send(
                404,
                "text/plain",
                "404 Not Found");
        });

    httpRoutesConfigured = true;
    Serial.println("[HTTP] Routes configured");
}

/* =========================================================
 * Setup
 * ========================================================= */

void setup()
{
    Serial.begin(115200);
    delay(800U);

    Serial.println();
    Serial.println("====================================");
    Serial.println(" SMART DATALOGGER ESP32 STATE MACHINE");
    Serial.println("====================================");

    PowerOn_ResetOnce();
    STM32Link_Init();
    LineUplink_Init();
    setupHttpServer();

    bool credentialsExist = loadWiFiCredentials();

    if (!credentialsExist)
    {
        Serial.println("[WIFI] No credentials");
        startProvisioningAP();
    }
    else
    {
        Serial.println("[WIFI] Saved credentials found");
        connectToSavedWiFi();
    }

    lastHeartbeatMs = millis();
    lastStatusPollMs = millis();
}

/* =========================================================
 * Main loop
 * ========================================================= */

void loop()
{
    uint32_t now = millis();

    STM32Link_Task();
    STM32Link_UpdateConnectionState(now);

    processModeTransition(now);
    processNetworkState(now);
    LineUplink_Task(now, networkState == NET_STA_CONNECTED, STM32Link_GetState());

    if (httpServerRunning)
    {
        server.handleClient();
    }

    if (networkState == NET_AP_ACTIVE)
    {
        if ((uint32_t)(now - lastHeartbeatMs) >= AP_HEARTBEAT_MS)
        {
            lastHeartbeatMs = now;
            Serial.print("[AP] WEB http://");
            Serial.print(WiFi.softAPIP());
            Serial.print("/ clients=");
            Serial.print(WiFi.softAPgetStationNum());
            Serial.print(" heap=");
            Serial.println(ESP.getFreeHeap());
        }

        yield();
        return;
    }

    if (networkState != NET_STA_CONNECTED)
    {
        yield();
        return;
    }

    const SystemState &state = STM32Link_GetState();

    if (!modeTransitionActive && ((uint32_t)(now - lastStatusPollMs) >= STATUS_POLL_MS))
    {
        lastStatusPollMs = now;
        STM32Link_SendCommand("STATUS,MODE");
        STM32Link_SendCommand("STATUS,IO");
        STM32Link_SendCommand("STATUS,SENSOR");
        STM32Link_SendCommand("STATUS,THERMAL");
        STM32Link_SendCommand("STATUS,TIMER");
    }

    bool thermalWebActive =
        !modeTransitionActive &&
        (lastThermalWebAccessMs != 0U) &&
        ((uint32_t)(now - lastThermalWebAccessMs) < THERMAL_WEB_ACTIVE_MS);

    if (thermalWebActive &&
        (state.mode == 2U) &&
        ((uint32_t)(now - lastThermalRequestMs) >= THERMAL_REQUEST_MS) &&
        !STM32Link_ThermalRequestPending())
    {
        lastThermalRequestMs = now;
        STM32Link_RequestThermalFrame();
    }

    if ((uint32_t)(now - lastHeartbeatMs) >= STA_HEARTBEAT_MS)
    {
        lastHeartbeatMs = now;
        const ThermalCache &thermal = STM32Link_GetThermalCache();

        Serial.println();
        Serial.println("========== HEARTBEAT ==========");
        Serial.print("WEB     : http://");
        Serial.print(WiFi.localIP());
        Serial.println("/dashboard?v=5");
        Serial.print("WIFI    : CONNECTED\n");
        Serial.print("HTTP    : ");
        Serial.println(httpServerRunning ? "RUNNING" : "STOPPED");
        Serial.print("STM32   : ");
        Serial.println(state.stm32Online ? "ONLINE" : "OFFLINE");
        Serial.print("MODE    : ");
        Serial.println(state.mode);
        Serial.print("MODE REQ: ");
        if (modeTransitionActive) Serial.println(requestedMode); else Serial.println("-");
        Serial.print("THERMAL : ");
        Serial.println(thermal.valid ? "FRAME OK" : "WAIT");
        Serial.print("RECONNECTS: ");
        Serial.println(wifiReconnectCount);
        Serial.println("===============================");
    }

    yield();
}
