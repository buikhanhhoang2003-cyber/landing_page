"""End-to-end contracts at the ESP32, dashboard and LINE HTTP boundaries.

These tests never contact LINE: every outbound request uses MockTransport.
"""

import base64
import hashlib
import hmac
import json
from dataclasses import replace

import httpx
import pytest
from fastapi.testclient import TestClient

from main import create_app
from settings import Settings


API_KEY = "test-device-api-key"
TOKEN = "test-channel-access-token"
SECRET = "test-channel-secret"
TARGET = "U0123456789abcdef0123456789abcdef"
EVENT = "123e4567-e89b-42d3-a456-426614174000"
HEADERS = {"X-API-Key": API_KEY, "X-Event-ID": EVENT}

# The complete shape serialized by line_uplink.cpp, including unused thermal
# readings. Values exercise negative temperature and fixed-point conversion.
SNAPSHOT = {
    "device_id": "logger-01",
    "uptime_ms": 6000,
    "online": True,
    "mode": 1,
    "temperature10": -35,
    "humidity10": 654,
    "pressure10": 10132,
    "lightLux": 320,
    "noise10": 452,
    "validMask": 15,
    "adc1Mv": 1234,
    "adc2Mv": 2345,
    "diMask": 3,
    "relays": 1,
    "thermalReady": False,
    "thermalAmbient10": 0,
    "thermalCenter10": 0,
    "thermalMin10": 0,
    "thermalMax10": 0,
}


@pytest.fixture
def contract_settings(tmp_path):
    return Settings(
        api_key=API_KEY,
        line_token=TOKEN,
        target_id=TARGET,
        channel_secret=SECRET,
        database_path=tmp_path / "contract.sqlite3",
    )


def client_for(settings, handler):
    return TestClient(create_app(settings, transport=httpx.MockTransport(handler)))


def test_firmware_snapshot_reaches_line_and_dashboard(contract_settings):
    requests = []

    def line_api(request):
        requests.append(request)
        return httpx.Response(200, json={}, headers={"x-line-request-id": "line-request-1"})

    with client_for(contract_settings, line_api) as client:
        response = client.post("/api/telemetry", headers=HEADERS, json=SNAPSHOT)
        assert response.status_code == 200, response.text
        assert response.json()["line_status"] == "sent"
        latest = client.get("/api/telemetry/latest", headers=HEADERS).json()

    assert len(requests) == 1
    request = requests[0]
    assert str(request.url) == "https://api.line.me/v2/bot/message/push"
    assert request.headers["Authorization"] == f"Bearer {TOKEN}"
    assert request.headers["X-Line-Retry-Key"] == EVENT
    sent = json.loads(request.content)
    assert sent["to"] == TARGET
    assert len(sent["messages"]) == 1
    text = sent["messages"][0]["text"]
    for converted in ("-3.5", "65.4", "1013.2", "320 lux", "45.2"):
        assert converted in text
    reading = latest["readings"][0]
    assert reading["data"] == SNAPSHOT
    assert reading["device_id"] == SNAPSHOT["device_id"]
    assert reading["event_id"] == EVENT
    assert reading["received_at"]
    assert reading["line_status"] == "sent"
    assert reading["line_error"] is None
    assert reading["message"] == text


@pytest.mark.parametrize("key", [None, "incorrect-key"])
def test_authentication_prevents_storage_and_line_side_effects(contract_settings, key):
    requests = []

    def line_api(request):
        requests.append(request)
        return httpx.Response(200, json={})

    headers = {"X-Event-ID": EVENT}
    if key is not None:
        headers["X-API-Key"] = key
    with client_for(contract_settings, line_api) as client:
        for path, body in (
            ("/api/telemetry", SNAPSHOT),
            ("/api/line/push", {"message": "unauthorized"}),
            ("/api/line/broadcast", {"message": "unauthorized"}),
            ("/api/notify", {"message": "unauthorized"}),
        ):
            response = client.post(path, headers=headers, json=body)
            assert response.status_code == 401, (path, response.text)
        assert client.get("/api/telemetry/latest", headers=HEADERS).json() == {"readings": []}
    assert requests == []


