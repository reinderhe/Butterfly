"""Which game a build is for. Hookshot was written for The Legend of Zelda: Ocarina of Time; this fork adds Nintendo
Switch Sports Resort. Everything that only fits one game (Ocarina of Time's patches, its hot-function list and profile,
its chunk list) asks here first.

The game is found from the build id of the dump's main module and kept in the work folder's game.txt, and passed to
the tools the build starts in HOOKSHOT_GAME.
"""
import os

GAMES = {
    "oot": {
        "title": "The Legend of Zelda: Ocarina of Time",
        "program_id": "0400486024DEC000",
        "build_id": "e0f3ccfa",
    },
    "ssr": {
        "title": "Nintendo Switch Sports Resort",
        "program_id": "04008E001F098000",
        "build_id": "3941550a",
    },
}

ENV = "HOOKSHOT_GAME"


def from_build_id(build_id):
    """The game whose supported build this is, or None"""
    for name, g in GAMES.items():
        if build_id.lower().startswith(g["build_id"]):
            return name
    return None


def supported_builds():
    return ", ".join("%s (%s)" % (g["build_id"], g["title"]) for g in GAMES.values())


def current(work=None):
    """The game this build is for: HOOKSHOT_GAME, else the work folder's game.txt, else Ocarina of Time"""
    name = os.environ.get(ENV, "").strip().lower()
    if name in GAMES:
        return name
    if work:
        try:
            with open(os.path.join(work, "game.txt"), encoding="utf-8") as f:
                name = f.read().strip().lower()
        except OSError:
            name = ""
        if name in GAMES:
            return name
    return "oot"


def remember(work, name):
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, "game.txt"), "w", encoding="utf-8") as f:
        f.write(name + "\n")
    os.environ[ENV] = name


def is_oot(work=None):
    return current(work) == "oot"
