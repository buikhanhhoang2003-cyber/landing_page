#!/usr/bin/env python3
"""Bundle the complete source workspace as one relocatable Bash installer."""
import argparse
import base64
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_DIRS = {
    ".git", ".venv", "venv", "env", "myenv", ".pio", ".tools", ".metadata",
    ".metadata_backup", ".pytest_cache", "__pycache__", ".vscode", ".idea",
    "dist", "build", "Debug", "Release", "data", "node_modules",
}
EXCLUDED_SUFFIXES = {
    ".pyc", ".pyo", ".log", ".sqlite3", ".db", ".key", ".pem", ".backup",
    ".o", ".elf", ".map", ".bin", ".su", ".cyclo",
}
LEGACY_WIFI_FILES = {
    "smart_datalogger_v1/esp32_smartlogger/test/htpp_server_test.c",
    "smart_datalogger_v1/esp32_smartlogger/test/version_1.c",
}
REQUIRED_FILES = (
    "setup_project.sh", "scripts/configure_project.py", "README.md",
    "esp-line-app-system/apps/line-dashboard/main.py",
    "esp-line-app-system/apps/line-dashboard/.env.example",
    "smart_datalogger_v1/esp32_smartlogger/src/main.cpp",
    "smart_datalogger_v1/esp32_smartlogger/src/line_uplink.cpp",
    "smart_datalogger_v1/Script_STM32/.project",
    "smart_datalogger_v1/Script_STM32/.cproject",
    "smart_datalogger_v1/Script_STM32/Script_STM32.ioc",
    "smart_datalogger_v1/Script_STM32/Core/Src/main.c",
)

BOOTSTRAP = r'''import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import sys
import tarfile
import tempfile

def main():
    if sys.version_info < (3, 10):
        raise ValueError("Can Python 3.10 tro len.")
    args = sys.argv[2:]
    split = args.index("--") if "--" in args else len(args)
    forwarded = args[split + 1:] if split < len(args) else []
    parser = argparse.ArgumentParser(
        description="Tao lai full project STM32 + ESP32 + FastAPI o vi tri moi. Mac dinh chi giai nen.",
        epilog="Vi du: bash installer.sh --directory ~/datalogger; them --setup de cai/build/chay.",
    )
    parser.add_argument("--directory", default="project_datalogger", help="Thu muc dich moi/rong")
    parser.add_argument("--setup", action="store_true", help="Sau khi giai nen: cai thu vien, build ESP32, chay FastAPI")
    parser.add_argument("--extract-only", action="store_true", help="Chi tao source, khong cai/build/chay")
    options = parser.parse_args(args[:split])
    if options.extract_only and options.setup:
        raise ValueError("Chon --setup hoac --extract-only, khong dung ca hai.")
    if forwarded and not options.setup:
        raise ValueError("Them --setup khi truyen tham so setup sau --.")

    target = Path(options.directory).expanduser().absolute()
    if target.is_symlink():
        raise ValueError("Khong giai nen vao symlink; chon thu muc moi.")
    if target.exists() and (not target.is_dir() or any(target.iterdir())):
        raise ValueError(f"Thu muc dich da co du lieu: {target}. Dung --directory voi thu muc moi.")
    marker = b"\n__PROJECT_DATALOGGER_PAYLOAD__\n"
    parts = Path(sys.argv[1]).read_bytes().split(marker, 1)
    if len(parts) != 2:
        raise ValueError("Installer thieu payload.")
    payload = base64.b64decode(b"".join(parts[1].split()), validate=True)
    if hashlib.sha256(payload).hexdigest() != "@@PAYLOAD_SHA256@@":
        raise ValueError("Checksum payload khong dung; file installer bi hong.")

    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".datalogger-install-", dir=target.parent) as work:
        staged = Path(work) / "project_datalogger"
        staged.mkdir()
        extracted = set()
        with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
            for member in archive.getmembers():
                path = PurePosixPath(member.name)
                if (path.is_absolute() or ".." in path.parts or not path.parts
                        or path.parts[0] != "project_datalogger" or len(path.parts) < 2):
                    raise ValueError("Archive co duong dan khong hop le.")
                relative = PurePosixPath(*path.parts[1:])
                if not member.isfile() or relative.as_posix() in extracted:
                    raise ValueError("Archive chi duoc chua file thuong, khong trung duong dan.")
                destination = staged.joinpath(*relative.parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, destination.open("xb") as output:
                    while chunk := source.read(1024 * 1024):
                        output.write(chunk)
                destination.chmod(0o755 if member.mode & 0o111 else 0o644)
                extracted.add(relative.as_posix())
        manifest = json.loads((staged / "PROJECT_MANIFEST.json").read_text())
        expected = manifest["files"]
        if set(expected) != extracted - {"PROJECT_MANIFEST.json"}:
            raise ValueError("Danh sach file khong khop manifest.")
        for name, checksum in expected.items():
            if hashlib.sha256((staged / name).read_bytes()).hexdigest() != checksum:
                raise ValueError(f"Checksum file khong dung: {name}")
        if not (staged / "setup_project.sh").is_file():
            raise ValueError("Archive thieu setup_project.sh.")
        # Check again immediately before publishing the staged tree.
        if target.is_symlink():
            raise ValueError("Thu muc dich da thay doi thanh symlink.")
        if target.exists():
            target.rmdir()  # Refuses a directory populated during extraction.
        staged.rename(target)

    print(f"Da tao full project: {target}", flush=True)
    if not options.setup:
        print(f"Buoc tiep theo: bash {str(target / 'setup_project.sh')!r}")
        return
    # stdin held our Python heredoc. Restore the user's original terminal/pipe
    # before handing off so setup prompts work in the standalone installer.
    os.dup2(3, 0)
    os.close(3)
    os.execvp("bash", ["bash", str(target / "setup_project.sh"), *forwarded])

try:
    main()
except (OSError, ValueError, KeyError, tarfile.TarError) as error:
    print(f"Installer: {error}", file=sys.stderr)
    sys.exit(1)
'''


