# Beta Clean Machine Test

> **Status:** STATIC + ASSEMBLY PASS (2026-09-22) · **RUNTIME PENDING**
> (requires a user-executed first run — see protocol below).
> No code changed for this test. No Beta Gate claimed.

## Proposed setup (as executed)

Isolated dir `luaapi-clean-test/` (outside the repo/game install),
assembled WITHOUT touching the dev installation:

- `LuaAPI.dll` + `injector.exe` — current `build/Release` outputs
  (built from unmodified source; static `/MT`, so no VC-redist needed).
- `scripts/`, `API.md`, `README.md` — byte-identical from
  `git archive HEAD` (tracked files only).

Deliberately excluded (untracked/dev-only): `tools/`, `build/`,
`*.log`, `e1_error_probe/`, `bounty_hunter.zip`, game assets.

## Package/install surface under test (35 files)

`LuaAPI.dll`, `injector.exe`, `scripts/init.lua`,
`scripts/active_mods.txt` (committed default stack:
`target_reselect`, `bounty_hunter`, `smart_ai`), `scripts/framework/`
(10 files, required by smart_ai/target_reselect `require`s),
`scripts/mods/` (8 tracked mods incl. inactive M1/M2 probes and
diagnostics — loaded ONLY if listed in `active_mods.txt`),
`API.md`, `README.md`.

## Static results (all PASS)

1. **No dev-machine paths** in shipped scripts (grep for absolute
   paths/`tools/tmp`: only doc mentions in HOW_TO_USE texts, zero code
   dependencies).
2. **Requires resolve inside the package:** `framework.util` (the only
   runtime `require` in the default stack) ships in-package.
3. **Self-containment smoke:** headless run against the package copy —
   all 3 default mods `require` + `Update` cleanly with stubbed engine
   and zero dev-machine files (`SMOKE OK`, exit 0).
4. **Fresh-state behavior by construction:** no `LuaAPI.log`, no saved
   state, no config ships; logger creates the log next to the DLL;
   Gate 1.3 recreates the VM per match.
5. **Docs match the package:** README install tree
   (`LuaAPI.dll`/`injector.exe`/`scripts/`) is exactly what was
   assembled; mod-enable/create paths verified against `init.lua`.

## Why this is not yet a full PASS

A same-machine isolated dir cannot prove clean-machine behavior:
same OS/user/registry/redist, no virgin YR install, and the
interactive first run (inject → attach → VM init → skirmish →
menu → second match) was not executed here.

## Runtime protocol (user-executed)

1. Copy this package next to a fresh YR 1.001 install (no prior
   `LuaAPI.log`, no other mods).
2. Run `injector.exe`, start the game, start a skirmish.
3. Expect: welcome message, `LuaAPI.log` created next to the DLL,
   mod-loading lines for the 3 default mods, no errors.
4. Play, return to menu, start a second match (Gate 1.3 interplay).
5. PASS iff: first run works with zero manual fixes AND the second
   match behaves identically. Any manual step = record the exact
   mismatch (docs vs packaging vs environment vs implementation).
