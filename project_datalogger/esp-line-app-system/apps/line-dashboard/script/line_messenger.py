"""Asynchronous LINE Messaging API transport; credentials never come from callers."""
import asyncio
from uuid import uuid4

import httpx

BASE_URL = "https://api.line.me/v2/bot/message"


class LineError(Exception):
    def __init__(self, message: str, http_status: int = 502):
        super().__init__(message)
        self.http_status = http_status


async def send_messages(client: httpx.AsyncClient, token: str, messages: list[dict], *,
                        target: str | None = None, retry_key: str | None = None,
                        timeout_seconds: float = 6.0) -> dict:
    payload = {"messages": messages}
    endpoint = "broadcast"
    if target is not None:
        payload["to"] = target
        endpoint = "push"
    headers = {
        "Authorization": f"Bearer {token}",
        "X-Line-Retry-Key": retry_key or str(uuid4()),
    }
    try:
        # Bound the whole request, including connect/TLS/read phases.
        response = await asyncio.wait_for(
            client.post(f"{BASE_URL}/{endpoint}", headers=headers, json=payload),
            timeout=timeout_seconds,
        )
    except (httpx.TimeoutException, asyncio.TimeoutError) as exc:
        raise LineError("LINE request timed out", 504) from exc
    except httpx.RequestError as exc:
        raise LineError("Cannot connect to LINE Messaging API") from exc
    duplicate = response.status_code == 409 and bool(response.headers.get("x-line-accepted-request-id"))
    if not 200 <= response.status_code < 300 and not duplicate:
        # Do not relay upstream bodies, which may contain credential/recipient data.
        raise LineError(f"LINE Messaging API rejected the request (HTTP {response.status_code})")
    return {"success": True, "status": response.status_code, "duplicate": duplicate}
