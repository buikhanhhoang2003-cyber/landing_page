#!/usr/bin/env bash
# Set up the complete project from any current working directory.
set -euo pipefail
umask 077

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SERVER_DIR="$PROJECT_ROOT/esp-line-app-system/apps/line-dashboard"
FIRMWARE_DIR="$PROJECT_ROOT/smart_datalogger_v1/esp32_smartlogger"
SERVER_HOST="0.0.0.0"
SERVER_PORT="8000"
CONFIGURE_ONLY=0
SKIP_BUILD=0
NO_START=0
INSTALL_DEV=0
CONFIG_ARGS=()

usage() {
    cat <<'HELP'
Usage: bash setup_project.sh [options]

Configure server + ESP32, install Python dependencies, build ESP32, then run
FastAPI in the foreground. No board is flashed and no test LINE message is sent.
The STM32 source/CubeIDE project and documentation stay in the project folder.

  --configure-only       Create/check configuration only; no downloads or build
  --non-interactive      Never prompt; use existing config/environment/defaults
  --server-url URL       ESP32-reachable base URL, e.g. http://192.168.1.100:8000
  --port PORT            FastAPI port (default: 8000)
  --host HOST            FastAPI listen address (default: 0.0.0.0)
  --device-id ID         ESP32 name; empty/default uses its MAC address
  --ca-file FILE         PEM root CA for an HTTPS server URL
  --force-config        Regenerate managed config values from supplied inputs
  --skip-build           Skip PlatformIO installation and ESP32 compilation
  --no-start             Finish setup without starting FastAPI
  --install-dev          Also install server test dependencies
  -h, --help             Show this help

Secrets are read from prompts or environment variables, never command arguments:
DATALOGGER_API_KEY, LINE_CHANNEL_ACCESS_TOKEN, LINE_TARGET_ID, LINE_CHANNEL_SECRET.
LINE_SERVER_URL can also supply the server URL. Existing configurations are kept;
conflicting overrides require --force-config. Missing LINE credentials can be
filled later; the script clearly reports when LINE is not ready.

Prerequisite: Bash and Python 3.10+ with venv/pip support. No automatic sudo/apt.
HELP
}

need_value() {
    if [[ $# -lt 2 || ( -z "$2" && "$1" != "--device-id" ) || "$2" == --* ]]; then
        printf 'Missing value for %s\n' "$1" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --configure-only) CONFIGURE_ONLY=1; shift ;;
        --non-interactive|--force-config) CONFIG_ARGS+=("$1"); shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --no-start) NO_START=1; shift ;;
        --install-dev) INSTALL_DEV=1; shift ;;
        --server-url|--device-id|--ca-file)
            need_value "$@"; CONFIG_ARGS+=("$1" "$2"); shift 2 ;;
        --port) need_value "$@"; SERVER_PORT="$2"; shift 2 ;;
        --host) need_value "$@"; SERVER_HOST="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3.10+ is required. Install python3, python3-venv and python3-pip, then rerun.\n' >&2
    exit 1
fi
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else "Python 3.10+ is required")'
for required in "$SERVER_DIR/requirements.txt" "$FIRMWARE_DIR/platformio.ini" "$PROJECT_ROOT/scripts/configure_project.py"; do
    if [[ ! -f "$required" ]]; then
        printf 'Incomplete project: missing %s\n' "$required" >&2
        exit 1
    fi
done

python3 "$PROJECT_ROOT/scripts/configure_project.py" \
    --root "$PROJECT_ROOT" --host "$SERVER_HOST" --port "$SERVER_PORT" "${CONFIG_ARGS[@]}"
if [[ "$CONFIGURE_ONLY" == 1 ]]; then
    printf 'Configuration complete. No dependencies installed, firmware built, or server started.\n'
    exit 0
fi

SERVER_PYTHON="$SERVER_DIR/.venv/bin/python"
if [[ ! -x "$SERVER_PYTHON" ]]; then
    printf 'Creating server Python environment...\n'
    if ! python3 -m venv "$SERVER_DIR/.venv"; then
        printf 'Unable to create venv. On Ubuntu/Debian install python3-venv, then rerun.\n' >&2
        exit 1
    fi
fi
if [[ "$INSTALL_DEV" == 1 ]]; then
    REQUIREMENTS="$SERVER_DIR/requirements-dev.txt"
else
    REQUIREMENTS="$SERVER_DIR/requirements.txt"
fi
printf 'Installing server dependencies...\n'
"$SERVER_PYTHON" -m pip install --disable-pip-version-check -r "$REQUIREMENTS"

if [[ "$SKIP_BUILD" != 1 ]]; then
    if command -v pio >/dev/null 2>&1; then
        PIO_COMMAND=("$(command -v pio)")
    else
        PIO_PYTHON="$PROJECT_ROOT/.tools/platformio/.venv/bin/python"
        if [[ ! -x "$PIO_PYTHON" ]]; then
            python3 -m venv "$PROJECT_ROOT/.tools/platformio/.venv"
        fi
        "$PIO_PYTHON" -m pip install --disable-pip-version-check 'platformio>=6.1,<7'
        PIO_COMMAND=("$PIO_PYTHON" -m platformio)
    fi
    printf 'Building ESP32 firmware (no upload)...\n'
    "${PIO_COMMAND[@]}" run --project-dir "$FIRMWARE_DIR" -e esp32dev
fi

printf '\nSetup complete. STM32 CubeIDE project: %s\n' "$PROJECT_ROOT/smart_datalogger_v1/Script_STM32"
printf 'Use STM32CubeIDE separately to build/flash the STM32 firmware.\n'
if [[ "$NO_START" == 1 ]]; then
    printf 'Server not started. Run this script again, or follow the server README.\n'
    exit 0
fi
printf 'Starting FastAPI on %s:%s; Ctrl+C stops it.\n' "$SERVER_HOST" "$SERVER_PORT"
printf 'Dashboard on this machine: http://localhost:%s/\n' "$SERVER_PORT"
cd -- "$SERVER_DIR"
# The validated/generated .env is authoritative after setup. In particular,
# an exported empty value must not override the generated shared API key.
unset DATALOGGER_API_KEY LINE_CHANNEL_ACCESS_TOKEN LINE_TARGET_ID LINE_CHANNEL_SECRET LINE_GROUP_ID
exec "$SERVER_PYTHON" -m uvicorn main:app --host "$SERVER_HOST" --port "$SERVER_PORT"
