#pragma once

// Copy to line_config.h (gitignored) and edit before building/uploading.
#define LINE_ENABLED true
// Base URL only. ESP32 appends /api/telemetry. Never use localhost here.
#define LINE_SERVER_URL "http://192.168.1.100:8000"
// Same value as DATALOGGER_API_KEY in the FastAPI server .env.
#define LINE_API_KEY "replace-with-your-server-api-key"
// Empty means an automatically generated esp32-<MAC> identifier.
#define LINE_DEVICE_ID ""

// Each snapshot sends one LINE message. Choose an interval within your quota.
#define LINE_SEND_INTERVAL_MS 300000UL
#define LINE_HTTP_TIMEOUT_MS 10000UL
#define LINE_MAX_ATTEMPTS 4U

// For https://: paste the root CA PEM for YOUR server below, including markers.
// The certificate is public. TLS verification is never disabled.
// HTTPS also requires Internet/NTP access to validate certificate dates.
#define LINE_ROOT_CA ""
// Example syntax:
// #define LINE_ROOT_CA R"PEM(-----BEGIN CERTIFICATE-----
// ... root CA certificate ...
// -----END CERTIFICATE-----
// )PEM"
