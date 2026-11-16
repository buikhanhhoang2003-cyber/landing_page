#!/usr/bin/env python3
"""Configure the server and ESP32 without evaluating shell or requiring packages."""
from __future__ import annotations

import argparse
import getpass
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import socket
import ssl
import sys
import tempfile
from urllib.parse import urlsplit

KEYS = ("DATALOGGER_API_KEY", "LINE_CHANNEL_ACCESS_TOKEN", "LINE_TARGET_ID", "LINE_CHANNEL_SECRET")
TARGET_RE = re.compile(r"[UCR][0-9a-f]{32}\Z")
DEVICE_RE = re.compile(r"[A-Za-z0-9_.:-]{0,64}\Z")
ENV_LINE = re.compile(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")


class ConfigError(Exception):
    pass


def parse_env_value(value: str) -> str:
    """Read the common single-line dotenv forms, never execute substitutions."""
    value = value.strip()
    if not value:
        return ""
    if value.startswith(("'", '"')):
        quote = value[0]
        match = re.match(r"^(['\"])((?:\\.|(?!\1).)*)\1\s*(?:#.*)?$", value)
        if not match:
            raise ConfigError("Existing .env contains an unsupported quoted/multiline value; edit it to a single line first")
        inner = match[2]
        if quote == "'":
            value = re.sub(r"\\([\\'])", r"\1", inner)
        else:
            escapes = {"n": "\n", "r": "\r", "t": "\t", "b": "\b", "f": "\f", "v": "\v", '"': '"', "'": "'", "\\": "\\"}
            value = re.sub(r"\\(.)", lambda m: escapes.get(m[1], m[0]), inner)
    else:
        value = re.split(r"\s+#", value, maxsplit=1)[0].rstrip()
    return value


def read_env(path: Path) -> tuple[str, dict[str, str]]:
    if not path.exists():
        return "", {}
    if path.is_symlink() or not path.is_file():
        raise ConfigError("Server .env must be a regular file, not a symlink")
    content = path.read_text(encoding="utf-8")
    values = {}
    for line in content.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = ENV_LINE.match(line)
        if not match:
            raise ConfigError("Existing .env contains an unsupported line; use KEY=value on one line")
        if match[1] in values:
            raise ConfigError(f"Existing .env defines {match[1]} more than once; remove the duplicate first")
        values[match[1]] = parse_env_value(match[2])
    return content, values


def macro_string(content: str, name: str, force: bool = False) -> str | None:
    definitions = list(re.finditer(r"^\s*#define\s+" + name + r"\s+", content, re.MULTILINE))
    if not definitions:
        return None
    if len(definitions) != 1:
        if force:
            return None
        raise ConfigError(f"Existing {name} has multiple definitions; use --force-config or edit it manually")
    start = definitions[0]
    rest = content[start.end():]

    def complete_literal(end: int) -> bool:
        tail = rest[end:].split("\n", 1)[0].strip()
        return not tail or tail.startswith("//") or bool(re.fullmatch(r"(?:/\*.*?\*/\s*)+(?://.*)?", tail))

    raw = re.match(r'R"([A-Za-z0-9_]{0,16})\((.*?)\)\1"', rest, re.DOTALL)
    if raw and complete_literal(raw.end()):
        return raw[2]
    literal = re.match(r'"(?:\\.|[^"\\\r\n])*"', rest)
    if literal and complete_literal(literal.end()):
        try:
            return json.loads(literal[0])
        except ValueError:
            pass
    if force:
        return None
    raise ConfigError(f"Existing {name} must be a quoted literal; edit it manually or use --force-config")


def clean_value(name: str, value: str) -> str:
    if any(ord(char) < 32 or ord(char) == 127 for char in value):
        raise ConfigError(f"{name} must be a single line without control characters")
    if "${" in value:
        raise ConfigError(f"{name} cannot contain dotenv interpolation syntax (${{...}})")
    return value.strip()


def validate_url(value: str) -> str:
    value = clean_value("LINE_SERVER_URL", value).rstrip("/")
    if not value:
        return ""
    try:
        parsed = urlsplit(value)
        parsed.port
    except ValueError as exc:
        raise ConfigError("LINE_SERVER_URL is not a valid HTTP/HTTPS base URL") from exc
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
        raise ConfigError("LINE_SERVER_URL must be an HTTP/HTTPS base URL without credentials")
    if parsed.query or parsed.fragment or any(char.isspace() for char in value):
        raise ConfigError("LINE_SERVER_URL cannot contain whitespace, query parameters or a fragment")
    host = parsed.hostname.lower()
    if host == "localhost" or host.endswith(".localhost"):
        raise ConfigError("ESP32 cannot reach localhost; use the server LAN IP or public domain")
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        pass
    else:
        if address.is_loopback or address.is_unspecified or address.is_multicast:
            raise ConfigError("Use a reachable server LAN IP/domain, not a loopback/listen/multicast address")
    if parsed.path.endswith("/api/telemetry"):
        raise ConfigError("Use the base server URL without /api/telemetry; firmware appends that path")
    return value


def detect_url(port: int) -> str:
    candidates = []
    try:
        # Resolve this computer's name locally; no external network probe is sent.
        candidates = [entry[4][0] for entry in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)]
    except OSError:
        pass
    for candidate in candidates:
        address = ipaddress.ip_address(candidate)
        if address.is_private and not address.is_loopback and not address.is_unspecified:
            return f"http://{address}:{port}"
    return ""


