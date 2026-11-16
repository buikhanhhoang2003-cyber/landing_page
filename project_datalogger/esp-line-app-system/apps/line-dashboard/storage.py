"""Small persistent event ledger; all calls run in the server thread pool."""
import json
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class EventConflict(Exception):
    pass


class Store:
    def __init__(self, path: Path):
        self.path = path

    @contextmanager
    def connection(self):
        db = sqlite3.connect(self.path, timeout=3)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def initialize(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connection() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS telemetry (
                    event_id TEXT PRIMARY KEY, device_id TEXT NOT NULL,
                    received_at TEXT NOT NULL, payload TEXT NOT NULL,
                    target_id TEXT NOT NULL, message TEXT NOT NULL,
                    line_status TEXT NOT NULL, line_error TEXT
                );
                CREATE INDEX IF NOT EXISTS device_time ON telemetry(device_id, received_at);
                CREATE TABLE IF NOT EXISTS targets (
                    target_id TEXT PRIMARY KEY, type TEXT NOT NULL, last_seen TEXT NOT NULL
                );
            """)

    @staticmethod
    def decode(row):
        result = dict(row)
        result["data"] = json.loads(result.pop("payload"))
        return result

    def receive(self, event_id: str, data: dict, target_id: str, message: str):
        payload = json.dumps(data, sort_keys=True, separators=(",", ":"))
        with self.connection() as db:
            db.execute("BEGIN IMMEDIATE")
            existing = db.execute("SELECT * FROM telemetry WHERE event_id=?", (event_id,)).fetchone()
            if existing:
                if existing["payload"] != payload:
                    raise EventConflict("X-Event-ID was already used for a different payload")
                return self.decode(existing), True
            db.execute(
                "INSERT INTO telemetry VALUES (?,?,?,?,?,?,?,?)",
                (event_id, data["device_id"], utc_now(), payload, target_id, message, "pending", None),
            )
            row = db.execute("SELECT * FROM telemetry WHERE event_id=?", (event_id,)).fetchone()
            return self.decode(row), False

    def set_result(self, event_id: str, status: str, error: str | None = None):
        with self.connection() as db:
            # A concurrent successful request must not be overwritten by a timeout.
            db.execute(
                "UPDATE telemetry SET line_status=?, line_error=? WHERE event_id=? AND line_status!='sent'",
                (status, error, event_id),
            )

    def latest(self):
        with self.connection() as db:
            rows = db.execute("""
                SELECT * FROM (
                    SELECT *, ROW_NUMBER() OVER (PARTITION BY device_id ORDER BY received_at DESC, rowid DESC) AS rank
                    FROM telemetry
                ) WHERE rank=1 ORDER BY received_at DESC
            """).fetchall()
            result = []
            for row in rows:
                item = self.decode(row)
                item.pop("rank")
                item.pop("target_id")
                result.append(item)
            return result

    def record_targets(self, targets: list[tuple[str, str]]):
        with self.connection() as db:
            db.executemany(
                "INSERT INTO targets VALUES (?,?,?) ON CONFLICT(target_id) DO UPDATE SET type=excluded.type,last_seen=excluded.last_seen",
                [(target, kind, utc_now()) for target, kind in targets],
            )

    def targets(self):
        with self.connection() as db:
            return [dict(row) for row in db.execute("SELECT * FROM targets ORDER BY last_seen DESC")]
