# 🧠 LuaAPI for Red Alert 2: Yuri's Revenge — Engineering Context & Memory Log

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current API Version:** `1.1.0` — Milestone 11  
> **Last Updated:** August 2026

## 📌 Executive Summary

LuaAPI is a native x86 Lua 5.4 scripting runtime embedded into Command & Conquer: Red Alert 2 — Yuri's Revenge 1.001. It exposes selected engine functionality to Lua while keeping unsafe engine interaction inside C++.

### Current Status

- **Milestones 1–9:** ✅ Complete and verified.
- **Milestone 10:** ✅ Core advanced-combat and multi-turret functionality verified.
- **Milestone 11:** 🚧 Current development baseline.
- **CnCNet compatibility:** ✅ Verified.
- **Lua mod-table callbacks:** ✅ Active.
- **Global `OnDebugCommand`:** ✅ Active.
- **Savegame-aware runtime initialization:** Supported through runtime checks in `Update()`.

> ⚠️ **Important:** `API.md` is the authoritative public API reference. Historical engineering notes must not be treated as public API guarantees unless verified in the current implementation.

---

# 🏛️ Architecture

```text
┌─────────────────────────────┐
│        Red Alert 2          │
│       gamemd.exe            │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│        C++ LuaAPI           │
│ • Engine hooks              │
│ • Pointer validation        │
│ • Runtime state             │
│ • Native engine calls       │
│ • Lua bindings              │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│           Lua 5.4           │
│ • Gameplay logic            │
│ • Target selection          │
│ • Mod behavior              │
│ • Event handling            │
│ • Debug tools               │
└─────────────────────────────┘
```

> **Core principle:** C++ manages engine state. Lua controls gameplay behavior.

C++ provides safe primitives, native operations, lifecycle handling, and runtime state. Lua decides gameplay behavior unless a native hook is explicitly required.

---

# 💀 Hall of Hard-Learned Lessons

## 1. 🔢 32-Bit Lepton Integer Overflow

RA2 uses 256 leptons per cell. Large spatial radii can overflow signed 32-bit squared-distance calculations.

```text
500 × 256 = 128,000 leptons
128,000² = 16,384,000,000
```

This exceeds `2,147,483,647`.

**Rule:** use sufficiently wide arithmetic for squared distances. For broad global searches, prefer `World.GetAllUnits()` instead of excessively large radii.

## 2. 💀 VTable Access on Destroyed Units

Engine-backed objects can become invalid immediately after destruction. Calling virtual methods on a destroyed object can produce `0xC0000005`.

**Rule:** validate objects before use and invalidate stored references when targets die. Native code interacting directly with unstable engine objects must use the project's safety mechanisms.

## 3. 🧹 Container Iterator Invalidation

Combat operations can indirectly destroy units while a container is being iterated.

**Rule:** use deferred cleanup. Queue removals during active iteration and process them after iteration has finished.

## 4. 🚀 SpawnManager and Guided Missile Ownership

Spawned missiles such as `DMISL` can remain controlled by the parent `SpawnManagerClass`. Changing a target alone may be overwritten by the engine.

The verified approach decouples the spawned child from its manager, clears the relevant spawn-node state, and applies the required native locomotor destination logic.

## 5. ⚔️ C++ Auto-Fire vs Passive Architecture

Autonomous targeting and firing inside `SubTurretManager::UpdateAll()` caused unintended attacks from idle units.

**Rule:** C++ manages turret state, timers, rotation, pointer safety, and bookkeeping. Lua decides targeting and firing.

## 6. 💾 Savegame Lifecycle

`OnScenarioStart()` is not a universal initialization event for loaded saves.

**Rule:** systems that require persistent runtime state must detect missing state during `Update()` and restore it when necessary. This also applies to units created after scenario initialization.

## 7. 🔗 Binding Identifier Synchronization

Do not invent binding names. Use the identifiers exported by the current implementation and documented in `API.md`.

When a binding changes, synchronize the implementation, `API.md`, `TUTORIAL.md`, `CAPABILITIES.md`, example mods, and engineering documentation.

## 8. 🚀 RocketLocomotor Pre-Computed Trajectory

`RocketLocomotor` can establish a ballistic trajectory during initialization. Changing a target after trajectory creation does not guarantee recalculation.

**Rule:** establish the intended target/destination before trajectory initialization, or use an appropriate native projectile mechanism. Do not document mid-flight retargeting as reliable without runtime verification.

---

# 🧩 Current Working Subsystems

## `SubTurretManager`

Primary files:

```text
src/sub_turret.h
src/sub_turret.cpp
```

Responsibilities include turret registration, state management, target references, reload timers, target invalidation, deferred cleanup, safe updates, split-target assignment, split-salvo execution, and spawned-missile processing.

