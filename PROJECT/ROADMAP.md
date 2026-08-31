# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Architecture Roadmap

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current Release:** `v1.0.0` Production Release  
> **Current Development:** Milestone 11  
> **Last Updated:** August 2026

This document tracks the architectural evolution of LuaAPI from the initial proof of concept to the current advanced combat and engine-integration work.

The roadmap distinguishes between:

- **Completed and verified milestones**
- **Completed gates**
- **Deferred work**
- **Current development**
- **Future architectural goals**

> ⚠️ **Verification policy:** A gate should only be marked `DONE / VERIFIED` when the corresponding functionality has been implemented and tested against the current LuaAPI build.

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
| Phase 7 | Milestone 11 — Presentation, Tactical AI & Naval Intelligence | 🔵 CURRENT |

---

# 🏆 Detailed Milestones & Gates

---

## [x] Milestones 1–3 — Core Engine Hooking & Runtime Sandbox

> **Goal:** Prove that a modern Lua runtime can be embedded into the closed-source 32-bit Yuri's Revenge executable without destabilizing the original engine loop.

### [x] Gate 1.1 — MainLoop Hook

Hook the canonical game loop at:

```text
0x55D360
```

**Result:** Stable per-frame execution inside `gamemd.exe`.

### [x] Gate 1.2 — Lua 5.4 Runtime

Implemented:

- Native Lua 5.4 runtime
- Isolated script execution
- `pcall` error handling
- Script error reporting

Lua errors are handled without terminating the game process.

### [x] Gate 1.3 — Native Techno Access

Established the initial C++ ↔ Lua bridge for reading and modifying engine-backed `TechnoClass` objects.

---

## [x] Milestone 4 — Inbound Events & Sub-Frame Reactive Control

> **Goal:** Move beyond frame polling and allow Lua scripts to react at the engine's damage-processing boundary.

### [x] Gate 4.1 — `OnPreDamage`

Implemented an interception point around the engine's damage-processing path.

### [x] Gate 4.2 — Damage Modification Pipeline

Lua can:

- Pass damage through with `nil`
- Return modified damage
- Return `0` to cancel damage

This provides the foundation for custom defensive mechanics.

### [x] Gate 4.3 — `shield_overload` Validation

Validated through the `shield_overload` showcase mod.

**Status:** ✅ VERIFIED

---

## [x] Milestone 5 — Multiplayer Determinism & Benchmarking

> **Goal:** Establish predictable execution under Yuri's Revenge's lockstep multiplayer environment.

### [x] Gate 5.1 — CnCNet Process Attachment

Implemented process attachment for CnCNet-launched game instances.

The injector can resolve the active game process rather than relying exclusively on the standard executable name.

### [x] Gate 5.2 — Deterministic Frame-Based Execution

Gameplay callbacks are driven by the game's logical frame rather than render-frame frequency.

This establishes the foundation required for deterministic scripting.

### [x] Gate 5.3 — Performance Benchmarking

LuaAPI was benchmarked under real game workloads using hardware-level frame measurements.

The benchmark demonstrated negligible runtime overhead relative to the base game.

---

# [x] Milestone 6 — Alpha-1: Lifecycle Hardening & Safety

> **Goal:** Make the native scripting layer resilient to destroyed engine objects, session transitions, and common sources of `0xC0000005` crashes.

### [x] Gate 6.1 — Session Lifecycle Management

Implemented session reset handling for:

- Scenario initialization
- Mission restart
- Game exit

### [x] Gate 6.2 — Techno Validation

Implemented validation of engine-backed objects before exposing them to Lua.

The API avoids blindly dereferencing stale `TechnoClass*` pointers.

### [x] Gate 6.3 — Economy & HUD APIs

Implemented high-level interfaces for:

```lua
House.GetPlayer()
House.GetCredits()
House.AddCredits()
Engine.PrintMessage()
```

### [x] Gate 6.4 — `bounty_hunter` Validation

Validated the lifecycle and economy APIs through the `bounty_hunter` showcase.

