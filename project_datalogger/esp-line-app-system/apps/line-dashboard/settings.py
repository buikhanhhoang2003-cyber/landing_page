"""Load configuration once when the application is created."""
import os
import re
from dataclasses import dataclass
from pathlib import Path

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent
TARGET_PATTERN = re.compile(r"^[UCR][0-9a-f]{32}$")


@dataclass(frozen=True)
class Settings:
    api_key: str = ""
    line_token: str = ""
    target_id: str = ""
    channel_secret: str = ""
    database_path: Path = BASE_DIR / "data/datalogger.sqlite3"
    timeout_seconds: float = 6.0

    def __post_init__(self):
        if self.target_id and not TARGET_PATTERN.fullmatch(self.target_id):
            raise ValueError("LINE_TARGET_ID must be a Messaging API U/C/R ID followed by 32 lowercase hex digits")
        if not 0 < self.timeout_seconds <= 30:
            raise ValueError("LINE_HTTP_TIMEOUT_SECONDS must be > 0 and <= 30")

    @classmethod
    def from_env(cls):
        load_dotenv(BASE_DIR / ".env")
        database_path = Path(os.getenv("DATABASE_PATH", "data/datalogger.sqlite3"))
        if not database_path.is_absolute():
            database_path = BASE_DIR / database_path
        return cls(
            api_key=os.getenv("DATALOGGER_API_KEY", "").strip(),
            line_token=os.getenv("LINE_CHANNEL_ACCESS_TOKEN", "").strip(),
            target_id=(os.getenv("LINE_TARGET_ID", "").strip() or os.getenv("LINE_GROUP_ID", "").strip()),
            channel_secret=os.getenv("LINE_CHANNEL_SECRET", "").strip(),
            database_path=database_path,
            timeout_seconds=float(os.getenv("LINE_HTTP_TIMEOUT_SECONDS", "6")),
        )
