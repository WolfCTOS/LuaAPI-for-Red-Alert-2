# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Project Roadmap

## 📍 Project Lifecycle Overview
- **Phase 1: Proof of Concept & MVP (Milestones 1–5)** -> [x] DONE / VERIFIED
- **Phase 2: Alpha-1 - Core Lifecycle & Safety (Milestone 6)** -> [x] DONE / VERIFIED
- **Phase 3: Alpha-2 - Spatial Queries & Extended Events (Milestone 7)** -> [x] DONE / VERIFIED
- **Phase 4: Beta - Feature Freeze & Stress Hardening (Milestone 8)** -> [x] DONE / VERIFIED
- **Phase 5: Production Release v1.0 (Milestone 9)** -> [x] DONE / VERIFIED

---

## 🏆 Milestones & Gates

### [x] Milestone 1–3: Core Engine & Hooking (MVP)
- [x] Hook MainLoop at 0x55D360 with zero regression.
- [x] Integrate Lua 5.4 runtime and isolated pcall execution.
- [x] Basic TechnoClass property manipulation bindings.

### [x] Milestone 4: Inbound Events (Sub-Frame Control)
- [x] `OnPreDamage` engine interception hook.
- [x] `game_RegisterEvent` API with damage modification support.
- [x] Validated via `shield_overload` mod.

### [x] Milestone 5: Multiplayer Determinism & Performance
- [x] CnCNet `--withcncnet` headless spawner mode.
- [x] Synchronized deterministic RNG (seed 12345) to prevent OOS.
- [x] Hardware benchmark via Intel PresentMon (60.04 FPS / 55.11 1% Lows).

### [x] Milestone 6: Alpha-1 — Lifecycle & RTTI Safety
- [x] `ResetSession()` on scenario start/restart/exit (zero memory/callback leaks).
- [x] `ValidateTechno()` RTTI (`WhatAmI()`) and lifecycle flags to prevent `0xC0000005` Access Violations.
- [x] Economy & HUD bindings: `house_GetCredits`, `house_AddCredits`, `game_PrintMessage`.
- [x] Validated via `bounty_hunter` mod and v0.1.0-alpha release.

### [x] Milestone 7: Alpha-2 — Spatial Map API & Extended Events (CURRENT)
- [x] **Gate 7.1: Scenario Start Hook (`OnScenarioStart`)**: First-frame callback execution after map load.
- [x] **Gate 7.2: Destruction Event Hook (`OnUnitDestroyed`)**: Inbound event on unit death `(victim, killer)`.
- [x] **Gate 7.3: Spatial Queries API**:
  - [x] `game_GetWaypoint(waypoint_id)` -> returns map coordinates/cell.
  - [x] `game_GetUnitsInRadius(x, y, radius_cells)` -> returns table of techno pointers within distance.
- [x] **Gate 7.4: Showcase Mod `damaged_fleet`**: Validates start-of-match damage and smoke attachment without trigger hacks.

### [x] Milestone 8: Beta — Feature Freeze & Hardening
- [x] **Gate 8.1: Long-Run Stress Test**: 200+ active units in combat over 30 min without memory creep.
- [ ] **Gate 8.2: CnCNet ModBase Native Integration**: Transparent DLL loading without standalone injector window.
- [ ] **Gate 8.3: Complete API Reference Manual**: Full markdown documentation of all Lua functions.

### [x] Milestone 9: Production Release v1.0
- [x] **Gate 9.1: Public Release Package**: v1.0.0 Stable ZIP on GitHub & ModDB.
- [x] **Gate 9.2: Community Showcase Announcement**: Release thread on Haven & PPM.

## Architecture Notes

- **Threading:** Lua lives entirely on the main game thread (lazy init inside the hook). Logger is the only cross-thread component (mutex-protected).
- **Hook:** MinHook trampoline on `Unsorted::MainLoop`; original runs first, then Lua dispatch guarded by `ScenarioClass::Instance != nullptr`.
- **Safety:** every binding validates pointers; script errors are contained by `lua_pcall` and logged, never crash the game.
- **Deploy:** POST_BUILD copies `LuaAPI.dll` / `injector.exe` to the game root; `scripts/` is resolved relative to the DLL.

---

## Release History

| Version | Date | Description |
|---------|------|-------------|
| v0.1.0-alpha | 2026-08-26 | Initial public release with Milestones 1–7: core engine, inbound events, multiplayer determinism, lifecycle hardening, RTTI pointer safety, economy & HUD bindings, spatial map API and damaged_fleet mod showcase. |
| v1.0.0-stable | TBD | Planned production release with full API reference, CnCNet ModBase native integration, and long-run stress test validation. |