**Status:** ✅ VERIFIED

---

# [x] Milestone 7 — Alpha-2: Spatial Map API & Extended Events

> **Goal:** Replace common trigger-based workarounds with direct Lua access to scenario events and spatial game data.

### [x] Gate 7.1 — `OnScenarioStart`

Implemented post-scenario initialization callback.

Useful for:

- Initial unit configuration
- Scenario setup
- Starting-state modifications
- Custom visual effects

### [x] Gate 7.2 — `OnUnitDestroyed`

Implemented destruction-event handling for gameplay systems such as:

- Bounties
- Boss phases
- Custom victory conditions
- Unit lifecycle tracking

### [x] Gate 7.3 — Spatial Queries

Implemented:

```lua
World.GetWaypoint()
World.GetUnits()
World.GetUnitsInRadius()
```

These provide direct access to map and unit information from Lua.

### [x] Gate 7.4 — `damaged_fleet` Validation

Validated scenario-start unit modification and visual damage effects.

**Status:** ✅ VERIFIED

---

# [x] Milestone 8 — Beta: Feature Freeze & Hardening

> **Goal:** Transition from feature development toward a stable SDK suitable for external modders.

### [x] Gate 8.1 — Long-Run Stress Testing

Performed extended combat testing under heavy AI workloads.

The test verified:

- Runtime stability
- No observed memory creep
- No crash during the test session
- Stable Lua execution under sustained load

### [x] Gate 8.2 — API Reference

Created the primary API reference:

```text
API.md
```

The document defines the public Lua-facing API and usage examples.

### [x] Gate 8.3 — CnCNet Integration

Validated loading LuaAPI within CnCNet-oriented mod environments.

### [x] Gate 8.4 — API Stabilization

Existing public API signatures were stabilized before the production release.

Future changes should preserve compatibility where practical.

---

# [x] Milestone 9 — Production Release v1.0

> **Goal:** Package the proven runtime as a usable public release.

### [x] Gate 9.1 — `v1.0.0` Release

Published:

- LuaAPI runtime
- Injector / launcher components
- Example mods
- Documentation

### [x] Gate 9.2 — Community Release

Published the project for external C&C modding communities and testers.

**Release status:** ✅ `v1.0.0`

---

# 🟡 Milestone 10 — Multi-Turret & Advanced Combat

> **Status:** 🟡 **CORE COMPLETE**  
> **Version target:** `v1.1`  
> **Primary objective:** Break the vanilla single-target / single-turret limitation while keeping the native C++ layer passive and Lua-driven.

Milestone 10 established the core multi-turret architecture.

The remaining presentation-oriented work was intentionally moved to Milestone 11.

---

### [x] Gate 10.1 — Sub-Turret Memory Model & Lifecycle

Implemented `SubTurretManager` as a native C++ sidecar system associated with `TechnoClass*`.

Tracked state includes:

- Turret identity
- Facing
- Target
- Reload / ROF timer
- Weapon information
- Spatial offsets

Lifecycle handling includes:

- Unit removal
- Target invalidation
- Deferred cleanup
- Global target invalidation

**Status:** ✅ VERIFIED

---

### [x] Gate 10.2 — Independent Targeting & Combat Dispatch

Implemented the core infrastructure for:

- Multiple turret slots
- Independent target references
- Target-facing calculations
- ROT stepping
- Split-target allocation
- Explicit salvo dispatch
- Spawned missile interception

The system deliberately separates state management from gameplay decisions.

**C++ manages:**

```text
State
Pointers
Timers
Safety
Engine integration
```

**Lua controls:**

```text
Target selection
Target allocation
Attack decisions
Salvo execution
```

**Status:** ✅ VERIFIED

---

### [x] Gate 10.4 — Lua Multi-Turret API & Showcase

Exposed Lua-facing functionality including:

```lua
unit:AddSubTurret(...)
unit:GetSubTurretCount()
unit:SetSplitTargets(...)
unit:FireSplitSalvo()
```

Validated through:

```text
multi_turret_battleship
```

