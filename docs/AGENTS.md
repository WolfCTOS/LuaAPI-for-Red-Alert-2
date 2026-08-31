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
- API surface: World.* (GetBuildings/GetUnits/GetAllUnits/GetUnitsInRadius), unit methods incl. sub-turret API (AddSubTurret/SetSplitTargets/FireSplitSalvo) and IsAttacking, AI.QueueUnit/AI.CountUnit.

### Traps (read before touching hooks or combat)
1. ActiveClickWith detour crashes on ANY player attack order on a TechnoClass target (spawner or not), even with trivial return. Native path works with hook disabled (kDisableActiveClickHook=true). Do not re-enable without a new interception point (Milestone 11: SetTarget/QueueMission detour or Ares/Phobos). Full story: docs/ENGINEERING_LESSONS.md section 9.
2. SpawnManager umbilical: native AI re-overwrites spawned missile target from Owner->Target each frame; decouple via pMissile->SpawnOwner=nullptr before redirect.
3. RocketLocomotor precomputes ballistic spline at spawn; in-flight redirect needs Force_Immediate_Destination.
4. Warhead choice decides everything: stock [Fire] deals ~0 vs heavy armor; fallback chain named -> TerrorBombWH -> DemobombWH -> Rules->C4Warhead; pass IgnoreDefenses/PreventSelfDefend from scripted AoE.
5. Dangling pointers: engine frees objects aggressively; cached pointers die within frames.
6. Log spam: per-frame critical frame logs and per-volley tracer logs flood LuaAPI.log; demote before demo/release.

## Status (update at end of every session)

### Current: Milestone 10 (v1.1) — ready for tag
- Gate 10.1 done: sub-turret sidecar state, cleanup on death.
- Gate 10.2 done: per-turret targeting, ROT stepping, tracer bullets (damage 0 visual for spawners, AP direct-hit otherwise), split-salvo redirect via ProcessSpawnedMissiles.
- Gate 10.3 deferred to Milestone 11: voxel render stubs empty (InitDrawHook/DrawSubTurrets); the v1.1 presentation relies on tracers, not rotating voxels.
- Gate 10.4 done: showcase multi_turret_battleship works (log proof: 5 DREDs x 3 turrets, distinct targets, missile #2 redirected, Lua avg 0.02-0.04 ms).
- Player priority implemented: the player's native target (unit:GetTarget) is locked onto turret 0 and excluded from the candidate pool in AssignSplitTargets; autonomous targeting fills the remaining turrets.
- Log hygiene done: per-frame EventHook::Update / frame logs and per-volley tracer logs demoted to trace/debug; dead-target ValidateTechno -> debug; warn/error verbosity untouched.
- Known issues: ActiveClickWith hook disabled (needs a Milestone 11 interception point); voxel render stubs deferred to Milestone 11.
- Next: record the demo video, tag v1.1, then Milestone 11 (ActiveClickWith/SetTarget interception + voxel rendering).

### Session protocol
- One session per sprint/task to bound context cost.
- Read this file plus the relevant docs section first.
- Last command of the session: update this Status block.