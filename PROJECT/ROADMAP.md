# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Architecture Roadmap

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current Release:** `v1.0.0` Production Release  
> **Current Development:** Milestone 12  
> **Last Updated:** August 31, 2026

This roadmap tracks the evolution of LuaAPI from runtime embedding to safe native bindings, CnCNet integration, advanced combat systems, and Lua-driven tactical gameplay.

> ⚠️ **Verification policy:** Implementation and runtime verification are separate. A capability is only marked `VERIFIED` after it has been tested against the current LuaAPI build.

---

## 📍 Project Lifecycle Overview

| Phase | Milestone | Status |
|---|---|---|
| Phase 1 | Milestones 1–5 — MVP & Core Runtime | ✅ DONE / VERIFIED |
| Phase 2 | Milestone 6 — Lifecycle & Safety | ✅ DONE / VERIFIED |
| Phase 3 | Milestone 7 — Spatial API & Events | ✅ DONE / VERIFIED |
| Phase 4 | Milestone 8 — Beta Hardening | ✅ DONE / VERIFIED |
| Phase 5 | Milestone 9 — Production Release v1.0 | ✅ DONE / VERIFIED |
| Phase 6 | Milestone 10 — Multi-Turret & Advanced Combat | 🟡 CORE COMPLETE |
| Phase 7 | Milestone 11 — CnCNet Compatibility & Dev Tools | ✅ DONE |
| Phase 8 | Milestone 12 — Unit Control API & Tactical AI | ✅ DONE |

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
### [x] Gate 4.1 — `OnPreDamage`
Implemented interception around the engine damage-processing path.
### [x] Gate 4.2 — Damage Modification Pipeline
Lua can pass damage through, modify it, or return `0` to cancel it.
### [x] Gate 4.3 — `shield_overload` Validation
Validated through the `shield_overload` showcase.
**Status:** ✅ VERIFIED

---

## [x] Milestone 5 — Multiplayer Determinism & Benchmarking
### [x] Gate 5.1 — CnCNet Process Attachment
Implemented process attachment for CnCNet-launched game instances.
### [x] Gate 5.2 — Deterministic Frame-Based Execution
Gameplay callbacks use the engine's logical frame rather than render FPS.
### [x] Gate 5.3 — Performance Benchmarking
Benchmarked LuaAPI under real game workloads with negligible observed runtime overhead.

---

# [x] Milestone 6 — Alpha-1: Lifecycle Hardening & Safety
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
**Status:** ✅ VERIFIED

---

# [x] Milestone 7 — Alpha-2: Spatial Map API & Extended Events
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
**Status:** ✅ VERIFIED

---

# [x] Milestone 8 — Beta: Feature Freeze & Hardening
### [x] Gate 8.1 — Long-Run Stress Testing
Verified runtime stability and sustained Lua execution under heavy AI workloads.

### [x] Gate 8.2 — API Reference
Created `API.md` as the primary public API reference.

### [x] Gate 8.3 — CnCNet Integration
Validated LuaAPI in CnCNet-oriented mod environments.

### [x] Gate 8.4 — API Stabilization
Stabilized public API signatures before production release.

---

# [x] Milestone 9 — Production Release v1.0

### [x] Gate 9.1 — `v1.0.0` Release
Published the runtime, injector/launcher, examples, and documentation.

### [x] Gate 9.2 — Community Release
Published LuaAPI for external C&C modding communities and testers.
**Release status:** ✅ `v1.0.0`

---

# 🟡 Milestone 10 — Multi-Turret & Advanced Combat
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

### [ ] Gate 10.3 — Voxel Matrix Rendering
**Status:** ⏸️ **DEFERRED TO MILESTONE 12**
Independent visual rotation of voxel sub-turrets remains deferred. The project prioritizes functional multi-turret combat before visual turret rendering.

---

# [x] Milestone 11 — CnCNet Compatibility & Development Tools
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

# 🔵 Milestone 12 — Unit Control API & Tactical AI
> **Status:** 🔵 **IN PROGRESS**  
> **Goal:** Expose safe unit-control primitives and use them to build higher-level tactical behavior without immediately replacing the native AI.

