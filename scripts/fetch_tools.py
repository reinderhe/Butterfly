"""(butterfly) The Windows build tools tools.lock names that a tools folder lacks, fetched as the Longshot builder fetches them.

Butterfly's build (scripts/build_ssr.bat) runs in a folder made from Hookshot Builder's tools with this repository's code over
them. Longshot builds with some newer tools (llvm-mingw, the CPM packages with dav1d): this downloads each windows-x64 tool of
tools.lock whose version the folder does not have yet, from the URLs the lock lists, in order, checks its size and SHA-256,
and unpacks it in place of the old one (the archive's <prefix> folder becomes <dest>, under the tools folder). Python itself,
which runs this, is left alone; so is emsdk while the folder has Hookshot's (the same version).

Usage: python -I scripts/fetch_tools.py <tools folder>   (tools.lock is read beside scripts/)
"""
import hashlib
import json
import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
LOCK = os.path.join(HERE, "..", "tools.lock")
SKIP = {"python"}   # (running this)
USER_AGENT = "longshot-builder-butterfly/1"   # (Cloudflare answers 403 to urllib's default agent, as magicbean_login.py says)


def lock_tools(system="windows-x64"):
    tools = []
    with open(LOCK, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 9 or parts[0] != "tool" or parts[3] != system:
                continue
            name, version, _, sha, size, dest, prefix = parts[1:8]
            tools.append({"name": name, "version": version, "sha256": sha.lower(), "size": int(size), "dest": dest,
                          "prefix": prefix, "urls": parts[8:]})
    return tools


def dest_dir(tools_dir, dest):
    """the lock's dest (LongshotBuilder/tools/<x>) in this tools folder (<tools folder>/<x>)"""
    rel = dest.replace("\\", "/")
    for head in ("LongshotBuilder/tools/", "HookshotBuilder/tools/"):
        if rel.startswith(head):
            rel = rel[len(head):]
    return os.path.join(tools_dir, *rel.split("/"))


def download(tool, into):
    for url in tool["urls"]:
        print("  %s %s: %s" % (tool["name"], tool["version"], url), flush=True)
        try:
            h = hashlib.sha256()
            n = 0
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT}), timeout=60) as r, open(into, "wb") as out:
                while True:
                    block = r.read(1 << 20)
                    if not block:
                        break
                    h.update(block)
                    out.write(block)
                    n += len(block)
            if n != tool["size"] or h.hexdigest() != tool["sha256"]:
                print("    not the file tools.lock names (%d bytes, sha256 %s): the next address" % (n, h.hexdigest()[:16]))
                continue
            return True
        except OSError as e:
            print("    failed (%s): the next address" % e)
    return False


def unpack(archive, prefix, dest):
    tmp = dest + ".new"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            name = info.filename.replace("\\", "/")
            if prefix != "-":
                if not name.startswith(prefix.rstrip("/") + "/"):
                    continue
                name = name[len(prefix.rstrip("/")) + 1:]
            if not name or name.startswith("/") or ".." in name.split("/"):
                continue
            target = os.path.join(tmp, *name.split("/"))
            if info.is_dir():
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out, 1 << 20)
    if os.path.exists(dest):
        shutil.rmtree(dest)   # (the older version of this tool, which this folder's builds no longer use)
    os.replace(tmp, dest)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    tools_dir = os.path.abspath(sys.argv[1])
    stamp_path = os.path.join(tools_dir, "tools_versions.json")
    try:
        with open(stamp_path, encoding="utf-8") as f:
            have = json.load(f)
    except (OSError, ValueError):
        have = {}
    bad = 0
    for tool in lock_tools():
        name, dest = tool["name"], dest_dir(tools_dir, tool["dest"])
        if name in SKIP or have.get(name) == tool["version"]:
            continue
        if "hookshot-v2.1.0" in tool["version"] and tool["version"].endswith("v2.1.0") and os.path.isdir(dest):
            have[name] = tool["version"]   # (Hookshot Builder's own, which this folder was made from)
            continue
        print("tools: %s %s" % (name, tool["version"]), flush=True)
        fd, archive = tempfile.mkstemp(suffix=".zip", dir=tools_dir)
        os.close(fd)
        try:
            if not download(tool, archive):
                if os.path.isdir(dest):
                    # (the older one this folder has builds too, most likely: said, and the build goes on with it)
                    print("tools: %s could not be fetched from any address; the build uses the older one this folder has" % name)
                else:
                    print("tools: %s could not be fetched from any address" % name)
                    bad += 1
                continue
            unpack(archive, tool["prefix"], dest)
            have[name] = tool["version"]
            with open(stamp_path, "w", encoding="utf-8") as f:
                json.dump(have, f, indent=1)
        finally:
            if os.path.exists(archive):
                os.remove(archive)
    with open(stamp_path, "w", encoding="utf-8") as f:
        json.dump(have, f, indent=1)
    print("tools: up to date" if not bad else "tools: %d could not be fetched" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
