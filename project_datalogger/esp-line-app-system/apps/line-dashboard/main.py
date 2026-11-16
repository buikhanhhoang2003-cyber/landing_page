"""ESP32 telemetry gateway and LINE Messaging API dashboard."""
import base64
import hashlib
import hmac
import json
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from uuid import UUID

import httpx
from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from starlette.concurrency import run_in_threadpool

from models import ManualMessage, Telemetry, format_telemetry
from script.line_messenger import LineError, send_messages
from settings import BASE_DIR, TARGET_PATTERN, Settings
from storage import EventConflict, Store


def create_app(settings: Settings | None = None, transport: httpx.AsyncBaseTransport | None = None) -> FastAPI:
    config = settings or Settings.from_env()
    store = Store(config.database_path)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        await run_in_threadpool(store.initialize)
        async with httpx.AsyncClient(timeout=config.timeout_seconds, transport=transport) as client:
            app.state.line_client = client
            yield

    app = FastAPI(title="Smart Datalogger LINE Gateway", lifespan=lifespan)
    app.state.settings = config
    app.mount("/static", StaticFiles(directory=BASE_DIR / "static"), name="static")

    @app.exception_handler(RequestValidationError)
    async def invalid_request(request: Request, exc: RequestValidationError):
        # Omit raw input (including NaN/Infinity and possible client secrets).
        errors = [{"loc": error["loc"], "msg": error["msg"], "type": error["type"]}
                  for error in exc.errors()]
        return JSONResponse(status_code=422, content={"detail": errors})

    def require_key(x_api_key: str | None = Header(default=None)):
        if not config.api_key:
            raise HTTPException(503, "DATALOGGER_API_KEY is not configured")
        if x_api_key is None or not hmac.compare_digest(x_api_key.encode(), config.api_key.encode()):
            raise HTTPException(401, "Invalid or missing X-API-Key")

    def require_line(target_required: bool = False):
        if not config.line_token:
            raise HTTPException(503, "LINE_CHANNEL_ACCESS_TOKEN is not configured")
        if target_required and not config.target_id:
            raise HTTPException(503, "LINE_TARGET_ID is not configured")

    @app.get("/")
    def index():
        return FileResponse(BASE_DIR / "static/index.html")

    @app.get("/healthz")
    def healthz():
        return {"ok": True}

    @app.get("/api/status")
    def status():
        return {
            "tokenConfigured": bool(config.line_token), "targetConfigured": bool(config.target_id),
            "apiKeyConfigured": bool(config.api_key), "webhookConfigured": bool(config.channel_secret),
        }

    @app.get("/api/telemetry/latest", dependencies=[Depends(require_key)])
    async def latest():
        return {"readings": await run_in_threadpool(store.latest)}

    @app.post("/api/telemetry", dependencies=[Depends(require_key)])
    async def telemetry(data: Telemetry, request: Request, x_event_id: UUID = Header()):
        require_line(target_required=True)
        event_id = str(x_event_id)
        try:
            record, duplicate = await run_in_threadpool(
                store.receive, event_id, data.model_dump(), config.target_id, format_telemetry(data),
            )
        except EventConflict as exc:
            raise HTTPException(409, str(exc)) from exc
        if record["line_status"] == "sent":
            return {"success": True, "event_id": event_id, "duplicate": True, "line_status": "sent"}
        # LINE retry keys expire after 24 hours; never resend an ambiguous old event.
        age = (datetime.now(timezone.utc) - datetime.fromisoformat(record["received_at"])).total_seconds()
        if age >= 24 * 3600:
            await run_in_threadpool(store.set_result, event_id, "failed", "Retry window expired")
            raise HTTPException(409, "Event is outside the 24-hour LINE retry window; submit a new snapshot")
        try:
            result = await send_messages(
                request.app.state.line_client, config.line_token,
                [{"type": "text", "text": record["message"]}],
                target=record["target_id"], retry_key=event_id, timeout_seconds=config.timeout_seconds,
            )
        except LineError as exc:
            await run_in_threadpool(store.set_result, event_id, "failed", str(exc))
            raise HTTPException(exc.http_status, str(exc)) from exc
        await run_in_threadpool(store.set_result, event_id, "sent")
        return {"success": True, "event_id": event_id, "duplicate": duplicate or result["duplicate"], "line_status": "sent"}

    async def manual_send(data: ManualMessage, request: Request, broadcast: bool):
        require_line()
        target = None if broadcast else data.user_id or data.target or config.target_id
        if not broadcast and not target:
            raise HTTPException(400, "Provide a recipient ID or configure LINE_TARGET_ID")
        try:
            return await send_messages(
                request.app.state.line_client, config.line_token, data.messages(),
                target=target, timeout_seconds=config.timeout_seconds,
            )
        except LineError as exc:
            raise HTTPException(exc.http_status, str(exc)) from exc

    @app.post("/api/notify", dependencies=[Depends(require_key)])
    async def notify(data: ManualMessage, request: Request):
        return await manual_send(data, request, data.broadcast or data.type == "broadcast")

    @app.post("/api/line/push", dependencies=[Depends(require_key)])
    async def push(data: ManualMessage, request: Request):
        return await manual_send(data, request, False)

    @app.post("/api/line/broadcast", dependencies=[Depends(require_key)])
    async def broadcast(data: ManualMessage, request: Request):
        return await manual_send(data, request, True)

    @app.get("/api/line/targets", dependencies=[Depends(require_key)])
    async def targets():
        return {"targets": await run_in_threadpool(store.targets)}

    @app.post("/api/line/webhook")
    async def webhook(request: Request, x_line_signature: str | None = Header(default=None)):
        if not config.channel_secret:
            raise HTTPException(503, "LINE_CHANNEL_SECRET is not configured")
        body = bytearray()
        async for chunk in request.stream():
            body.extend(chunk)
            if len(body) > 65536:
                raise HTTPException(413, "Webhook body exceeds 64 KiB")
        expected = base64.b64encode(hmac.new(config.channel_secret.encode(), body, hashlib.sha256).digest())
        if not x_line_signature or not hmac.compare_digest(expected, x_line_signature.encode()):
            raise HTTPException(401, "Invalid or missing LINE signature")
        try:
            payload = json.loads(body)
        except (ValueError, UnicodeDecodeError) as exc:
            raise HTTPException(400, "Invalid webhook JSON") from exc
        if not isinstance(payload, dict) or not isinstance(payload.get("events"), list):
            raise HTTPException(400, "Webhook events must be an array")
        found = []
        for event in payload["events"]:
            if not isinstance(event, dict):
                raise HTTPException(400, "Each webhook event must be an object")
            source = event.get("source", {})
            if not isinstance(source, dict):
                raise HTTPException(400, "Webhook source must be an object")
            # Record identifiers only; never change the configured destination automatically.
            for field, kind in (("userId", "user"), ("groupId", "group"), ("roomId", "room")):
                target = source.get(field)
                if isinstance(target, str) and TARGET_PATTERN.fullmatch(target):
                    found.append((target, kind))
        await run_in_threadpool(store.record_targets, found)
        return {"ok": True, "events": len(payload["events"])}

    return app


app = create_app()