The showcase demonstrates multiple turret slots engaging separate targets.

**Status:** ✅ VERIFIED

---

### [x] Gate 10.5 — Spawned Missile Decoupling

Implemented the native interception path required to prevent `SpawnManagerClass` from continuously forcing spawned missiles back onto the parent's target.

The system can detach the spawned missile from the parent spawn manager and redirect its trajectory through the native locomotor interface.

This solves a fundamental limitation of spawned `DMISL`-style projectiles.

**Status:** ✅ VERIFIED

---

### [ ] Gate 10.3 — Voxel Matrix Rendering

**Status:** ⏸️ **DEFERRED TO MILESTONE 11**

The original goal was to render independently rotating voxel sub-turrets by hooking the engine's drawing path.

This work was intentionally removed from the Milestone 10 completion criteria.

Milestone 10 therefore focuses on:

> **Functional multi-turret combat first, visual turret rendering second.**

---

# 🔵 Milestone 11 — Tactical AI, Naval Intelligence & Presentation

> **Status:** 🔵 **CURRENT DEVELOPMENT**

Milestone 11 extends the functional combat foundation established in Milestone 10 into higher-level tactical systems and visual presentation.

---

### [ ] Gate 11.1 — Voxel Matrix Sub-Turret Rendering

Implement independent visual rotation for sub-turret voxel components.

Target areas include:

```text
TechnoClass::Draw
VoxelClass::Draw_Matrix
Matrix3D transformations
```

The objective is for each turret's visual facing to correspond to its independent combat target.

---

### [ ] Gate 11.2 — Z-Mode / Planning Command Integration

Investigate interception of the command buffer generated by Planning Mode (`Z` key).

Target behavior:

```text
Player command
      ↓
Planning Mode
      ↓
Command buffer
      ↓
LuaAPI interception
      ↓
SetSplitTargets()
      ↓
Multi-turret execution
```

The objective is to make multi-target control usable through normal player interaction rather than only through scripted commands.

---

### [ ] Gate 11.3 — Dynamic Salvo Convergence

Implement fallback target allocation when one of several assigned targets is destroyed.

Example:

```text
Turret 1 → Target A 💥 DESTROYED
Turret 2 → Target B
Turret 3 → Target C
             ↓
     Reallocate Turret 1
             ↓
        Target B / C
```

The system should avoid wasted salvos and dynamically redistribute available firepower.

---

### [ ] Gate 11.4 — Naval Cell Intelligence

Investigate direct `CellClass` access for:

- Water detection
- Land detection
- Island identification
- Naval movement analysis
- Coastal area classification

This provides the foundation for higher-level naval AI.

---

### [ ] Gate 11.5 — Dynamic Build Queue Control

Expose safe Lua control over production decisions.

Potential applications include:

- Naval production priorities
- Dynamic AI reinforcement
- Threat-based production
- Scenario-specific build logic
- Custom tactical AI

---

### [ ] Gate 11.6 — Tactical AI Prototype

Build the first Lua-driven tactical AI layer using the existing engine APIs.

Potential architecture:

```text
                 ┌──────────────┐
                 │  Game State  │
                 └──────┬───────┘
                        ↓
              ┌──────────────────┐
              │ Tactical Analysis │
              └────────┬─────────┘
                       ↓
              ┌──────────────────┐
              │ Decision System  │
              └────────┬─────────┘
                       ↓
              ┌──────────────────┐
              │ Lua Gameplay API │
              └──────────────────┘
```

The objective is not to replace the vanilla AI immediately, but to establish a programmable tactical layer on top of the existing engine.

---

# 🧭 Architectural Principles

These principles should remain stable across future milestones.

### 1. 🧠 C++ Manages State, Lua Controls Gameplay

C++ should provide:

- Safe engine access
- Native state management
- Pointer lifecycle handling
- Timers
- Performance-critical integration

Lua should provide:

- Gameplay rules
- Target selection
- Tactical decisions
- Mod-specific behavior

---

### 2. 🛡️ Never Trust Persistent Engine Pointers

