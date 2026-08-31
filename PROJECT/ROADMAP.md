# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Architecture Roadmap

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current Release:** `v1.0.0` Production Release  
> **Current Development:** Milestone 12 (Showcase Verified)  
> **Last Updated:** September 1, 2026

This roadmap tracks the evolution of LuaAPI from runtime embedding to safe native bindings, CnCNet integration, advanced combat systems, and Lua-driven tactical gameplay.

> ⚠️ **Verification policy:** Implementation and runtime verification are separate. A capability is only marked `VERIFIED` after it has been tested against the current LuaAPI build.

---

## 📍 Project Lifecycle Overview

| Phase | Milestone | Status |
|---|---|---|
| Phase 1 | Milestones 1–5 — MVP & Core Runtime | ✅ DONE / VERIFIED |
| Phase 2 | Milestone 6 — Lifecycle & Safety | ✅ DONE / VERIFIED |
| Phase 3 | Milestone 7 — Spatial API & Events | ⚠️ CODE COMPLETE, RUNTIME ISSUES |
| Phase 4 | Milestone 8 — Beta Hardening | ✅ DONE / VERIFIED |
| Phase 5 | Milestone 9 — Production Release v1.0 | ✅ DONE / VERIFIED |
| Phase 6 | Milestone 10 — Multi-Turret & Advanced Combat | 🟡 CORE COMPLETE |
| Phase 7 | Milestone 11 — CnCNet Compatibility & Dev Tools | ✅ DONE |
| Phase 8 | Milestone 12 — Unit Control API & Tactical AI | 🟡 SHOWCASE VERIFIED |

---

# 🏆 Detailed Milestones & Gates

## [x] Milestones 1–3 — Core Engine Hooking & Runtime Sandbox

> **Goal:** Embed Lua into the closed-source 32-bit Yuri's Revenge executable without destabilizing the game loop.

### [x] Gate 1.1 — MainLoop Hook

Hooked the canonical game loop at `0x55D360`.

**Result:** Stable Lua execution inside `gamemd.exe`.

### [x] Gate 1.2 — Lua 5.4 Runtime

Implemented the native Lua 5.4 runtime, isolated script execution, protected calls, and script error reporting.

### [x] Gate 1.3 — Native Techno Access

Established the C++ ↔ Lua bridge for engine-backed `TechnoClass` objects.

---

## [x] Milestone 4 — Inbound Events & Sub-Frame Reactive Control

> **Status:** ⚠️ **CODE COMPLETE, RUNTIME ISSUES**  
> Implementation exists, but events do not fire in current builds due to initialization race conditions and missing `pcall` wrappers. Showcase mods (`shield_overload`, `bounty_hunter`) fail to load or crash. Restoration work is planned for Milestone 13.

### [x] Gate 4.1 — `OnPreDamage`

Implemented interception around the engine damage-processing path.

### [x] Gate 4.2 — Damage Modification Pipeline

Lua can pass damage through, modify it, or return `0` to cancel it.

### [x] Gate 4.3 — `shield_overload` Validation

Validated through the `shield_overload` showcase.

**Status:** ✅ VERIFIED in the initial release; ⚠️ BROKEN in current builds.

---

## [x] Milestone 5 — Multiplayer Determinism & Benchmarking

### [x] Gate 5.1 — CnCNet Process Attachment

Implemented process attachment for CnCNet-launched game instances.

### [x] Gate 5.2 — Deterministic Frame-Based Execution

Gameplay callbacks use the engine's logical frame rather than render FPS.

### [x] Gate 5.3 — Performance Benchmarking

Benchmarked LuaAPI under real game workloads with negligible observed runtime overhead.

---

## [x] Milestone 6 — Alpha-1: Lifecycle Hardening & Safety

> **Goal:** Prevent stale engine pointers and lifecycle-related crashes.

### [x] Gate 6.1 — Session Lifecycle Management

Handled scenario initialization, mission restart, and game exit resets.

### [x] Gate 6.2 — Techno Validation

