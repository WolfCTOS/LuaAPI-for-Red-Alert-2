# Current Task

> Single source of truth for the task currently being investigated.
> Keep this file current: human observation → here → review → implementation → game test → update here.

## Goal

Launcher (injector.exe) must detect and inject into Syringe/Ares/Phobos and
CnCNet game processes, not only `gamemd.exe`. Needed for M14.2 (model the
same AA-divert behavior in Ares/Phobos and compare).

## Scope

- `src/injector_gui.cpp`: `FindTargetProcess()` iterates
  `gamemd.exe` → `gamemd-spawn.exe` (first match wins, module-verified);
  all GUI detection/inject paths report the matched name.
- New hero button **CnCNet**: launches the CnCNet client found next to the
  launcher (`CnCNetClient.exe` / `clientdx.exe` / `clientxna.exe` /
  `CnCNet.exe`), waits up to 120 s for the spawned game, auto-injects
  LuaAPI (status via the standard launch-done path). If a game is already
  running, injects into it instead. If no client is installed, explains
  what to install (no silent no-op).
- Vanilla launch flow (`RA2MD` stub → `gamemd.exe`) unchanged.
- Headless `--attach` already supported a list; GUI now matches it.

## Out of Scope

- Do NOT change hook/detour logic for Ares coexistence (M11 Gate 11.2
  already covers MinHook chaining; verify via log, don't rework).
- Do NOT change Lua/mods/native bindings in this task.
- Do NOT start M14.2 itself (needs the Ares/Phobos install + modeling).

## Current Hypothesis

With Syringe launching the game, the user injects via the GUI Inject button
into the already-running process (Syringe must stay the launcher). Multi-name
detection covers `gamemd.exe` and `gamemd-spawn.exe` setups.

## Test Scenario

1. Close the running launcher (it locks `injector.exe` and blocks deploy).
2. Rebuild (or manual copy) deploys the new `injector.exe` to game dir.
3. Launch the game via Syringe (Ares/Phobos); press Inject in the launcher.
4. Check `LuaAPI.log`: `Detected running <name>`, `MH_CreateHook` statuses.

## Expected Result

- Inject button finds `gamemd.exe` OR `gamemd-spawn.exe` (whichever runs).
- `LuaAPI.log` shows successful injection + hook states under Ares/Phobos.
- Vanilla path (Launch button → `gamemd.exe`) behaves exactly as before.

## Actual Result

Syringe/CnCNet test PASSED 2026-09-10 23:56 UTC: all signatures OK, all
hooks `MH_OK` (MainLoop, LoadString, Detonate, GetPrimaryWeapon) under
Syringe + Ares + Phobos + spawner — no hook conflicts. Both mods loaded,
log alive (24 KB). Multi-name detection + module-wait policy confirmed
working. DTA-theme client crash resolved user-side (client runs).

## Evidence

- `src/injector_gui.cpp`: `kGameProcessNames[]`, `FindTargetProcess(outName)`,
  updated detection/inject/log call sites; launch flow untouched.
- Build 2026-09-11 00:21 UTC: compile OK; auto-deploy of `injector.exe`
  failed with Permission denied (file in use); `LuaAPI.dll` unaffected
  (this task touches the injector project only).

## Analysis Status

`CLOSED`

<!-- Valid values: OPEN / INVESTIGATING / READY FOR IMPLEMENTATION / BLOCKED / CLOSED -->

## Next Action

CnCNet-only testing from here. Next candidate: M14.2 (Ares/Phobos comparison
modeling — Syringe stand is ready, multi-name detection + CnCNet button live).