The current development loop is:
```
Observe game state
      ↓
Select unit
      ↓
Select objective / threat
      ↓
Issue movement or attack command
      ↓
Observe result
      ↓
Re-evaluate
```

### [x] Gate 12.1 — Unit Control API Vertical Slice
Implemented:
```
unit:GetMission()
unit:GetTarget()
unit:MoveTo(x, y)
unit:Attack(target)
unit:Stop()
unit:IsIdle()
```


`Attack()` uses the native target-assignment and attack-mission path without relying on the disabled `ActiveClickWith` hook.

Added the `patrol_demo` showcase.
**Implementation status:** ✅ COMPLETE  
**Runtime showcase status:** ⚠️ VERIFICATION PENDING / DEBUGGING
The current build exposed Lua-side errors in the first showcase pass. Runtime movement and attack behavior must be re-tested after those errors are corrected.

### [ ] Gate 12.2 — Dynamic Objective Defense
Create a Lua-driven tactical showcase that detects important objectives, selects nearby friendly units, moves them toward the objective, scans for enemies, attacks them, and continuously re-evaluates the situation.

**Target showcase:** `dynamic_objective_defense`
The first version should use the minimum existing API surface. New bindings should only be added when a concrete missing primitive is demonstrated.

### [ ] Gate 12.3 — Miner Self-Preservation / Threat Avoidance
Prevent mining units from repeatedly entering obvious enemy danger zones.
Initial behavior:
- Detect hostile units near the miner's route or destination.
- Avoid sending replacement miners into recently dangerous areas.
- Stop or redirect miners when the route becomes unsafe.
- Avoid repeatedly committing miners after losses without re-evaluating the threat.

This is a concrete survival behavior, not a complete strategic AI.

### [ ] Gate 12.4 — Tactical Threat Model
After basic survival behavior is proven, introduce a lightweight threat model using factors such as enemy presence, distance, friendly strength, objective value, recent losses, and retreat/regroup state.
Do not add weapon-range or damage-estimation bindings until a concrete behavior requires them.

### [ ] Gate 12.5 — Dynamic Salvo Convergence
Reallocate a multi-turret target when an assigned target is destroyed.

### [ ] Gate 12.6 — Voxel Matrix Sub-Turret Rendering
Resume the deferred visual turret work from Milestone 10.

Target areas:

```text
TechnoClass::Draw
VoxelClass::Draw_Matrix
Matrix3D transformations
```

### [ ] Gate 12.7 — Dynamic Build Queue Control
Expose safe Lua control over production decisions for reinforcement, naval production, threat-based production, and custom tactical AI.

---

# 🧭 Architectural Principles

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
RA2 uses leptons (`1 cell = 256 leptons`). Squared distance calculations may exceed signed 32-bit limits; use 64-bit arithmetic where required.

### 7. ⏱️ Use Logical Game Frames
Gameplay callbacks should use logical frame progression rather than render FPS.

### 8. 🔗 Hook Conflicts Are Not Automatically Fatal
A signature mismatch or existing hook is not automatically an injection failure. Hook state must be inspected in context.

### 9. 🎯 C++ Combat Systems Should Remain Passive
Native systems maintain state and expose safe primitives. Strategic decisions such as who to attack and when to attack belong to Lua or explicit game commands.

### 10. 🧪 No API Without a Demonstrated Consumer

Preferred progression:
```
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

# 📊 Current Project State

```text
Milestones 1–9
████████████████████ 100% ✅

Milestone 10
██████████████████░░  Core complete 🟡

Milestone 11
████████████████████ 100% ✅

Milestone 12
███░░░░░░░░░░░░░░░░░  Current development 🔵
```

Milestone 12 is intentionally iterative. Individual gates should only be marked verified after implementation and runtime testing against the current build.
---
# 🎯 Roadmap Philosophy
LuaAPI should evolve in this order:
```
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

The roadmap should describe what is **actually proven**, not what is merely technically imaginable.