Implemented liveness validation before dereferencing exposed engine objects.

### [x] Gate 6.3 — Economy & HUD APIs

Implemented:

```lua
House.GetPlayer()
House.GetCredits()
House.AddCredits()
Engine.PrintMessage()
```

### [x] Gate 6.4 — `bounty_hunter` Validation

Validated lifecycle and economy APIs through the `bounty_hunter` showcase.

**Status:** ✅ VERIFIED in the initial release; ⚠️ BROKEN in current builds due to the event system.

---

## [x] Milestone 7 — Alpha-2: Spatial Map API & Extended Events

> **Status:** ⚠️ **CODE COMPLETE, RUNTIME ISSUES**  
> Spatial queries work. Event hooks (`OnScenarioStart`, `OnUnitDestroyed`) are implemented but do not fire due to initialization race conditions and missing `pcall`. The `damaged_fleet` showcase has syntax errors and does not load. Restoration is planned for Milestone 13.

### [x] Gate 7.1 — `OnScenarioStart`

Implemented post-scenario initialization callbacks.

### [x] Gate 7.2 — `OnUnitDestroyed`

Implemented destruction-event handling for gameplay systems.

### [x] Gate 7.3 — Spatial Queries

Implemented:

```lua
World.GetWaypoint()
World.GetUnits()
World.GetUnitsInRadius()
```

### [x] Gate 7.4 — `damaged_fleet` Validation

Validated scenario-start modification and visual damage effects.

**Status:** ✅ VERIFIED in the initial release; ⚠️ BROKEN in current builds.

---

## [x] Milestone 8 — Beta: Feature Freeze & Hardening

### [x] Gate 8.1 — Long-Run Stress Testing

Verified runtime stability and sustained Lua execution under heavy AI workloads.

### [x] Gate 8.2 — API Reference

Created `API.md` as the primary public API reference.

### [x] Gate 8.3 — CnCNet Integration

Validated LuaAPI in CnCNet-oriented mod environments.

### [x] Gate 8.4 — API Stabilization

Stabilized public API signatures before production release.

---

## [x] Milestone 9 — Production Release v1.0

### [x] Gate 9.1 — v1.0.0 Release

Published the runtime, injector/launcher, examples, and documentation.

### [x] Gate 9.2 — Community Release

Published LuaAPI for external C&C modding communities and testers.

**Release status:** ✅ `v1.0.0`

---

## 🟡 Milestone 10 — Multi-Turret & Advanced Combat

> **Status:** 🟡 **CORE COMPLETE**  
> **Version target:** `v1.1`  
> **Goal:** Break the vanilla single-target / single-turret limitation while keeping native C++ systems passive and Lua-driven.

### [x] Gate 10.1 — Sub-Turret Memory Model & Lifecycle

Implemented `SubTurretManager` as a native C++ sidecar associated with `TechnoClass*`.

Tracked state includes turret identity, facing, target, reload/ROF timer, weapon information, and spatial offsets. Lifecycle handling includes unit removal, target invalidation, deferred cleanup, and global target invalidation.

**Status:** ✅ VERIFIED

### [x] Gate 10.2 — Independent Targeting & Combat Dispatch

Implemented multiple turret slots, independent targets, target-facing calculations, ROT stepping, split-target allocation, explicit salvo dispatch, and spawned missile interception.

**Status:** ✅ VERIFIED

### [ ] Gate 10.3 — Voxel Matrix Rendering

**Status:** ⏸️ **DEFERRED TO MILESTONE 12**

Independent visual rotation of voxel sub-turrets remains deferred. The project prioritizes functional multi-turret combat before visual turret rendering.

### [x] Gate 10.4 — Lua Multi-Turret API & Showcase

Exposed:

```lua
unit:AddSubTurret(...)
unit:GetSubTurretCount()
unit:SetSplitTargets(...)
unit:FireSplitSalvo()
```

Validated through `multi_turret_battleship`.

**Status:** ✅ VERIFIED