An engine pointer obtained during one frame may become invalid later.

Objects can disappear because of:

- Destruction
- Scenario transitions
- Savegame loading
- Engine cleanup
- Unit removal

Native code must validate engine-backed objects before dereferencing them.

---

### 3. 🧹 Use Deferred Cleanup

Do not erase entries from a container while iterating through that same container.

Preferred pattern:

```text
Update
  ↓
Detect invalid objects
  ↓
Queue removal
  ↓
Finish iteration
  ↓
Process removals
```

---

### 4. 💀 Invalidate Destroyed Targets Globally

Any subsystem maintaining target references must release references to destroyed objects.

This is especially important for multi-turret systems where several independent turrets may reference the same target.

---

### 5. 💾 Treat Savegames as a Separate Lifecycle

`OnScenarioStart()` is not sufficient for all initialization requirements.

Runtime systems must account for:

```text
New scenario
    +
Loaded savegame
    +
Newly spawned units
    +
Existing units
```

---

### 6. 🔢 Use Appropriate Integer Widths

RA2 coordinates use leptons:

```text
1 cell = 256 leptons
```

Squared distance calculations can exceed signed 32-bit integer limits.

Use 64-bit arithmetic where required.

---

### 7. ⏱️ Use Logical Game Frames

Gameplay callbacks should be synchronized with the engine's logical frame progression rather than render FPS.

This prevents high-refresh or variable-FPS environments from unintentionally executing gameplay logic at different rates.

---

### 8. 🔗 Hook Conflicts Are Not Automatically Fatal

A function may already be hooked by another component such as Ares, Phobos, or CnCNet infrastructure.

Therefore:

```text
Signature mismatch
        ≠
Injection failure
```

Hook state must be inspected before treating a mismatch as a fatal condition.

---

### 9. 🎯 C++ Combat Systems Should Remain Passive

The multi-turret system established an important architectural boundary.

The native manager should maintain state and provide safe primitives.

It should not independently decide:

```text
Who to attack
When to attack
Which target is strategically important
```

Those decisions belong to Lua or explicit game commands.

---

# 📊 Current Project State

```text
Milestones 1–9
████████████████████ 100% ✅

Milestone 10
██████████████████░░  Core complete 🟡

Milestone 11
████░░░░░░░░░░░░░░░░  In development 🔵
```

### Proven foundation

LuaAPI currently provides the architectural foundation for:

- Native Lua 5.4 scripting
- Safe engine-backed object access
- Scenario lifecycle callbacks
- Destruction events
- Spatial queries
- Economy manipulation
- HUD messages
- Sub-frame damage interception
- Multi-turret state management
- Split-target combat
- Spawned missile interception
- CnCNet-oriented execution
- Logical-frame scripting

---

# 🛣️ Development Direction

The project is moving from:

```text
Engine Hook
     ↓
Lua Runtime
     ↓
Safe API
     ↓
Gameplay Events
     ↓
Advanced Combat
     ↓
Tactical Systems
     ↓
Programmable AI
```

The long-term objective is not simply to expose more functions.

It is to establish a stable abstraction layer that allows modders to implement mechanics that previously required hardcoded engine modifications.

---

# 📌 Documentation Synchronization

When the public API changes, the following documents should be reviewed together:

- `README.md` — Project overview
- `API.md` — Lua API reference
- `TUTORIAL.md` — Modder tutorial
- `CAPABILITIES.md` — Verified capabilities and recipes
- `PROJECT/ENGINEERING_CONTEXT.md` — Technical history and engine lessons
- `PROJECT/ROADMAP.md` — Development milestones and gates

A capability should not be marked **VERIFIED** in documentation until it has been implemented and tested against the current build.

---

# 🎓 Engineering Philosophy

> **Build small.**
>
> **Verify against the real engine.**
>
> **Keep unsafe work native.**
>
> **Keep gameplay logic scriptable.**
>
> **Document failures, not only successes.**
>
> **Do not call an engine limitation solved until the implementation survives real gameplay.**
