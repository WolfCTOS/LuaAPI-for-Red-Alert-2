# AGENTS.md

## Stable facts (change rarely)

### Project
- LuaAPI: Lua 5.4 runtime injected into Yuri's Revenge 1.001 (gamemd.exe, 32-bit x86).
- Layout: src/ (C++ DLL + injector), scripts/mods/<name>/main.lua (hot-swappable), docs/.
- Build: cmake --build build --config Release; deploys LuaAPI.dll + injector.exe.
- Injector must attach to gamemd.exe. RA2MD.exe is a launcher stub (VirtualProtect error 487 = wrong process).

### Architecture
- MinHook on Unsorted::MainLoop (0x55D360), per-frame dispatch, in-match guarded.
- MinHook on StringTable::LoadString (0x734E60, __fastcall).
- Lua state on main game thread, lazy init on first tick.
- Lua changes need no rebuild (reload on match start); C++ changes need rebuild + redeploy.

### Verified addresses (gamemd.exe 1.001)
- 0x55D360 Unsorted::MainLoop
- 0x734E60 StringTable::LoadString (__fastcall)
- 0xA8ED84 Unsorted::CurrentFrame
- 0xA83D4C ScenarioClass::Instance
- 0x4D74E0 UnitClass::Active_Click_With: DO NOT HOOK (see Traps)

### Conventions
- SEH __try/__except around every engine deref; C++ exceptions must not cross SEH; keep __try in small helper functions (C2712 otherwise).
- Validate before deref: nullptr, WhatAmI() RTTI, Health>0; StillExists() via array membership for dangling.
- Logging: LUA_LOG_* macros, flush per line; LUA_FLUSH_LOG at critical points.
- API surface: World.* (GetBuildings/GetUnits/GetAllUnits/GetUnitsInRadius), unit methods incl. IsAttacking, AI.QueueUnit/AI.CountUnit. Sub-turret / EventHook / BulletHook / FireProjectile REMOVED 2026-09-21.

### Traps (read before touching hooks or combat)
1. ActiveClickWith detour crashes on ANY player attack order on a TechnoClass target (spawner or not), even with trivial return. Native path works with hook disabled (kDisableActiveClickHook=true). Do not re-enable without a new interception point (Milestone 11: SetTarget/QueueMission detour or Ares/Phobos). Full story: docs/ENGINEERING_LESSONS.md section 9.
2. SpawnManager umbilical: native AI re-overwrites spawned missile target from Owner->Target each frame; decouple via pMissile->SpawnOwner=nullptr before redirect.
3. RocketLocomotor precomputes ballistic spline at spawn; in-flight redirect needs Force_Immediate_Destination.
4. Warhead choice decides everything: stock [Fire] deals ~0 vs heavy armor; fallback chain named -> TerrorBombWH -> DemobombWH -> Rules->C4Warhead; pass IgnoreDefenses/PreventSelfDefend from scripted AoE.
5. Dangling pointers: engine frees objects aggressively; cached pointers die within frames.
6. Log spam: per-frame critical frame logs and per-volley tracer logs flood LuaAPI.log; demote before demo/release.

## Status (update at end of every session)

### Current: Gate 1 PARTIAL — feature freeze (2026-09-20 closure audit)

- Gate 1: items 1, 2, 4, 5, 6 CLOSED (docs/config/decision); item 3 (stale
  house-userdata cache, dead `ResetSession`) OPEN and runtime-gated.
  Authoritative state: `PROJECT/GATES.md`.
- Gate 2 PARTIAL (core loop live-verified; lease/production/damage-event gaps open).
- Gate 3 NOT PASSED (flagship ready; release checklist + media + blockers open).
- Feature freeze per `PROJECT/DECISIONS.md`; next research: DU-1 audit
  (`FSM/DYNAMIC_UNIT_BEHAVIOR.md`). No new implementation before DU-1 exit.
- Actual `scripts/active_mods.txt` on disk (2026-09-21): `target_reselect`,
  `bounty_hunter`, `smart_ai`. (`barrel_elevation_diag` / `command_authority`
  are present but INACTIVE.) `bounty_hunter` is the rewritten v2 (ID-diff +
  `MarkBounty`, no `OnPreDamage`) — the old "inert" label in Gate-1 records
  is stale, see `PROJECT/GATES.md` addendum 2026-09-21. `smart_ai` = rally +
  capture guard, no squads.
- `OnPreDamage`/`OnUnitDestroyed`: contracts without invocation (see `API.md`).
  Death detection = ID-diff polling. No native event architecture implemented.

### Session protocol
- One session per sprint/task to bound context cost.
- Read this file plus the relevant docs section first.
- Last command of the session: update this Status block.