### [x] Gate 10.5 — Spawned Missile Decoupling

Implemented native interception and locomotor redirection for spawned projectiles so the parent spawn manager cannot continuously force them back onto the parent's target.

**Status:** ✅ VERIFIED

---

## [x] Milestone 11 — CnCNet Compatibility & Development Tools

> **Status:** ✅ **DONE**  
> **Goal:** Make LuaAPI reliable in CnCNet-launched environments and provide the tooling needed for continued development.

The engineering work for this milestone is complete. Full two-client online multiplayer validation is not claimed and remains a separate test.

### [x] Gate 11.1 — CnCNet Attach Mode

Implemented attach mode for CnCNet-launched `gamemd-spawn.exe` processes.

**Status:** ✅ VERIFIED

### [x] Gate 11.2 — MinHook Compatibility / Chaining

Implemented compatibility handling for hooks coexisting with Ares, Phobos, and CnCNet infrastructure.

**Status:** ✅ VERIFIED

### [x] Gate 11.3 — Logical-Frame Gating

Gameplay callbacks are synchronized to logical game frames for deterministic execution.

**Status:** ✅ VERIFIED

### [x] Gate 11.4 — `house:SpawnUnit`

Implemented validated unit spawning with placement/pathfinding checks and fallback placement.

**Status:** ✅ VERIFIED

### [x] Gate 11.5 — Debug Input Layer

Implemented development input handling for debug command entry and execution.

**Status:** ✅ VERIFIED

### [x] Gate 11.6 — ModLoader Path Resolution

Fixed mod/script path resolution so the loader resolves paths relative to the LuaAPI/DLL environment instead of the process working directory.

**Status:** ✅ VERIFIED

---

## [x] Milestone 12 — Unit Control API & Tactical AI

> **Status:** 🟡 **SHOWCASE VERIFIED**  
> **Goal:** Expose safe unit-control primitives and use them to build higher-level tactical behavior without immediately replacing the native AI.

### [x] Gate 12.1 — Unit Control API Vertical Slice

Implemented:

```lua
unit:GetMission()
unit:GetTarget()
unit:MoveTo(x, y)
unit:Attack(target)
unit:Stop()
unit:IsIdle()
```

`Attack()` uses the native target-assignment and attack-mission path without relying on the disabled `ActiveClickWith` hook.

Fixed house userdata caching (`PushHouse` caches `HouseClass* → Lua registry ref`), so `unit:GetOwner() == House.GetPlayer()` evaluates correctly instead of always being `false` due to fresh userdata per call.

Added the `patrol_demo` showcase: anchors a combat unit (skips MCV), patrols between two points, engages enemies in radius with a 100-frame cooldown, and ignores neutrals/civilians/vehicles.

**Implementation status:** ✅ COMPLETE  
**Runtime verification:** ✅ VERIFIED

### [x] Gate 12.2 — Dynamic Objective Defense

Created `dynamic_objective_defense`, a public showcase mod that:

1. Selects a unit manually and activates the defender with **Numpad1**.
2. Scans `World.GetBuildings()` for an objective (`CAOILD`, `CAHOSP`, `CAAIRP`, or a captured `CA*` building).
3. Patrols two points on either side of the objective, offset by ±6 cells.
4. Detects enemies within 15 cells and issues `unit:Attack` with a 100-frame cooldown. Neutrals and civilians are ignored.
5. After the target dies, calls `unit:Stop` and resumes patrol.

The complete `detect → attack → kill → return to patrol` cycle was verified in live gameplay.

Also added `Input.WasKeyPressed` and `World.GetSelectedUnits` as bindings with a demonstrated consumer in the showcase.

All decisions run in `Update()` each logic frame. No event hooks are required.

Full walkthrough: `docs/SHOWCASE_DYNAMIC_DEFENSE.md`.

**Implementation status:** ✅ COMPLETE  
**Runtime verification:** ✅ VERIFIED

### [ ] Gate 12.3 — Miner Safety Validation

