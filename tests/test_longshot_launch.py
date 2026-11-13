"""longshot-launch (the AppImage's --play) starts the built game in bin/ and does not keep the builder's libraries on the path.

python -I -B tests/test_longshot_launch.py
"""
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest

# (Linux only)
RUN_ALL_SKIP = {"windows": "the Linux launcher, a POSIX shell script"}

ROOT = Path(__file__).resolve().parents[1]
LAUNCH = ROOT / "builder" / "linux" / "longshot-launch"


def game(folder):
    """A built folder: image.bin, the game in bin/ (it prints where it runs and what it was given), and a folder launcher
    beside it that must never be started"""
    (folder / "bin").mkdir(parents=True)
    (folder / "image.bin").write_bytes(b"image")
    exe = folder / "bin" / "longshot"
    exe.write_text(
        "#!/bin/sh\n"
        "pwd\n"
        "printf 'APPDIR=%s\\n' \"${APPDIR-UNSET}\"\n"
        "printf 'LD_LIBRARY_PATH=%s\\n' \"${LD_LIBRARY_PATH-UNSET}\"\n"
        "printf 'LONGSHOT_TOOLS=%s\\n' \"${LONGSHOT_TOOLS-UNSET}\"\n"
        "printf 'PYTHONPATH=%s\\n' \"${PYTHONPATH-UNSET}\"\n"
        "printf 'CHECK=%s\\n' \"${LONGSHOT_NO_UPDATE_CHECK-UNSET}\"\n",
        encoding="utf-8",
    )
    exe.chmod(exe.stat().st_mode | stat.S_IEXEC)
    launcher = folder / "longshot"
    launcher.write_text("#!/bin/sh\nprintf 'launcher\\n'\n", encoding="utf-8")
    launcher.chmod(launcher.stat().st_mode | stat.S_IEXEC)


class LaunchTests(unittest.TestCase):
    def run_launch(self, *args, home, cwd=None):
        e = os.environ.copy()
        e.pop("DISPLAY", None)
        e.pop("WAYLAND_DISPLAY", None)
        e.update(HOME=str(home), APPDIR="/from/the/appimage", APPIMAGE="/tmp/Longshot.AppImage", ARGV0="Longshot", OWD="/old",
                 LD_LIBRARY_PATH="/builder/lib", LONGSHOT_TOOLS="/builder/tools", PYTHONPATH="/builder/python")
        return subprocess.run([str(LAUNCH), *args], env=e, cwd=cwd, text=True, capture_output=True, check=False)

    def test_the_folder_starts_bin_longshot_without_the_builders_libraries(self):
        with tempfile.TemporaryDirectory(prefix="longshot-launch-") as tmp:
            folder = Path(tmp) / "100% Games"
            game(folder)
            ran = self.run_launch(str(folder), home=tmp)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        lines = ran.stdout.splitlines()
        self.assertEqual(lines[0], str(folder))
        for line in ("APPDIR=UNSET", "LD_LIBRARY_PATH=UNSET", "LONGSHOT_TOOLS=UNSET", "PYTHONPATH=UNSET", "CHECK=1"):
            self.assertIn(line, lines)
        self.assertNotIn("launcher", lines)

    def test_no_folder_is_games_longshot(self):
        with tempfile.TemporaryDirectory(prefix="longshot-launch-") as tmp:
            folder = Path(tmp) / "Games" / "Longshot"
            game(folder)
            ran = self.run_launch(home=tmp)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        self.assertEqual(ran.stdout.splitlines()[0], str(folder))

    def test_a_relative_folder(self):
        with tempfile.TemporaryDirectory(prefix="longshot-launch-") as tmp:
            folder = Path(tmp) / "100% Games"
            game(folder)
            for relative in ("100% Games", "./100% Games"):
                ran = self.run_launch(relative, home=tmp, cwd=tmp)
                self.assertEqual(ran.returncode, 0, ran.stderr)
                self.assertEqual(ran.stdout.splitlines()[0], str(folder))

    def test_no_game_is_an_error(self):
        with tempfile.TemporaryDirectory(prefix="longshot-launch-") as tmp:
            # (a folder with only its launcher, longshot, and no bin/longshot: never started, it would start this again)
            folder = Path(tmp) / "game"
            game(folder)
            (folder / "bin" / "longshot").unlink()
            for args in ((), (str(folder),)):
                ran = self.run_launch(*args, home=tmp)
                self.assertNotEqual(ran.returncode, 0)
                self.assertIn("no built game", ran.stderr)
                self.assertNotIn("launcher", ran.stdout)

    def test_play_is_the_builder_appimage(self):
        script = (ROOT / "scripts" / "linux" / "appimage.sh").read_text(encoding="utf-8")
        self.assertNotIn("Longshot-launcher-", script)
        apprun = (ROOT / "builder" / "linux" / "AppRun").read_text(encoding="utf-8")
        self.assertIn("longshot-launch", apprun)
        desktop = (ROOT / "builder" / "linux" / "longshot-builder.desktop").read_text(encoding="utf-8")
        self.assertIn("Actions=Play;", desktop)
        self.assertIn("Exec=AppRun --play\n", desktop)


if __name__ == "__main__":
    unittest.main()