def test_timeout_retry_after_restart_keeps_original_message_and_recipient(contract_settings):
    requests = []

    def lost_response(request):
        requests.append(request)
        # LINE might have accepted the message although its response was lost.
        raise httpx.ReadTimeout("response lost", request=request)

    with client_for(contract_settings, lost_response) as client:
        response = client.post("/api/telemetry", headers=HEADERS, json=SNAPSHOT)
        assert response.status_code == 504, response.text
        assert client.get("/api/telemetry/latest", headers=HEADERS).json()["readings"][0]["line_status"] == "failed"

    def already_accepted(request):
        requests.append(request)
        return httpx.Response(
            409,
            headers={"x-line-accepted-request-id": "accepted-before-timeout"},
            json={"message": "The retry key is already accepted"},
        )

    # Changing the default after a restart must not change a pending retry.
    new_settings = replace(contract_settings, target_id="Cabcdef0123456789abcdef0123456789")
    with client_for(new_settings, already_accepted) as client:
        response = client.post("/api/telemetry", headers=HEADERS, json=SNAPSHOT)
        assert response.status_code == 200, response.text
        assert response.json()["line_status"] == "sent"
        duplicate = client.post("/api/telemetry", headers=HEADERS, json=SNAPSHOT)
        assert duplicate.status_code == 200
        assert duplicate.json()["duplicate"] is True
        readings = client.get("/api/telemetry/latest", headers=HEADERS).json()["readings"]
        assert len(readings) == 1
        assert readings[0]["line_status"] == "sent"

    assert len(requests) == 2
    assert requests[0].content == requests[1].content
    assert requests[0].headers["X-Line-Retry-Key"] == requests[1].headers["X-Line-Retry-Key"] == EVENT
    assert json.loads(requests[1].content)["to"] == TARGET


def test_webhook_signature_controls_discovery_without_sending(contract_settings):
    requests = []

    def line_api(request):
        requests.append(request)
        return httpx.Response(200, json={})

    payload = json.dumps({
        "events": [{"type": "message", "source": {"type": "user", "userId": TARGET}}],
    }).encode()
    signature = base64.b64encode(hmac.new(SECRET.encode(), payload, hashlib.sha256).digest()).decode()
    with client_for(contract_settings, line_api) as client:
        response = client.post(
            "/api/line/webhook", content=payload,
            headers={"Content-Type": "application/json", "X-Line-Signature": "wrong-signature"},
        )
        assert response.status_code in (400, 401, 403)
        assert client.get("/api/line/targets", headers=HEADERS).json() == {"targets": []}
        response = client.post(
            "/api/line/webhook", content=payload,
            headers={"Content-Type": "application/json", "X-Line-Signature": signature},
        )
        assert response.status_code == 200, response.text
        targets = client.get("/api/line/targets", headers=HEADERS).json()["targets"]
        assert len(targets) == 1
        assert targets[0]["target_id"] == TARGET
        assert targets[0]["type"] == "user"
    assert requests == []


@pytest.mark.parametrize("patch", [
    {"temperature10": float("nan")},
    {"uptime_ms": True},
    {"token": "untrusted-token-override"},
])
def test_invalid_sensor_json_is_rejected_without_side_effects(contract_settings, patch):
    requests = []

    def line_api(request):
        requests.append(request)
        return httpx.Response(200, json={})

    with client_for(contract_settings, line_api) as client:
        # content= lets the test exercise nonstandard NaN JSON from a bad client.
        response = client.post(
            "/api/telemetry", content=json.dumps({**SNAPSHOT, **patch}),
            headers={**HEADERS, "Content-Type": "application/json"},
        )
        assert response.status_code in (400, 422), response.text
        assert client.get("/api/telemetry/latest", headers=HEADERS).json() == {"readings": []}
    assert requests == []
