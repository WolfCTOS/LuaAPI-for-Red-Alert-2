"""Loader layout test: mods/<id>/ and the legacy scripts/mods/<id>/.

The DLL only ever loads <game>\\scripts\\init.lua; everything about mod
discovery lives in that script. This test runs the real init.lua through a
Lua runtime with the engine globals stubbed out and checks that:

  1. mods/<id>/main.lua is found and loaded (current layout);
  2. scripts/mods/<id>/main.lua still works (legacy installs);
  3. when a folder exists in both places, mods/<id> wins and nothing is
     loaded twice.

Requirements: Python 3 and `pip install lupa`.
Run from anywhere:  python tools/test_mod_layout.py
"""

import os
import shutil
import sys
import tempfile

try:
    from lupa import LuaRuntime
except ImportError:  # pragma: no cover - environment problem, not a code bug
    print("This test needs lupa: pip install lupa")
    sys.exit(2)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INIT_LUA = os.path.join(REPO, "scripts", "init.lua")
MODS = os.path.join(REPO, "mods")

# Two mods that load without touching the real engine.
CASES = ["bounty_hunter", "smart_ai"]


def write_active(dest):
    with open(os.path.join(dest, "active_mods.txt"), "w", encoding="utf-8") as f:
        f.write("# test active set\n")
        for mod in CASES:
            f.write(mod + "\n")


def build_staging(root, layout, marker=None):
    """layout: 'new' | 'legacy' | 'both'."""
    scripts = os.path.join(root, "scripts")
    os.makedirs(scripts)
    shutil.copy(INIT_LUA, os.path.join(scripts, "init.lua"))
    # Mods that use the shared helpers expect scripts/framework next to init.lua.
    framework = os.path.join(REPO, "scripts", "framework")
    if os.path.isdir(framework):
        shutil.copytree(framework, os.path.join(scripts, "framework"))

    def copy_mods(dest):
        os.makedirs(dest, exist_ok=True)
        for mod in CASES:
            shutil.copytree(os.path.join(MODS, mod), os.path.join(dest, mod))

    if layout in ("new", "both"):
        mods_dir = os.path.join(root, "mods")
        copy_mods(mods_dir)
        write_active(mods_dir)          # current layout: list lives with the mods
    if layout in ("legacy", "both"):
        copy_mods(os.path.join(scripts, "mods"))
        if layout == "legacy":
            write_active(scripts)       # legacy layout: the list sits in scripts\
        if marker:
            main = os.path.join(scripts, "mods", CASES[0], "main.lua")
            with open(main, encoding="utf-8") as f:
                body = f.read()
            with open(main, "w", encoding="utf-8") as f:
                f.write('print("%s")\n' % marker + body)
    return root


def run(root):
    lua = LuaRuntime(unpack_returned_tuples=True)
    logs = []

    def capture(*args):
        logs.append(" ".join(str(a) for a in args))

    lua.globals().capture = capture
    lua.execute(
        """
        print = function(...) capture(...) end
        local function autotable()
            return setmetatable({}, { __index = function() return function() return nil end end })
        end
        Engine = autotable(); World = autotable(); Input = autotable(); House = autotable()
        """
    )
    scripts = os.path.join(root, "scripts").replace("\\", "/")
    lua.execute('package.path = "%s/?.lua;" .. package.path' % scripts)
    lua.execute('dofile("%s/init.lua")' % scripts)
    lua.execute("OnTick(1) OnTick(2)")
    return logs


def check(label, root, marker=None):
    logs = run(root)
    active = [l for l in logs if "Mod active:" in l]
    failed = [l for l in logs if "Failed to load mod" in l]
    wrong_copy = [l for l in logs if marker and marker in l]
    ids = [l.rsplit("'", 2)[1] for l in active]
    ok = sorted(ids) == sorted(CASES) and not failed and not wrong_copy
    print("[%s] %s" % ("PASS" if ok else "FAIL", label))
    for line in logs:
        print("       |", line)
    if len(ids) != len(set(ids)):
        print("       | duplicate load detected")
    return ok


def main():
    tmp = tempfile.mkdtemp(prefix="luaapi-layout-")
    try:
        results = [
            check("mods/<id>", build_staging(os.path.join(tmp, "new"), "new")),
            check("scripts/mods/<id> (legacy)",
                  build_staging(os.path.join(tmp, "legacy"), "legacy")),
            check("both layouts, mods/ must win",
                  build_staging(os.path.join(tmp, "both"), "both", marker="LEGACY-COPY-RAN")),
        ]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = all(results)
    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
