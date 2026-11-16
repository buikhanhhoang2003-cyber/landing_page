import base64
import hashlib
import hmac
import json
from dataclasses import replace
from uuid import uuid4

import httpx
import pytest
from fastapi.testclient import TestClient

from main import create_app
from models import Telemetry, format_telemetry
from settings import Settings
from storage import Store

KEY = "unit-test-api-key"
TARGET = "U" + "1" * 32
HEADERS = {"X-API-Key": KEY}


@pytest.fixture
def config(tmp_path):
    return Settings(api_key=KEY, line_token="test-token", target_id=TARGET,
                    channel_secret="test-secret", database_path=tmp_path / "test.sqlite3")


@pytest.fixture
def payload():
    return {
        "device_id": "esp32-test", "uptime_ms": 12000, "online": True, "mode": 2,
        "temperature10": 253, "humidity10": 654, "pressure10": 10132,
        "lightLux": 150, "noise10": 425, "validMask": 15,
        "adc1Mv": 1000, "adc2Mv": 2000, "diMask": 1, "relays": 2,
        "thermalReady": True, "thermalAmbient10": 250, "thermalCenter10": 321,
        "thermalMin10": 231, "thermalMax10": 350,
    }


def client(config, handler):
    return TestClient(create_app(config, transport=httpx.MockTransport(handler)))


def auth_event():
    return {**HEADERS, "X-Event-ID": str(uuid4())}


def test_manual_endpoints_and_no_browser_token(config):
    requests = []
    def upstream(request):
        requests.append(request)
        return httpx.Response(200, json={})
    with client(config, upstream) as api:
        assert api.get("/healthz").json() == {"ok": True}
        status = api.get("/api/status").json()
        assert status == dict(tokenConfigured=True, targetConfigured=True,
                              apiKeyConfigured=True, webhookConfigured=True)
        assert api.get("/").status_code == 200
        assert api.get("/static/app.js").status_code == 200
        for endpoint in ("/api/notify", "/api/line/push", "/api/line/broadcast"):
            assert api.post(endpoint, json={"message": "hello"}).status_code == 401
            assert api.post(endpoint, headers=HEADERS, json={"message": "hello"}).status_code == 200
        assert json.loads(requests[0].content)["to"] == TARGET
        assert json.loads(requests[1].content)["to"] == TARGET
        assert "to" not in json.loads(requests[2].content)
        assert requests[0].headers["authorization"] == "Bearer test-token"
        assert "x-line-retry-key" in requests[0].headers
        assert api.post("/api/line/push", headers=HEADERS,
                        json={"message": "hello", "token": "do-not-echo-secret"}).status_code == 422
        assert len(requests) == 3


def test_manual_validation(config):
    def upstream(_):
        pytest.fail("Invalid messages must not call LINE")
    with client(config, upstream) as api:
        for data in ({"message": " "}, {"message": "x", "image": "http://example.com/img.png"},
                     {"message": "x", "sticker": ["1"]}, {"message": "😀" * 2501},
                     {"message": "x", "user_id": "invalid"}):
            assert api.post("/api/notify", headers=HEADERS, json=data).status_code == 422


def test_missing_configuration_fails_closed(config, payload):
    def upstream(_):
        pytest.fail("Unconfigured server must not call LINE")
    with client(replace(config, api_key=""), upstream) as api:
        assert api.post("/api/telemetry", headers=auth_event(), json=payload).status_code == 503
        assert api.get("/api/telemetry/latest", headers=HEADERS).status_code == 503
    with client(replace(config, line_token=""), upstream) as api:
        assert api.post("/api/telemetry", headers=auth_event(), json=payload).status_code == 503


@pytest.mark.parametrize("status", [400, 401, 403, 409, 429, 500])
def test_upstream_failure_saved_and_redacted(config, payload, status):
    with client(config, lambda _: httpx.Response(status, json={"message": "test-token"})) as api:
        response = api.post("/api/telemetry", headers=auth_event(), json=payload)
        assert response.status_code == 502
        assert "test-token" not in response.text
        reading = api.get("/api/telemetry/latest", headers=HEADERS).json()["readings"][0]
        assert reading["line_status"] == "failed"
        assert str(status) in reading["line_error"]


def test_network_failure_saved(config, payload):
    def upstream(request):
        raise httpx.ConnectError("connection details must not leak", request=request)
    with client(config, upstream) as api:
        response = api.post("/api/telemetry", headers=auth_event(), json=payload)
        assert response.status_code == 502
        assert "details" not in response.text
        assert api.get("/api/telemetry/latest", headers=HEADERS).json()["readings"][0]["line_status"] == "failed"


def test_event_id_reuse_cannot_change_payload(config, payload):
    requests = []
    def upstream(request):
        requests.append(request)
        return httpx.Response(200, json={})
    headers = auth_event()
    with client(config, upstream) as api:
        assert api.post("/api/telemetry", headers=headers, json=payload).status_code == 200
        changed = {**payload, "temperature10": 300}
        assert api.post("/api/telemetry", headers=headers, json=changed).status_code == 409
        assert len(requests) == 1


def test_retry_expired_event_does_not_resend(config, payload):
    headers = auth_event()
    with client(config, lambda _: httpx.Response(500)) as api:
        assert api.post("/api/telemetry", headers=headers, json=payload).status_code == 502
    store = Store(config.database_path)
    with store.connection() as db:
        db.execute("UPDATE telemetry SET received_at='2020-01-01T00:00:00+00:00'")
    def upstream(_):
        pytest.fail("Expired retry must not contact LINE")
    with client(config, upstream) as api:
        assert api.post("/api/telemetry", headers=headers, json=payload).status_code == 409


def test_format_invalid_offline_and_thermal(payload):
    message = format_telemetry(Telemetry(**payload))
    assert "25.3 C" in message and "1013.2 hPa" in message
    assert "ambient 25.0, center 32.1, min 23.1, max 35.0" in message
    invalid = format_telemetry(Telemetry(**{**payload, "validMask": 2}))
    assert "Temperature: unavailable" in invalid
    assert "Humidity: unavailable" in invalid
    assert "Pressure: 1013.2 hPa" in invalid
    offline = format_telemetry(Telemetry(**{**payload, "online": False}))
    assert "STM32: OFFLINE" in offline
    assert "25.3" not in offline and "ADC / digital inputs / relays: unavailable" in offline
    assert "Thermal: unavailable" in offline


def test_webhook_verification_and_malformed_signed_body(config):
    def upstream(_):
        pytest.fail("Webhook discovery must never send messages")
    def sign(body):
        return base64.b64encode(hmac.new(config.channel_secret.encode(), body, hashlib.sha256).digest()).decode()
    with client(config, upstream) as api:
        for body, status in ((b'{"events":[]}', 200), (b'bad-json', 400),
                             (b'{"events":null}', 400), (b'{"events":[null]}', 400)):
            response = api.post("/api/line/webhook", content=body,
                                headers={"X-Line-Signature": sign(body)})
            assert response.status_code == status
        assert api.get("/api/line/targets", headers=HEADERS).json() == {"targets": []}
        assert api.post("/api/line/webhook", content=b"x" * 65537).status_code == 413