`UpdateAll()` is intended to remain a passive state-management layer.

## Lua Bindings

Primary binding file:

```text
src/bindings_techno.cpp
```

The complete public contract is maintained in `API.md`. Do not use historical names unless they are present in the current bindings.

Representative namespaces include:

```lua
World.GetAllUnits()
World.GetUnitsInRadius(x, y, radius)
World.GetWaypoint(...)

House.GetPlayer()
house:GetCredits()
house:AddCredits(...)
house:GetName()
house:SpawnUnit(...)

unit:GetOwner()
unit:GetTypeName()
unit:GetPosition()
unit:IsAlive()
unit:GetHealthRatio()
unit:SetHealthRatio(...)
unit:AttachParticleSystem(...)
unit:AddSubTurret(...)
unit:GetSubTurretCount()
unit:SetSplitTargets(...)
unit:FireSplitSalvo()

Engine.PrintMessage(...)
```

> 📌 For exact signatures and the complete surface, consult `API.md`.

---

# 🎮 Lua Mod Lifecycle

Mods use the mod-table architecture:

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    -- Post-scenario initialization.
end

function MyMod.Update(frame)
    -- Gameplay logic.
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- Destruction handling.
end

return MyMod
```

The loader calls supported methods on the returned table.

`OnDebugCommand(text)` is a global development callback rather than a normal mod-table method:

```lua
function OnDebugCommand(text)
    -- Development command handling.
end
```

---

# 🧪 Showcase Mods

- `shield_overload` — pre-damage interception and defensive mechanics.
- `bounty_hunter` — combat rewards, owner lookup, credits, and messages.
- `damaged_fleet` — post-scenario unit modification and visual damage effects.
- `multi_turret_battleship` — sub-turrets, split targeting, and split-salvo combat.
- `spawn_test` — runtime unit creation and logical-frame scheduling.
- `debug_console` — global `OnDebugCommand` development tooling.

Only features actually verified against the current build should be presented as capabilities.

---

# 🌐 CnCNet Compatibility

CnCNet may launch the game through `gamemd-spawn.exe` rather than the standard executable.

The injector must resolve the running game module rather than assuming a fixed executable name.

LuaAPI may also encounter an existing main-loop hook installed by another modification. A signature mismatch is therefore not automatically an injection failure. Compatibility must be evaluated before treating the condition as fatal.

---

# ⏱️ Logical Frame Gating

Gameplay callbacks should use the game's logical frame rather than render FPS:

```text
120 FPS ─┐
90 FPS  ─┼──> Logical Game Frame ──> LuaAPI
60 FPS  ─┘
```

Example:

```lua
function MyMod.Update(frame)
    if frame % 30 ~= 0 then
        return
    end

    -- Execute once every 30 logical frames.
end
```

Logical-frame gating is important for multiplayer-sensitive gameplay, but it is not by itself proof that arbitrary Lua gameplay is fully deterministic.

---

# 🔧 Universal Engineering Principles

1. **Verify before documenting.** Source, bindings, tests, and reproducible runtime behavior outrank assumptions.
2. **Do not invent API names.** Check the current native bindings first.
3. **Separate verified from planned.** Roadmap items are not capabilities until implemented and tested.
4. **Preserve lifecycle safety.** Engine pointers can become invalid after destruction, transitions, and save/load.
5. **Prefer passive C++ infrastructure.** Keep gameplay decisions in Lua unless native code is explicitly required.
6. **Use logical frames for gameplay timing.** Do not rely on render FPS for synchronized gameplay state changes.
7. **Treat hook conflicts carefully.** An existing hook can be expected in a modded environment.
8. **Use small reproducible tests.** New engine interactions should have a minimal validation path before being documented.

---

# 🎯 Current Development Direction

Milestone 10 core advanced-combat functionality is complete. The deferred voxel-rendering component belongs to Milestone 11.

Current Milestone 11 priorities are:

1. Voxel Matrix Rendering for sub-turret presentation.
2. Z-Mode non-linear waypoint interception.
3. Dynamic salvo convergence after target destruction.
4. Tactical AI and naval intelligence systems.

These are development goals and must not be described as verified capabilities until implemented and tested.

---

# 📚 Documentation Authority

When documentation conflicts, use this order:

1. Current native implementation and bindings.
2. Verified runtime tests and showcase mods.
3. `API.md` — public API contract.
4. `CAPABILITIES.md` — verified capabilities and recipes.
5. `PROJECT/ROADMAP.md` — development status and planned work.
6. `PROJECT/AI_CONTEXT.md` — engineering context for AI-assisted development.

If a proposed change conflicts with the native implementation, verify the implementation first.