def prompt_value(label: str, default: str, secret: bool = False) -> str:
    suffix = " [Enter to keep current value]" if default else " [Enter to skip]"
    value = getpass.getpass(label + suffix + ": ") if secret else input(label + suffix + ": ")
    return value.strip() or default


def atomic_write(path: Path, content: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ConfigError(f"Refusing to replace symlink: {path.name}")
    fd, temporary = tempfile.mkstemp(prefix=".setup-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            os.fchmod(stream.fileno(), 0o600)
            stream.write(content)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def render_env(original: str, values: dict[str, str]) -> str:
    remaining = dict(values)
    result = []
    for line in original.splitlines():
        match = ENV_LINE.match(line)
        if match and match[1] in remaining:
            name = match[1]
            # Single quotes prevent shell syntax being interpreted by dotenv.
            value = remaining.pop(name).replace("\\", "\\\\").replace("'", "\\'")
            result.append(f"{name}='{value}'")
        else:
            result.append(line)
    for name, value in remaining.items():
        value = value.replace("\\", "\\\\").replace("'", "\\'")
        result.append(f"{name}='{value}'")
    return "\n".join(result) + "\n"


def configure(args):
    root = args.root.resolve()
    env_path = root / "esp-line-app-system/apps/line-dashboard/.env"
    header_path = root / "smart_datalogger_v1/esp32_smartlogger/include/line_config.h"
    if not (env_path.parent / "main.py").is_file() or not (header_path.parent.parent / "platformio.ini").is_file():
        raise ConfigError("Incomplete project: server or firmware source is missing")
    if not 1 <= args.port <= 65535:
        raise ConfigError("--port must be between 1 and 65535")
    if not args.host or any(char.isspace() or ord(char) < 32 for char in args.host):
        raise ConfigError("--host must be a valid listen IP/hostname without whitespace")
    env_text, old_env = read_env(env_path)
    if header_path.is_symlink() or (header_path.exists() and not header_path.is_file()):
        raise ConfigError("Firmware line_config.h must be a regular file, not a symlink")
    header_text = header_path.read_text(encoding="utf-8") if header_path.exists() else ""
    old_header = {name: macro_string(header_text, name, args.force_config) for name in
                  ("LINE_API_KEY", "LINE_SERVER_URL", "LINE_DEVICE_ID", "LINE_ROOT_CA")}
    interactive = not args.non_interactive and sys.stdin.isatty()
    incoming = {name: os.environ.get(name) for name in KEYS}
    values = {name: incoming[name] if incoming[name] is not None else old_env.get(name, "") for name in KEYS}
    values["LINE_TARGET_ID"] = values["LINE_TARGET_ID"] or old_env.get("LINE_GROUP_ID", "")
    values["DATALOGGER_API_KEY"] = values["DATALOGGER_API_KEY"] or old_header["LINE_API_KEY"] or secrets.token_urlsafe(32)

    if interactive:
        for name, label, hidden in (("LINE_CHANNEL_ACCESS_TOKEN", "LINE channel access token", True),
                                    ("LINE_TARGET_ID", "LINE target ID (U.../C.../R...)", False),
                                    ("LINE_CHANNEL_SECRET", "LINE channel secret (optional webhook)", True)):
            # Existing settings are kept unless the user explicitly requests regeneration.
            if incoming[name] is None and (not values[name] or args.force_config):
                values[name] = prompt_value(label, values[name], hidden)
    values = {name: clean_value(name, value) for name, value in values.items()}
    if not values["DATALOGGER_API_KEY"]:
        values["DATALOGGER_API_KEY"] = secrets.token_urlsafe(32)
    for name in ("DATALOGGER_API_KEY", "LINE_CHANNEL_ACCESS_TOKEN"):
        if not values[name].isascii():
            raise ConfigError(f"{name} must contain ASCII characters for HTTP headers")
    if values["LINE_TARGET_ID"] and not TARGET_RE.fullmatch(values["LINE_TARGET_ID"]):
        raise ConfigError("LINE_TARGET_ID must be U/C/R followed by 32 lowercase hex digits")

    supplied_url = args.server_url if args.server_url is not None else os.environ.get("LINE_SERVER_URL")
    url = supplied_url if supplied_url is not None else old_header["LINE_SERVER_URL"] or detect_url(args.port)
    if interactive and supplied_url is None and (not old_header["LINE_SERVER_URL"] or args.force_config):
        suggested = f" (detected: {url})" if url else ""
        url = prompt_value("ESP32-reachable server base URL" + suggested, url)
    url = validate_url(url)
    device_id = args.device_id if args.device_id is not None else old_header["LINE_DEVICE_ID"] or ""
    if not DEVICE_RE.fullmatch(device_id):
        raise ConfigError("--device-id must be 0-64 letters/digits or '.', '_', ':', '-'")
    ca = old_header["LINE_ROOT_CA"] or ""
    if args.ca_file:
        try:
            ca = args.ca_file.read_text(encoding="ascii")
            if "-----BEGIN CERTIFICATE-----" not in ca:
                raise ValueError("missing PEM marker")
            ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT).load_verify_locations(cadata=ca)
        except (OSError, ValueError, ssl.SSLError) as exc:
            raise ConfigError("--ca-file must be a readable valid PEM CA certificate") from exc
    if url.startswith("https://") and not ca:
        raise ConfigError("HTTPS requires --ca-file with the server root CA (or an existing LINE_ROOT_CA)")

    if not args.force_config:
        for name in KEYS:
            if old_env.get(name) and old_env[name] != values[name]:
                raise ConfigError(f"Existing {name} differs; use --force-config to update configuration")
        if header_text:
            for name, desired in (("LINE_API_KEY", values["DATALOGGER_API_KEY"]),
                                  ("LINE_SERVER_URL", url), ("LINE_DEVICE_ID", device_id), ("LINE_ROOT_CA", ca)):
                if old_header[name] is None or old_header[name] != desired:
                    raise ConfigError(f"Existing firmware {name} differs or cannot be read; use --force-config to regenerate")

    values["DATABASE_PATH"] = old_env.get("DATABASE_PATH") or "data/datalogger.sqlite3"
    values["LINE_HTTP_TIMEOUT_SECONDS"] = old_env.get("LINE_HTTP_TIMEOUT_SECONDS") or "6"
    try:
        timeout = float(values["LINE_HTTP_TIMEOUT_SECONDS"])
        if not 0 < timeout <= 30:
            raise ValueError
    except ValueError as exc:
        raise ConfigError("LINE_HTTP_TIMEOUT_SECONDS in .env must be >0 and <=30") from exc
    new_env = render_env(env_text, values)
    enabled = bool(url)
    new_header = "\n".join([
        "#pragma once", "// Generated by setup_project.sh. Local file: do not commit secrets.",
        f"#define LINE_ENABLED {'true' if enabled else 'false'}",
        f"#define LINE_SERVER_URL {json.dumps(url)}",
        f"#define LINE_API_KEY {json.dumps(values['DATALOGGER_API_KEY'])}",
        f"#define LINE_DEVICE_ID {json.dumps(device_id)}",
        "#define LINE_SEND_INTERVAL_MS 300000UL", "#define LINE_HTTP_TIMEOUT_MS 10000UL",
        "#define LINE_MAX_ATTEMPTS 4U", f"#define LINE_ROOT_CA {json.dumps(ca)}", "",
    ])
    # All checks precede writes, so a bad key/URL/CA leaves existing config intact.
    if not env_text or new_env != env_text:
        atomic_write(env_path, new_env)
    if not header_text or args.force_config:
        atomic_write(header_path, new_header)
    print(f"Server configuration: {env_path}")
    print(f"ESP32 configuration: {header_path}")
    print("API key stored in both files; it is not printed. Read DATALOGGER_API_KEY in .env for the dashboard.")
    if url:
        print(f"ESP32 server URL: {url}")
    else:
        print("ESP32 uplink is DISABLED: no LAN IP was detected. Rerun with --server-url URL --force-config.")
    missing = [name for name in ("LINE_CHANNEL_ACCESS_TOKEN", "LINE_TARGET_ID") if not values[name]]
    if missing:
        print("LINE is NOT READY: configure " + ", ".join(missing) + " before expecting messages.")
    else:
        print("LINE credentials are configured; connectivity/token validity has not been tested.")
    if header_text and not args.force_config:
        print("Existing firmware settings preserved (including its LINE_ENABLED and timing values).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--non-interactive", action="store_true")
    parser.add_argument("--force-config", action="store_true")
    parser.add_argument("--server-url")
    parser.add_argument("--device-id")
    parser.add_argument("--ca-file", type=Path)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    try:
        configure(args)
    except (ConfigError, OSError, UnicodeError) as exc:
        print(f"Configuration failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
