"""Offline build-bootstrap contracts; no Qt or third-party Python packages."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
POWERSHELL = shutil.which("powershell" if os.name == "nt" else "pwsh")
BASH = str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe") \
    if os.name == "nt" else "bash"


class BuildPinsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="pimio build pins ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for relative in (
            "cmake/PimioLore.cmake",
            "tools/build/qt.env",
            "tools/build/pins.sh",
            "tools/build/pins.ps1",
            "tools/local-build/linux/pinned.sh",
            "tools/local-build/windows/pinned.ps1",
        ):
            destination = self.root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)

    def run_command(self, command, success=True):
        environment = dict(os.environ, PIMIO_TEST_ROOT=self.root.as_posix())
        result = subprocess.run(
            command, env=environment, text=True, capture_output=True, timeout=30
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def bash_pins(self, success=True):
        return self.run_command(
            [BASH, "-c",
             'set -euo pipefail; source "$PIMIO_TEST_ROOT/tools/local-build/linux/pinned.sh"; '
             'pimio_assert_pins_match_repository "$PIMIO_TEST_ROOT"; '
             'printf "%s\\n" "$PIMIO_QT_VERSION" "$PIMIO_QT_MODULES" '
             '"$PIMIO_LORE_VERSION" "$PIMIO_AQTINSTALL_VERSION"'],
            success,
        ).splitlines()

    def windows_pins(self, success=True):
        if not POWERSHELL:
            self.skipTest("PowerShell not installed; Windows CI runs this with PowerShell 5.1")
        output = self.run_command(
            [POWERSHELL, "-NoProfile", "-NonInteractive", "-Command",
             "$ErrorActionPreference = 'Stop'; "
             ". (Join-Path $env:PIMIO_TEST_ROOT 'tools/local-build/windows/pinned.ps1'); "
             "Assert-PimioPinsMatchRepository -RepositoryRoot $env:PIMIO_TEST_ROOT; "
             "$PimioPinned | ConvertTo-Json -Depth 4 -Compress"],
            success,
        )
        return json.loads(output) if success else None

    def cmake_pins(self):
        output = self.root / "lore.json"
        self.run_command([
            "cmake", f"-DREPOSITORY_ROOT={self.root.as_posix()}",
            f"-DOUTPUT_FILE={output.as_posix()}", "-P",
            str(ROOT / "tests/build/lore_pins.cmake"),
        ])
        return json.loads(output.read_text())

    def edit(self, relative, old, new):
        path = self.root / relative
        text = path.read_text()
        self.assertIn(old, text)
        path.write_text(text.replace(old, new), newline="\n")

    def assert_windows_matches_cmake(self):
        actual = self.windows_pins()
        expected = self.cmake_pins()
        self.assertEqual(actual["LoreVersion"], expected["LoreVersion"])
        self.assertEqual(actual["LoreBaseUrl"], expected["LoreBaseUrl"])
        for bundle in actual["LoreBundles"]:
            self.assertEqual(bundle["Sha256"], expected[bundle["Bundle"]])

    def test_windows_checksums_match_evaluated_cmake(self):
        self.assert_windows_matches_cmake()

    def test_shell_and_powershell_read_same_product_pins(self):
        shell = self.bash_pins()
        windows = self.windows_pins()
        self.assertEqual(shell, [
            windows["QtVersion"], " ".join(windows["QtModules"]),
            windows["LoreVersion"], windows["AqtInstall"].split("==")[1],
        ])

    def test_qt_changes_propagate_without_editing_consumers(self):
        (self.root / "tools/build/qt.env").write_text(
            'PIMIO_QT_VERSION=9.8.7\nPIMIO_QT_MODULES="qtimageformats qtmultimedia qtsvg"\n'
            'PIMIO_AQTINSTALL_VERSION=9.8.6\n',
            newline="\n",
        )
        self.assertEqual(self.bash_pins()[0:2], ["9.8.7", "qtimageformats qtmultimedia qtsvg"])
        actual = self.windows_pins()
        self.assertEqual(actual["QtVersion"], "9.8.7")
        self.assertEqual(actual["QtModules"], ["qtimageformats", "qtmultimedia", "qtsvg"])
        self.assertEqual(actual["AqtInstall"], "aqtinstall==9.8.6")

    def test_lore_changes_select_only_current_version_table(self):
        version = self.cmake_pins()["LoreVersion"]
        self.edit("cmake/PimioLore.cmake", version, "9.8.7")
        module = self.root / "cmake/PimioLore.cmake"
        text = module.read_text()
        text = re.sub(r'(\|zip\|)[0-9a-f]{64}', r'\g<1>' + "a" * 64, text)
        text += '\nset(_pimio_lore_checksums_0.0.1\n' \
                '    "liblore|x86_64-pc-windows-msvc|zip|' + "b" * 64 + '"\n)\n'
        module.write_text(text, newline="\n")
        self.assertEqual(self.bash_pins()[2], "9.8.7")
        self.assert_windows_matches_cmake()

    def test_missing_qt_module_pin_fails_instead_of_reusing_environment(self):
        (self.root / "tools/build/qt.env").write_text(
            "PIMIO_QT_VERSION=9.8.7\nPIMIO_AQTINSTALL_VERSION=9.8.6\n",
            newline="\n",
        )
        self.bash_pins(success=False)
        self.windows_pins(success=False)

    def test_unknown_lore_version_fails_before_windows_downloads(self):
        version = self.cmake_pins()["LoreVersion"]
        self.edit("cmake/PimioLore.cmake", f'"{version}" CACHE', '"9.8.7" CACHE')
        self.windows_pins(success=False)

    def test_missing_or_malformed_windows_checksum_fails_closed(self):
        expected = self.cmake_pins()
        self.edit("cmake/PimioLore.cmake", expected["liblore"], "not-a-checksum")
        self.windows_pins(success=False)

    def test_container_receives_derived_pins_and_shared_build_context(self):
        output = self.run_command([
            BASH, "-c",
            'set -euo pipefail; source "$PIMIO_TEST_ROOT/tools/local-build/linux/pinned.sh"; '
            'fake_engine() { printf "%s\\n" "$@"; }; '
            'pimio_prepare_image fake_engine test-image build "$PIMIO_TEST_ROOT"',
        ]).splitlines()
        pins = self.bash_pins()
        self.assertIn(f"QT_VERSION={pins[0]}", output)
        self.assertIn(f"QT_MODULES={pins[1]}", output)
        self.assertIn(f"AQTINSTALL_VERSION={pins[3]}", output)
        self.assertEqual(output[-1], self.root.as_posix() + "/tools/build")


class BuildWiringTests(unittest.TestCase):
    def test_metrics_include_build_sources_but_not_generated_output(self):
        from tools.metrics.generate_repo_metrics import detect_language, is_supported_path

        for relative in ("tools/build/pins.sh", "tools/build/pins.ps1", "tests/build/lore_pins.cmake"):
            self.assertTrue(is_supported_path(ROOT / relative))
        for relative in ("build/default/CMakeCache.txt", "install/bin/pimio",
                         "tests/build/__pycache__/test_build_contracts.pyc"):
            self.assertFalse(is_supported_path(ROOT / relative))
        self.assertEqual(detect_language(ROOT / "tools/build/qt.env"), "Shell")
        self.assertEqual(detect_language(ROOT / "tools/local-build/linux/Containerfile"), "Docker")

    def test_workflows_consume_shared_pins_and_packages(self):
        for workflow in ("ci.yml", "release.yml"):
            with self.subTest(workflow=workflow):
                text = (ROOT / ".github/workflows" / workflow).read_text()
                self.assertIn("source tools/build/pins.sh", text)
                self.assertIn("modules: ${{ env.PIMIO_QT_MODULES }}", text)
                self.assertIn("version: ${{ env.PIMIO_QT_VERSION }}", text)
                self.assertIn("xargs -a tools/build/linux-packages.txt", text)
                self.assertIn("python -m unittest discover -s tests/build -v", text)
                self.assertNotRegex(text, r"PIMIO_(?:QT|LORE)_VERSION:\s*[0-9]")
                self.assertNotIn("qt_modules:", text)

    def test_linux_common_packages_cover_codec_build_requirements(self):
        packages = (ROOT / "tools/build/linux-packages.txt").read_text().splitlines()
        # Codec tools plus Qt Multimedia's PulseAudio runtime SONAME, which clean
        # containers lack even though GitHub-hosted runners often ship it.
        self.assertTrue(
            {"nasm", "perl", "ninja-build", "libpulse0"}.issubset(packages)
        )
        self.assertEqual(len(packages), len(set(packages)))
        for package in packages:
            self.assertRegex(package, r"^[a-z0-9][a-z0-9+.-]*$")
        container = (ROOT / "tools/local-build/linux/Containerfile").read_text()
        self.assertIn("COPY linux-packages.txt /tmp/pimio-linux-packages.txt", container)
        self.assertIn("xargs -a /tmp/pimio-linux-packages.txt", container)
        self.assertNotRegex(container, r"ARG (?:QT_\w+|AQTINSTALL_VERSION|UBUNTU_\w+)=")

    def test_ci_exercises_actual_local_linux_entrypoint(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("bash tools/local-build/linux/build.sh --engine docker", text)

    def test_exiftool_is_pinned_and_shared_by_every_context(self):
        module = (ROOT / "cmake/PimioExifTool.cmake").read_text()
        self.assertRegex(module, r'PIMIO_EXIFTOOL_VERSION "[0-9]+\.[0-9]+"')
        self.assertRegex(module, r'PIMIO_EXIFTOOL_SHA256\s+"[0-9a-f]{64}"')
        self.assertIn("URL_HASH", module)
        self.assertIn("find_package(Perl REQUIRED)", module)
        self.assertIn("include(PimioExifTool)", (ROOT / "CMakeLists.txt").read_text())
        self.assertIn("pimio_acquire_exiftool()", (ROOT / "CMakeLists.txt").read_text())
        self.assertIn("perl", (ROOT / "tools/build/linux-packages.txt").read_text().splitlines())
        self.assertIn("PerlVersion", (
            ROOT / "tools/local-build/windows/pinned.ps1").read_text())


if __name__ == "__main__":
    unittest.main()