def include_file(path: Path) -> bool:
    name = path.name
    if name == "PROJECT_MANIFEST.json" or name == "line_config.h":
        return False
    if (name == ".env" or name.startswith(".env.")) and name != ".env.example":
        return False
    if name in {".DS_Store", "Thumbs.db"} or path.suffix in EXCLUDED_SUFFIXES:
        return False
    return not name.endswith((".sqlite3-wal", ".sqlite3-shm", ".db-wal", ".db-shm"))


def build(output: Path) -> tuple[int, int]:
    files = {}
    sanitized = []
    archive_bytes = io.BytesIO()
    output = output.resolve()
    if output.exists() and not output.read_bytes().startswith(b"#!/usr/bin/env bash\n# Smart Datalogger source installer."):
        raise ValueError("Output da ton tai va khong phai installer do script nay tao; chon duong dan khac")
    with tarfile.open(fileobj=archive_bytes, mode="w", format=tarfile.PAX_FORMAT) as archive:
        def add(name: str, data: bytes, executable: bool = False):
            info = tarfile.TarInfo("project_datalogger/" + name)
            info.size = len(data)
            info.mode = 0o755 if executable else 0o644
            info.mtime = 0
            archive.addfile(info, io.BytesIO(data))

        for directory, dirs, names in os.walk(ROOT, followlinks=False):
            dirs[:] = sorted(d for d in dirs if d not in EXCLUDED_DIRS)
            for name in dirs:
                if (Path(directory) / name).is_symlink():
                    raise ValueError(f"Source co symlink; thay bang source thuong: {Path(directory) / name}")
            for name in sorted(names):
                path = Path(directory) / name
                if not include_file(path) or path.resolve() == output:
                    continue
                if path.is_symlink() or not path.is_file():
                    raise ValueError(f"Khong dong goi symlink/file dac biet: {path}")
                relative = path.relative_to(ROOT).as_posix()
                data = path.read_bytes()
                if relative in LEGACY_WIFI_FILES:
                    pattern = rb'(\b(?:WIFI_SSID|WIFI_PASSWORD|SETUP_AP_SSID|SETUP_AP_PASSWORD)\s*=\s*)"(?:\\.|[^"\\])*"'
                    data, count = re.subn(
                        pattern,
                        lambda match: match[1] + (b'"YOUR_WIFI_SSID"' if b"SSID" in match[1] else b'"YOUR_WIFI_PASSWORD"'),
                        data,
                    )
                    if count:
                        sanitized.append(relative)
                files[relative] = hashlib.sha256(data).hexdigest()
                add(relative, data, bool(path.stat().st_mode & stat.S_IXUSR) or relative == "setup_project.sh")
        missing = set(REQUIRED_FILES) - files.keys()
        if missing:
            raise ValueError("Source thieu file bat buoc: " + ", ".join(sorted(missing)))
        manifest = {
            "format": 1,
            "files": files,
            "sanitized_legacy_wifi_files": sanitized,
            "excluded_directories": sorted(EXCLUDED_DIRS),
            "note": "Source snapshot; local credentials, runtime data and generated build files are excluded.",
        }
        add("PROJECT_MANIFEST.json", (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode())
    payload = gzip.compress(archive_bytes.getvalue(), mtime=0)
    digest = hashlib.sha256(payload).hexdigest()
    bootstrap = BOOTSTRAP.replace("@@PAYLOAD_SHA256@@", digest)
    header = (
        "#!/usr/bin/env bash\n# Smart Datalogger source installer. Generated by scripts/build_installer.py.\nset -euo pipefail\n"
        "command -v python3 >/dev/null 2>&1 || { echo 'Can cai Python 3.10+ truoc.' >&2; exit 1; }\n"
        "exec python3 - \"$0\" \"$@\" 3<&0 <<'PROJECT_INSTALLER_PY'\n"
        + bootstrap + "PROJECT_INSTALLER_PY\nexit 1\n__PROJECT_DATALOGGER_PAYLOAD__\n"
    ).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    # Replace the output atomically, never leave a partly generated installer.
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".installer-", delete=False) as handle:
        temporary = Path(handle.name)
        try:
            handle.write(header)
            handle.write(base64.encodebytes(payload))
            handle.flush()
            temporary.chmod(0o755)
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
    return len(files), output.stat().st_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/project_datalogger_installer.sh")
    args = parser.parse_args()
    try:
        count, size = build(args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Build installer: {error}\n")
    print(f"Installer: {args.output.resolve()} ({count} source files, {size / 1048576:.1f} MiB)")


if __name__ == "__main__":
    main()
