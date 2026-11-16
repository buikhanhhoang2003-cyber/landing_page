"""Offline acceptance tests for the full-project installer and setup helper.

Run from project_datalogger with: python3 -m unittest discover -s tests -v
No dependency downloads, firmware upload, server startup or LINE calls occur.
"""
from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
SERVER = Path("esp-line-app-system/apps/line-dashboard")
FIRMWARE = Path("smart_datalogger_v1/esp32_smartlogger")
STM32 = Path("smart_datalogger_v1/Script_STM32")
CONFIG_ENV = (
    "DATALOGGER_API_KEY", "LINE_CHANNEL_ACCESS_TOKEN", "LINE_TARGET_ID",
    "LINE_GROUP_ID", "LINE_CHANNEL_SECRET", "LINE_SERVER_URL",
)


def environment(**values):
    result = os.environ.copy()
    for name in CONFIG_ENV:
        result.pop(name, None)
    result.update(values)
    return result


def run(*command, cwd=None, env=None):
    return subprocess.run(
        command, cwd=cwd, env=env or environment(), text=True,
        capture_output=True, timeout=60,
    )


def env_value(path, name):
    match = re.search(rf"^{re.escape(name)}=(.*)$", path.read_text(), re.MULTILINE)
    if not match:
        raise AssertionError(f"Missing {name} in configuration")
    return match.group(1).strip().strip("\"'")


def cpp_value(path, name):
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(\".*\")\s*$", path.read_text(), re.MULTILINE)
    if not match:
        raise AssertionError(f"Missing {name} in firmware configuration")
    return json.loads(match.group(1))


class ProjectSetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="datalogger setup tests ")
        cls.base = Path(cls.temp.name)
        cls.fixture = cls.base / "source fixture"
        shutil.copytree(
            PROJECT, cls.fixture,
            ignore=shutil.ignore_patterns(
                ".git", ".venv", ".pio", ".metadata", ".metadata_backup",
                ".pytest_cache", "__pycache__", ".tools", "dist", "Debug", "Release",
                ".env", "line_config.h",
            ),
        )
        # Fixture-only secrets prove exclusion without altering user files.
        (cls.fixture / SERVER / ".env").write_text("DATALOGGER_API_KEY=DO_NOT_PACKAGE_TEST_KEY\n")
        (cls.fixture / FIRMWARE / "include/line_config.h").write_text('#define LINE_API_KEY "DO_NOT_PACKAGE_TEST_KEY"\n')
        cache = cls.fixture / SERVER / "data"
        cache.mkdir(exist_ok=True)
        (cache / "datalogger.sqlite3").write_bytes(b"fixture-private-records")
        cls.installer = cls.base / "full installer.sh"
        built = run(sys.executable, str(cls.fixture / "scripts/build_installer.py"), "--output", str(cls.installer))
        if built.returncode:
            raise AssertionError(f"Installer generation failed: {built.stdout}\n{built.stderr}")
        cls.extracted = cls.base / "extracted project"
        result = run("bash", str(cls.installer), "--directory", str(cls.extracted), "--extract-only")
        if result.returncode:
            raise AssertionError(f"Extraction failed: {result.stdout}\n{result.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fresh_project(self):
        destination = Path(tempfile.mkdtemp(prefix="configure ", dir=self.base)) / "project"
        shutil.copytree(self.extracted, destination)
        return destination

    def configure(self, project, *extra, **env):
        return run(
            "bash", str(project / "setup_project.sh"), "--configure-only", "--non-interactive",
            "--server-url", "http://192.168.1.20:8000", *extra,
            cwd=self.base, env=environment(**env),
        )

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, f"{result.stdout}\n{result.stderr}")

    def test_installer_and_setup_help(self):
        self.assert_success(run("bash", "-n", str(self.installer)))
        self.assert_success(run("bash", str(self.installer), "--help"))
        self.assert_success(run("bash", str(self.extracted / "setup_project.sh"), "--help"))

    def test_default_installer_only_creates_source(self):
        destination = self.base / "default extraction"
        result = run("bash", str(self.installer), "--directory", str(destination))
        self.assert_success(result)
        self.assertTrue((destination / FIRMWARE / "src/line_uplink.cpp").is_file())
        self.assertFalse((destination / SERVER / ".env").exists())
        self.assertFalse((destination / SERVER / ".venv").exists())
        self.assertFalse((destination / FIRMWARE / "include/line_config.h").exists())

    def test_setup_opt_in_forwards_configuration_arguments(self):
        destination = self.base / "opt in configure"
        result = run(
            "bash", str(self.installer), "--directory", str(destination), "--setup", "--",
            "--configure-only", "--non-interactive", "--server-url", "http://192.168.1.20:8000",
        )
        self.assert_success(result)
        self.assertTrue((destination / SERVER / ".env").is_file())
        self.assertEqual(cpp_value(destination / FIRMWARE / "include/line_config.h", "LINE_SERVER_URL"), "http://192.168.1.20:8000")
        self.assertFalse((destination / SERVER / ".venv").exists())

    def test_complete_source_and_cube_metadata_survive(self):
        for file in (
            SERVER / "main.py", SERVER / ".env.example", SERVER / "tests/test_contract.py",
            FIRMWARE / "src/main.cpp", FIRMWARE / "src/line_uplink.cpp",
            FIRMWARE / "include/line_config.example.h", FIRMWARE / "platformio.ini",
            STM32 / ".project", STM32 / ".cproject", STM32 / ".mxproject",
            STM32 / ".settings/stm32cubeide.project.prefs", STM32 / "Script_STM32.ioc",
            STM32 / "STM32F103RETX_FLASH.ld", STM32 / "Core/Src/main.c",
            STM32 / "Drivers/STM32F1xx_HAL_Driver/LICENSE.txt",
            Path("smart_datalogger_v1/Schematic .pdf"), Path("PROJECT_MANIFEST.json"),
        ):
            self.assertTrue((self.extracted / file).is_file(), str(file))

    def test_private_config_and_generated_files_are_excluded(self):
        for file in (SERVER / ".env", SERVER / "data/datalogger.sqlite3", FIRMWARE / "include/line_config.h"):
            self.assertFalse((self.extracted / file).exists(), str(file))
        for item in self.extracted.rglob("*"):
            self.assertNotIn(item.name, {".git", ".venv", ".pio", ".metadata", ".metadata_backup", "__pycache__", ".pytest_cache"})

    def test_manifest_matches_extracted_files(self):
        manifest = json.loads((self.extracted / "PROJECT_MANIFEST.json").read_text())
        self.assertEqual(manifest["format"], 1)
        files = {file.relative_to(self.extracted).as_posix() for file in self.extracted.rglob("*") if file.is_file()}
        self.assertEqual(set(manifest["files"]), files - {"PROJECT_MANIFEST.json"})
        for name, checksum in manifest["files"].items():
            self.assertEqual(hashlib.sha256((self.extracted / name).read_bytes()).hexdigest(), checksum, name)

    def test_legacy_wifi_literals_are_redacted_only_in_bundle(self):
        manifest = json.loads((self.extracted / "PROJECT_MANIFEST.json").read_text())
        for name in ("htpp_server_test.c", "version_1.c"):
            relative = FIRMWARE / "test" / name
            self.assertIn(relative.as_posix(), manifest["sanitized_legacy_wifi_files"])
            content = (self.extracted / relative).read_text()
            self.assertIn('"YOUR_WIFI_SSID"', content)
            self.assertIn('"YOUR_WIFI_PASSWORD"', content)
            # The builder must not modify the source it reads.
            self.assertEqual((self.fixture / relative).read_bytes(), (PROJECT / relative).read_bytes())

    def test_nonempty_destination_is_preserved(self):
        destination = self.base / "existing project"
        destination.mkdir()
        sentinel = destination / "keep.txt"
        sentinel.write_text("user work")
        result = run("bash", str(self.installer), "--directory", str(destination), "--extract-only")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_text(), "user work")
        self.assertEqual(list(destination.iterdir()), [sentinel])

    def test_destination_symlink_is_rejected(self):
        directory = self.base / "real destination"
        directory.mkdir()
        link = self.base / "symlink destination"
        link.symlink_to(directory, target_is_directory=True)
        result = run("bash", str(self.installer), "--directory", str(link), "--extract-only")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(directory.iterdir()), [])

    def test_corrupt_archive_is_rejected_before_source_is_installed(self):
        data = bytearray(self.installer.read_bytes())
        # Change decoded payload bytes, not the unused padding bits at the end
        # of a base64 stream (which can leave the compressed archive unchanged).
        marker = b"\n__PROJECT_DATALOGGER_PAYLOAD__\n"
        index = data.index(marker) + len(marker)
        data[index] = ord("A") if data[index] != ord("A") else ord("B")
        corrupt = self.base / "corrupt installer.sh"
        corrupt.write_bytes(data)
        destination = self.base / "corrupt extraction"
        result = run("bash", str(corrupt), "--directory", str(destination), "--extract-only")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((destination / "setup_project.sh").exists())

    def test_configure_only_creates_matching_key_and_is_idempotent(self):
        project = self.fresh_project()
        self.assert_success(self.configure(project))
        env_path = project / SERVER / ".env"
        firmware_path = project / FIRMWARE / "include/line_config.h"
        key = env_value(env_path, "DATALOGGER_API_KEY")
        self.assertGreaterEqual(len(key), 32)
        self.assertEqual(cpp_value(firmware_path, "LINE_API_KEY"), key)
        self.assertEqual(cpp_value(firmware_path, "LINE_SERVER_URL"), "http://192.168.1.20:8000")
        self.assertEqual(env_path.stat().st_mode & 0o077, 0)
        before = (env_path.read_bytes(), firmware_path.read_bytes())
        result = self.configure(project)
        self.assert_success(result)
        self.assertEqual(before, (env_path.read_bytes(), firmware_path.read_bytes()))
        self.assertNotIn(key, result.stdout + result.stderr)
        self.assertFalse((project / SERVER / ".venv").exists())

    def test_secret_values_are_data_and_not_echoed_or_executed(self):
        project = self.fresh_project()
        sentinel = self.base / "must-not-exist"
        token = f"literal$(touch '{sentinel}')`printf bad`quoted'value"
        result = self.configure(
            project,
            LINE_CHANNEL_ACCESS_TOKEN=token,
            LINE_TARGET_ID="U0123456789abcdef0123456789abcdef",
        )
        self.assert_success(result)
        self.assertFalse(sentinel.exists())
        self.assertNotIn(token, result.stdout + result.stderr)
        self.assertIn("$(touch", (project / SERVER / ".env").read_text())
        self.assertNotIn(token, (project / FIRMWARE / "include/line_config.h").read_text())

    def test_conflicting_existing_api_key_fails_without_modification(self):
        project = self.fresh_project()
        self.assert_success(self.configure(project))
        env_path = project / SERVER / ".env"
        firmware_path = project / FIRMWARE / "include/line_config.h"
        current = firmware_path.read_text()
        firmware_path.write_text(re.sub(r'^#define LINE_API_KEY .*$', '#define LINE_API_KEY "different-key"', current, flags=re.MULTILINE))
        before = (env_path.read_bytes(), firmware_path.read_bytes())
        result = self.configure(project)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(before, (env_path.read_bytes(), firmware_path.read_bytes()))

    def test_invalid_url_is_rejected_without_writing_config(self):
        project = self.fresh_project()
        result = run(
            "bash", str(project / "setup_project.sh"), "--configure-only", "--non-interactive",
            "--server-url", "ftp://192.168.1.20", env=environment(),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((project / SERVER / ".env").exists())
        self.assertFalse((project / FIRMWARE / "include/line_config.h").exists())

    def test_whitespace_key_cannot_create_enabled_but_unusable_config(self):
        project = self.fresh_project()
        result = self.configure(project, DATALOGGER_API_KEY="   ")
        if result.returncode == 0:
            key = env_value(project / SERVER / ".env", "DATALOGGER_API_KEY")
            self.assertTrue(key.strip())
            self.assertEqual(cpp_value(project / FIRMWARE / "include/line_config.h", "LINE_API_KEY"), key)
        else:
            self.assertFalse((project / SERVER / ".env").exists())
            self.assertFalse((project / FIRMWARE / "include/line_config.h").exists())

    def test_unsupported_cpp_string_concatenation_is_not_misread(self):
        project = self.fresh_project()
        self.assert_success(self.configure(project))
        firmware_path = project / FIRMWARE / "include/line_config.h"
        content = firmware_path.read_text()
        content = re.sub(r'^(#define LINE_API_KEY .*?)$', r'\1 "suffix"', content, flags=re.MULTILINE)
        firmware_path.write_text(content)
        before = (project / SERVER / ".env").read_bytes()
        result = self.configure(project)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(firmware_path.read_text(), content)
        self.assertEqual((project / SERVER / ".env").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
