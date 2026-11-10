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

## Gameplay-system engineering principles

These rules apply when designing runtime gameplay systems and mods. They are grounded in the repository's experiment history, not intended as generic project-management advice.

1. **Persistent runtime state over static event schedules.** A system should retain, update, and decay relevant match state across frames. A fixed sequence of timed effects without feedback from the match is a scripted sequence, not an adaptive gameplay system. Command Authority's CP, damage, loss, and directive ledgers are examples of state feeding later decisions.

2. **Gameplay decisions over telemetry alone.** Observation, kill feeds, and intensity meters do not constitute gameplay by themselves. A feature should change a participant's next meaningful decision; otherwise classify and scope it as UI or telemetry.

3. **Player agency over passive effects.** Prefer mechanics that expose a meaningful choice with a clear cost, timing, target, and readable outcome. Avoid making the player merely the recipient of unexplained periodic effects.

4. **Counterplay is part of the mechanic.** A consequential action should give opponents a legible opportunity to respond. Retaliation should be attributable and, where appropriate, announced or delayed. If the only sensible response is to wait, the mechanic needs further design.

5. **AI should use the same rules as the player where practical.** Reuse the same cost, eligibility, and execution paths. Any necessary asymmetry must be explicit, deterministic, and communicated rather than implemented as a hidden advantage or handicap.

6. **Determinism by design.** Use logical game frames for decisions. Avoid wall-clock time and uncontrolled randomness in gameplay logic. Prefer stable ID-ordered traversal and predictable arithmetic so equivalent game states produce equivalent decisions. Review determinism before optimizing behavioral cleverness.

7. **Do not extend the native API without demonstrated necessity.** First determine whether the required behavior can be built from existing bindings. Convenience alone is not sufficient justification for a C++ API addition. Document genuine capability gaps instead of assuming an unimplemented hook or binding exists.

8. **Root-cause analysis and feasibility before implementation.** State competing hypotheses, identify what evidence would distinguish them, and test the smallest relevant assumption. A prototype should answer a specific question, not merely demonstrate activity. Do not promote a concept to an implementation plan while critical feasibility checks remain unresolved.

9. **Harness verification is not live-game verification.** A harness PASS proves the tested logic under its mocks and assumptions. It does not prove engine behavior, integration, timing, or multiplayer behavior. Never present harness results as proof of an in-game outcome.

10. **Record live evidence separately and with its limits.** Record the tested build, launch path, scenario, session bounds, observations, and PASS/FAIL/INCONCLUSIVE result. Distinguish instrumented evidence from user-observed behavior. Do not upgrade a claim beyond what the evidence actually establishes.

## Traps (read before touching hooks or combat)
1. ActiveClickWith detour crashes on ANY player attack order on a TechnoClass target (spawner or not), even with trivial return. Native path works with hook disabled (kDisableActiveClickHook=true). Do not re-enable without a new interception point (Milestone 11: SetTarget/QueueMission detour or Ares/Phobos). Full story: docs/ENGINEERING_LESSONS.md section 9.
2. SpawnManager umbilical: native AI re-overwrites spawned missile target from Owner->Target each frame; decouple via pMissile->SpawnOwner=nullptr before redirect.
3. RocketLocomotor precomputes ballistic spline at spawn; in-flight redirect needs Force_Immediate_Destination.
4. Warhead choice decides everything: stock [Fire] deals ~0 vs heavy armor; fallback chain named -> TerrorBombWH -> DemobombWH -> Rules->C4Warhead; pass IgnoreDefenses/PreventSelfDefend from scripted AoE.
5. Dangling pointers: engine frees objects aggressively; cached pointers die within frames.
6. Log spam: per-frame critical frame logs and per-volley tracer logs flood LuaAPI.log; demote before demo/release.

## Repository status and source of truth

Do not treat copied status summaries in this file as authoritative. Project status, current task, and enabled mods must be checked against their respective source files before making claims:
- Project lifecycle and release state: follow the current SemVer documentation and changelog; use `PROJECT/CURRENT_TASK.md` for the active task.
- Current investigation: `PROJECT/CURRENT_TASK.md`.
- Actual mod load list: `scripts/active_mods.txt`.
- API contracts and callback status: `API.md` and the relevant implementation in `src/`.

**Historical note:** older project records refer to the temporary Gate 1-3 tracking framework. It has been retired in favor of SemVer-based release management. Treat Gate references as historical context, not current lifecycle status. Resolve active module configuration against `scripts/active_mods.txt`, the loader, current source, and runtime evidence.

## Session protocol

- One session per sprint/task to bound context cost.
- Read this file plus the relevant docs section first.
- Last command of the session: update the relevant task/status record, but only after verifying it against current repository state.