`miner_safety` detects threats near player miners, stops threatened miners, and delays respawn while the refinery is under threat.

**Current status:** 🟡 IMPLEMENTED, LIVE-FIRE VALIDATION PENDING

Known limitation: enemy miners cannot be reliably protected with `unit:Stop()` alone because vanilla AI reissues harvest/movement orders every frame. Protecting enemy miners would require hooking AI decision logic rather than only issuing a stop command.

Hotkey spam and mixed multi-select behavior are also not stress-tested. `World.GetSelectedUnits()` currently returns `UnitClass` objects and the showcase uses the first selected unit.

### 🔬 Milestone 12 Technical Notes

- `World.GetSelectedUnits()` reads `ObjectClass::CurrentObjects` at `0xA8ECB8`. `MouseClass::Instance->CurrentObject` does not exist in this YRpp branch.
- `Input.WasKeyPressed` is edge-triggered: it returns `true` once on the not-pressed → pressed transition. Previous state is stored in `g_keyPrevState[256]`.
- MCVs have no weapon, so `SetTarget` does not stick and `GetTarget()` returns `nil`. `patrol_demo` therefore filters MCVs out.
- The house equality bug was a Lua userdata identity problem. Registry-reference caching fixes it and is cleared during `ResetSession`.

---

## 🧭 Architectural Principles

### 1. 🧠 C++ Manages State, Lua Controls Gameplay

C++ provides safe engine access, native state, lifecycle handling, timers, and performance-critical integration. Lua provides gameplay rules, target selection, tactical decisions, and mod-specific behavior.

### 2. 🛡️ Never Trust Persistent Engine Pointers

Engine-backed pointers can become invalid after destruction, scenario transitions, savegame loading, cleanup, or unit removal. Native code must validate them before dereferencing.

### 3. 🧹 Use Deferred Cleanup

Detect invalid objects first, queue removals, finish iteration, then process removals.

### 4. 💀 Invalidate Destroyed Targets Globally

Every subsystem holding target references must release them when engine objects are destroyed.

### 5. 💾 Treat Savegames as a Separate Lifecycle

Runtime systems must account for new scenarios, loaded savegames, newly spawned units, and existing units.

### 6. 🔢 Use Appropriate Integer Widths

RA2 uses leptons (1 cell = 256 leptons). Squared distance calculations may exceed signed 32-bit limits; use 64-bit arithmetic where required.

### 7. ⏱️ Use Logical Game Frames

Gameplay callbacks should use logical frame progression rather than render FPS.

### 8. 🔗 Hook Conflicts Are Not Automatically Fatal

A signature mismatch or existing hook is not automatically an injection failure. Hook state must be inspected in context.

### 9. 🎯 C++ Combat Systems Should Remain Passive

Native systems maintain state and expose safe primitives. Strategic decisions such as who to attack and when to attack belong to Lua or explicit game commands.

### 10. 🧪 No API Without a Demonstrated Consumer

Preferred progression:

```text
Gameplay requirement
        ↓
Try existing API
        ↓
Identify missing primitive
        ↓
Add minimal binding
        ↓
Build showcase
        ↓
Verify in game
```

This keeps the native API surface small and prevents speculative engine exposure.

---

## 📊 Current Project State

```text
Milestones 1–9
████████████████████ 100% ✅

Milestone 10
██████████████████░░  Core complete 🟡

Milestone 11
████████████████████ 100% ✅

Milestone 12
██████████████████░░  Showcase verified 🟡
```

Milestone 12 is intentionally iterative. Individual gates should only be marked verified after implementation and runtime testing against the current build.

---

## 🎯 Roadmap Philosophy

LuaAPI should evolve in this order:

```text
Reverse Engineering
        ↓
Safe Native Primitive
        ↓
Lua Binding
        ↓
Small Prototype
        ↓
Stress Test
        ↓
Verified Capability
        ↓
Documentation
        ↓
Higher-Level Systems
```

The roadmap should describe what is actually proven, not what is merely technically imaginable.
