"""The builders no longer update themselves (docs/releases.md): the Longshot Launcher reads Longshot's signed releases, and
installs and updates each version's builder. What is left in the builders is command-line only, and none of it asks a server
for a release: no GitHub link, no release lookup, no game update check (host/hos_update.c is gone), and none of the old
self-update's hooks. The builders' windows, which had the version buttons, are gone too."""
import os
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

# (the code that used to ask GitHub for a release or install a newer builder, and must not now)
SOURCES = [
    "builder/linux/AppRun", "builder/linux/longshot-folder", "builder/linux/linux_tools.py", "builder/LongshotBuilder/Program.cs",
    "builder/LongshotBuilder/Runner.cs", "builder/setup/setup.c", "host/hos_main.c",
]


class NoLookup(unittest.TestCase):
    def test_no_builder_looks_for_a_release(self):
        for rel in SOURCES:
            text = (ROOT / rel).read_text(encoding="utf-8")
            for pattern in (r"api\.github\.com", r"github\.com/[^/\s\"]+/(Longshot|Hookshot)/releases", r"releases/latest",
                            r"NotRexed[/]Longshot", r"hos_update_check", r"LONGSHOT_LATEST_TAG", r"LONGSHOT_LINUX_LIST",
                            r"latest_tag", r"update.builder", r"--fetched", r"run_latest_setup"):
                self.assertIsNone(re.search(pattern, text), "%s: %s" % (rel, pattern))
        self.assertFalse((ROOT / "host" / "hos_update.c").exists(), "the game's own update check is back")

    def test_the_builders_have_no_window(self):
        for gone in ("builder/linux/longshot_builder.py", "builder/linux/linux_update.py", "builder/LongshotBuilder/MainWindow.xaml",
                     "builder/LongshotBuilder/App.xaml", "builder/LongshotBuilder/Updater.cs"):
            self.assertFalse((ROOT / gone).exists(), gone)
        project = (ROOT / "builder/LongshotBuilder/LongshotBuilder.csproj").read_text(encoding="utf-8")
        self.assertNotIn("UseWPF", project)
        self.assertNotIn("UseWindowsForms", project)


if __name__ == "__main__":
    os.environ.setdefault("PYTHONDONTWRITEBYTECODE", "1")
    unittest.main()
