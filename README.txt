LuaAPI + LMM for Yuri's Revenge
===============================

What is in this folder
----------------------
LMM.exe      Mod manager: lists mods, toggles them on/off, launches the game
             and injects LuaAPI into the game process.
LuaAPI.dll   The LuaAPI 0.2.0 engine - runs mod scripts inside the game.
scripts\     What the engine loads:
             init.lua            entry point (the DLL resolves its own path)
mods\        The mods themselves:
             active_mods.txt     enabled mods, one ID per line (# = comment)
             <id>\main.lua       mod code (the folder name IS the mod ID)
             <id>\mod.json       optional: name, version, author, description,
                                 conflicts
docs\        API.md            Lua API reference: what a mod can call
             MOD_MANAGER.md    how LMM orders and validates mods

Requirements
------------
* Red Alert 2: Yuri's Revenge (gamemd.exe must be present). A clean install,
  an Ares/Phobos setup and the CnCNet client all work.
* Windows 7 or newer, 32-bit game (YR itself is 32-bit).

Installation
------------
1. Copy EVERYTHING in this folder (LMM.exe, LuaAPI.dll, scripts, mods, docs)
   into the game directory - the one that contains gamemd.exe. Nothing in the
   game is overwritten; these files are simply added next to it.
   Example: C:\Games\Red Alert 2 Yuri's Revenge\
2. Run LMM.exe from the game directory.

Important: LMM locates the game relative to itself (it checks for LuaAPI.dll
next to it), so it must be run from the game folder, not from this Desktop
folder.

Running
-------
Option A (easiest): in LMM press "Launch Game" - the launcher starts gamemd.exe
and injects LuaAPI automatically.

Option B: start the game as usual (shortcut, CnCNet client, Syringe with
Ares/Phobos), then press "Inject" in LMM. The "CnCNet" button starts the CnCNet
client and waits for the game process (gamemd-spawn.exe) to appear.

If injection reports "access denied", close LMM and start it as administrator
(right click -> "Run as administrator").

Mods
----
* Bundled: ltnk_emp_nerf (EMP nerf for Yuri's Lasher Tanks) and Smart AI.
* Layout: one folder per mod under mods\ - mods\<id>\main.lua plus an
  optional mods\<id>\mod.json. The folder name is the ID used everywhere
  (active_mods.txt, LMM, mod.json "conflicts"), so renaming a folder renames
  the mod. A folder without main.lua is ignored.
* Enable/disable: the "Mods" tab in LMM (checkbox on the left of a row), then
  "Apply". Or edit the lines in mods\active_mods.txt directly.
* New mod: create mods\<id>\ with main.lua (and mod.json if you want a nice
  card), then enable it in LMM and press "Apply".
* Order: drag a row up or down. The list order is written to active_mods.txt
  and is the load/update order inside the game.
* Conflicts: if two enabled mods name each other in mod.json "conflicts",
  LMM shows a warning. It never disables anything for you.
* After editing active_mods.txt by hand, press "Refresh" (F5) in LMM.
* Old layout: an install that still keeps mods in scripts\mods\ keeps
  working - the loader and LMM scan mods\ first and then scripts\mods\, and
  list each mod only once (mods\<id> wins over scripts\mods\<id>). New mods
  belong in mods\.

What appears after the first run
--------------------------------
* LuaAPI.log in the game folder - engine and mod log. A loaded mod logs
  "[LuaAPI] [+] Mod active: <id>"; a failing mod logs its own ID and the error
  ("[-] Failed to load mod: <id> ..." or "Mod '<id>' Update error: ..."), and
  the other mods keep running.
* injector_log.txt next to LMM.exe - what LMM did: which mod folders it
  scanned ("Mods: N found in ..."), injections, errors.
* injector.ini next to LMM.exe - interface settings only (language), created
  automatically.

Versions
--------
LMM          v0.4.1  (built 2026-10-25)
LuaAPI       0.2.0   (prebuilt binary dated 2026-10-19)

Build note: the bundled LuaAPI.dll was compiled before the API was renumbered
to 0.2.0, so Engine.version inside this binary still returns "2.0.0". All
sources and documentation have been renumbered to 0.2.0; the binary will report
0.2.0 once it is rebuilt. Rebuilding the DLL from the current tree is blocked by
two pre-existing source issues (the missing header src/dynamic_fow_poc.h and a
MarkBounty declaration/definition mismatch), which is why the verified
2026-10-19 build ships here. Neither issue affects mod loading: the DLL still
loads exactly one script, scripts\init.lua.

Uninstall
---------
Delete LMM.exe, LuaAPI.dll, the scripts\ and mods\ folders, LuaAPI.log,
injector_log.txt and injector.ini from the game folder. Game files themselves
are untouched.
