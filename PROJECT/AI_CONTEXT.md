# 🧠 LuaAPI for Red Alert 2: Yuri's Revenge — Engineering Context & Memory Log

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current API Version:** `1.1.0` — Milestone 11  
> **Last Updated:** August 2026

---

## 📌 Executive Summary for the Incoming AI Technical Director

LuaAPI is a native x86 Lua 5.4 scripting runtime embedded into **Command & Conquer: Red Alert 2 — Yuri's Revenge 1.001**.

The runtime exposes selected engine functionality to Lua while keeping unsafe engine interaction inside the C++ layer.

### Current Status

- **Milestones 1–9:** ✅ Complete and verified.
- **API 1.1.0 / Milestone 11:** Current development baseline.
- **Advanced combat systems:** Implemented and verified.
- **Multi-turret / split-salvo systems:** Implemented and verified.
- **CnCNet compatibility:** Implemented and verified.
- **Lua mod-table callback system:** Active.
- **Global `OnDebugCommand`:** Active.
- **Savegame-aware runtime initialization:** Supported through runtime checks in `Update()`.

> ⚠️ **Important:** Do not assume that every engine capability mentioned in historical notes is part of the public Lua API. The authoritative API surface is defined by `API.md`.

---

# 🏛️ Architecture

LuaAPI follows a strict separation between engine infrastructure and gameplay logic.

```text
┌─────────────────────────────┐
│        Red Alert 2          │
│       gamemd.exe            │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│        C++ LuaAPI           │
│                             │
│ • Engine hooks              │
│ • Pointer validation        │
│ • Runtime state             │
│ • Memory safety             │
│ • Native engine calls       │
│ • Lua bindings               │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│          Lua 5.4             │
│                             │
│ • Gameplay logic            │
│ • Target selection          │
│ • Mod behavior              │
│ • Event handling             │
│ • Debug tools                │
└─────────────────────────────┘
```

### Core Design Principle

> **C++ manages engine state. Lua controls gameplay behavior.**

C++ provides safe primitives and maintains runtime state.

Lua decides:

- Which targets to attack
- When to fire
- Which units to modify
- How gameplay mechanics behave
- How multiple engine primitives are combined

The C++ layer should not silently introduce gameplay behavior that the Lua mod did not request.

---

# 💀 Hall of Hard-Learned Lessons

These are engineering lessons discovered while debugging the native engine.

## 1. 🔢 The 32-Bit Lepton Integer Overflow Trap

Red Alert 2 uses **256 leptons per cell**.

A radius of 500 cells becomes:

```text
500 × 256 = 128,000 leptons
```

Squaring this value produces:

```text
128,000² = 16,384,000,000
```

This exceeds the maximum signed 32-bit integer:

```text
2,147,483,647
```

The squared distance can therefore overflow and become negative, causing spatial queries to return no units.

### Rule

Use 64-bit arithmetic for squared distances:

```cpp
int64_t dx = ...;
int64_t dy = ...;
int64_t distSq = dx * dx + dy * dy;
```

For broad global searches, prefer:

```lua
World.GetAllUnits()
```

instead of relying on excessively large spatial radii.

---

## 2. 💀 VTable Destruction on Dead Units

A `TechnoClass` object can become invalid immediately after destruction.

Calling a virtual method on a destroyed object can produce:

```text
0xC0000005
STATUS_ACCESS_VIOLATION
```

### Rule

Never assume an engine-backed object remains valid simply because Lua previously received it.

Use the API's validity and alive checks before accessing it.

Native code that interacts directly with unstable engine objects must protect unsafe access appropriately.

When a target dies, all systems storing references to it must invalidate those references.

---

## 3. 🧹 Container Iterator Invalidation

Combat processing can indirectly destroy a unit while a container is being iterated.

```text
UpdateAll()
    ↓
iterate m_turrets
    ↓
FireSplitSalvo()
    ↓
unit dies
    ↓
OnUnitDestroyed()
    ↓
erase(victim)
    ↓
active iterator becomes invalid
```

### Rule

Use deferred cleanup:

```text
Update
  ↓
Detect removal
  ↓
Queue removal
  ↓
Finish iteration
  ↓
Process removals
```

Never erase entries from a container while the active iteration depends on that container.

---

## 4. 🚀 SpawnManager and Guided Missile Ownership

Spawned missiles such as `DMISL` can remain controlled by the parent `SpawnManagerClass`.

Changing the missile target alone may not be sufficient because the engine can restore the parent's target during its update cycle.

### Decoupling Strategy

The verified approach involved:

1. Detaching the missile from its spawn owner.
2. Clearing the relevant spawn-node ownership state.
3. Allowing the spawn tube to regenerate.
4. Applying the required native locomotor destination logic.

Conceptually:

```text
SpawnManager
     │
     ▼
Spawned missile
     │
     ├── Parent ownership
     └── Precomputed trajectory
```

must become:

```text
SpawnManager ──X──> Missile

Missile
   │
   └── Independent trajectory
```

### Important Limitation

`RocketLocomotor` uses a precomputed ballistic trajectory. Changing a target after the trajectory has already been instantiated does not necessarily cause the engine to recompute that trajectory.

Therefore, target selection and trajectory initialization must be treated as separate lifecycle stages.

---

## 5. ⚔️ C++ Auto-Fire vs. Passive Engine Architecture

Early multi-turret implementations placed automatic targeting and firing inside `SubTurretManager::UpdateAll()`.

This created unintended attacks while units were idle or moving.

### Rule

The C++ update layer must remain passive.

It may manage:

- Turret state
- Timers
- Rotation
- Target validity
- Pointer safety
- Runtime bookkeeping

It should not autonomously decide that a unit must attack.

Gameplay decisions belong to Lua.

```text
C++:
"Turret is ready."

Lua:
"Select these targets and fire."

C++:
"Execute the requested engine operation safely."
```

---

## 6. 💾 Savegame Lifecycle

`OnScenarioStart()` is a fresh-scenario initialization callback. It should not be treated as a universal initialization event for every game lifecycle.

Loaded savegames can bypass the callback.

### Rule

Systems requiring runtime state restoration must detect missing state during `Update()`.

For example:

```lua
if unit:GetSubTurretCount() == 0 then
    unit:AddSubTurret(...)
end
```

This allows systems to recover state for:

- Loaded savegames
- Newly created units
- Units that did not exist during scenario initialization

---

## 7. 🔗 Binding Identifier Synchronization

Lua scripts must use identifiers actually exported by the current bindings.

Historical or assumed names such as:

```lua
unit:GetHouse()
unit:GetType()
```

must not be used unless they actually exist in the current API.

The current bindings expose names such as:

```lua
unit:GetOwner()
unit:GetTypeName()
```

### Rule

When an API name changes:

1. Update the C++ binding.
2. Update `API.md`.
3. Update `TUTORIAL.md`.
4. Update `CAPABILITIES.md`.
5. Update example mods.
6. Update relevant engineering documentation.

Documentation and implementation must evolve together.

---

## 8. 🚀 RocketLocomotor Pre-Computed Trajectory Trap

Attempting to steer an in-flight `DMISL` missile by changing its target or destination during flight does not guarantee a new trajectory.

`RocketLocomotor` computes its ballistic trajectory during initialization and does not continuously recompute navigation from a changed target.

### Rule

If a missile must follow a different trajectory, the target or destination must be established before the relevant trajectory is instantiated, or the system must use a native projectile mechanism capable of recalculating its trajectory.

Do not document mid-flight target reassignment as reliable `RocketLocomotor` behavior without verifying it against the current build.

---

# 🧩 Current Working Subsystems

## 1. `SubTurretManager`

Primary files:

```text
src/sub_turret.h
src/sub_turret.cpp
```

Responsibilities include:

- Turret registration
- Turret state management
- Target references
- Reload timers
- Target invalidation
- Deferred cleanup
- Safe updates
- Split-target assignment
- Split-salvo execution
- Spawned missile processing

Important functions include:

```text
AddTurret
GetTurrets
RemoveTechno
InvalidateTargetGlobally
ClearAll
UpdateAll
AssignSplitTargets
FireSplitSalvo
ProcessSpawnedMissiles
```

`UpdateAll()` is intended to remain a passive state-management layer.

---

## 2. Lua Bindings

Primary binding file:

```text
src/bindings_techno.cpp
```

The current public API is namespace-based.

### World

```lua
World.GetAllUnits()
World.GetUnits()
World.GetBuildings()
World.GetUnitsInRadius(x, y, radius)
World.GetWaypoint(...)
```

### Techno / Unit

```lua
unit:GetOwner()
unit:GetTypeName()
unit:GetPosition()
unit:GetCoords()
unit:IsAlive()
unit:IsInLimbo()
unit:GetHealth()
unit:GetMaxHealth()
unit:GetHealthRatio()
unit:SetHealthRatio(...)
unit:AttachParticleSystem(...)
unit:AddSubTurret(...)
unit:GetSubTurretCount()
unit:SetSplitTargets(...)
unit:FireSplitSalvo()
```

### House

```lua
House.GetPlayer()
House.GetCount()
House.GetByIndex(...)

house:GetName()
house:IsHuman()
house:IsAlliedWith(other)
house:GetCredits()
house:AddCredits(...)
house:GetPowerOutput()
house:GetPowerDrain()
house:SpawnUnit(...)
```

### Engine

```lua
Engine.PrintMessage(...)
```

### Game

```lua
Game.GetFrame()
Game.IsInMatch()
```

> 📌 `API.md` is the authoritative reference for the exact public API contract.

---

# 🎮 Lua Mod Lifecycle

Mods use the mod-table architecture.

A mod returns a table:

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    -- Initialization after scenario loading.
end

function MyMod.Update(frame)
    -- Gameplay logic.
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Damage interception.
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- Destruction callback.
end

return MyMod
```

The loader calls supported methods on the returned table.

## 🐞 Global Debug Callback

`OnDebugCommand` is different from mod-table callbacks.

It is a **global Lua function**:

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Debug: " .. text, 1)
end
```

Only one active mod should normally define this global callback.

---

# 🧪 Showcase Mods

## `shield_overload`

Demonstrates:

- `OnPreDamage`
- Damage interception
- Damage modification
- Damage cancellation
- Defensive mechanics

## `bounty_hunter`

Demonstrates:

- Combat callbacks
- `GetOwner()`
- House credit manipulation
- HUD messages
- Dynamic combat rewards

## `damaged_fleet`

Demonstrates:

- `OnScenarioStart()`
- Unit enumeration
- Owner filtering
- Health modification
- Visual damage effects

## `multi_turret_battleship`

Demonstrates:

- Sub-turrets
- Multiple target allocation
- Split targeting
- Split-salvo firing
- Runtime turret state
- Advanced combat mechanics

## `spawn_test`

Demonstrates:

- Runtime unit creation
- Logical-frame scheduling
- Development testing

## `debug_console`

Demonstrates the global `OnDebugCommand` callback and runtime development commands.

---

# 🌐 CnCNet Compatibility

LuaAPI supports CnCNet environments where the game process can differ from the standard executable.

The relevant process may be:

```text
gamemd-spawn.exe
```

instead of:

```text
gamemd.exe
```

## Injector Strategy

The injector resolves the running game process rather than assuming a fixed executable name.

```text
Start CnCNet
      ↓
gamemd-spawn.exe appears
      ↓
Injector detects process
      ↓
Resolve game module
      ↓
Wait for required environment
      ↓
Inject LuaAPI
```

## 🔗 Hook Compatibility

LuaAPI may encounter an existing hook installed by another engine modification.

A signature mismatch therefore does not automatically mean injection failure.

The environment must distinguish between:

```text
Unexpected / incompatible binary
```

and:

```text
Expected existing hook
```

MinHook-based chaining allows LuaAPI to coexist with existing hooks when the hook environment is compatible.

---

# ⏱️ Logical Frame Gating

Gameplay callbacks should be synchronized to the game's logical frame rather than the render rate.

```text
120 FPS ─┐
90 FPS  ─┼──> Logical Game Frame ──> LuaAPI
60 FPS  ─┘
```

This prevents gameplay logic from executing multiple times merely because a client renders more frames.

Example:

```lua
function MyMod.Update(frame)
    if frame % 30 ~= 0 then
        return
    end

    -- Execute once every 30 logical frames.
end
```

This is particularly important for multiplayer-sensitive gameplay logic.

---

# 🔧 Universal Engineering Principles

## 1. 🧠 C++ Manages State, Lua Controls Gameplay

C++ provides safe engine access, native operations, runtime state, pointer management, and hooks.

Lua provides gameplay decisions, target selection, event responses, mod behavior, and higher-level mechanics.

---

## 2. 🛡️ Validate Engine Objects

Engine-backed objects can become invalid after unit destruction, map transitions, scenario changes, savegame loading, and other lifecycle events.

Never assume that a previously obtained object remains valid.

---

## 3. 🧹 Defer Cleanup

Never invalidate an active iterator by modifying its underlying container during iteration.

Use:

```text
Detect
  ↓
Queue
  ↓
Finish iteration
  ↓
Cleanup
```

---

## 4. 💀 Invalidate Destroyed Targets

Any runtime system storing engine object references must remove references to destroyed objects.

This is especially important for turret targets, spawned missiles, combat systems, and cached unit references.

---

## 5. 💾 Test Savegame Loading

A system that works on a fresh scenario is not automatically correct for loaded saves.

Always test:

```text
Fresh scenario
      ↓
Gameplay
      ↓
Save
      ↓
Exit
      ↓
Load save
      ↓
Verify runtime state
```

---

## 6. 🔢 Watch Integer Widths

RA2's lepton coordinate system makes squared-distance calculations particularly susceptible to 32-bit overflow.

Use appropriate integer widths for large coordinate calculations.

---

## 7. ⏱️ Use Logical Frames

Gameplay timing should use the logical game frame where deterministic behavior matters.

Avoid wall-clock timing for synchronized gameplay logic.

---

## 8. 🔗 Treat Hook Conflicts Carefully

A signature mismatch is not automatically fatal.

Before treating a hook as incompatible, determine:

- Whether another modification already installed a hook
- Whether the target is still the expected function
- Whether chaining is possible
- Whether the resulting execution path is safe

---

# 🎯 Development Roadmap

The following items represent engineering work that should be treated as development goals rather than existing API guarantees.

## Milestone 10 — Advanced Combat

Potential remaining work includes:

- 3D voxel sub-turret rotation
- `TechnoClass::Draw` / `VoxelClass::Draw_Matrix` integration
- Advanced Z-mode waypoint interception
- Dynamic salvo convergence
- More sophisticated missile routing

## Milestone 11 — Tactical AI & Naval Intelligence

Planned areas include:

- `CellClass` water sampling
- Island detection
- Naval-map analysis
- Dynamic build queue overrides
- Tactical AI systems

> ⚠️ These features must not be described as public verified capabilities until they are implemented and tested against the current build.

---

# 📚 Documentation Hierarchy

```text
                    ┌──────────────┐
                    │   API.md     │
                    │ Source of    │
                    │ API Contract │
                    └──────┬───────┘
                           │
             ┌─────────────┴─────────────┐
             ▼                           ▼
      ┌──────────────┐            ┌──────────────┐
      │ TUTORIAL.md  │            │CAPABILITIES  │
      │ How to use   │            │ Proven use   │
      │ the API      │            │ cases        │
      └──────────────┘            └──────────────┘
             │                           │
             └─────────────┬─────────────┘
                           ▼
                  ┌──────────────────┐
                  │ PROJECT/*.md     │
                  │ Engineering      │
                  │ context/history  │
                  └──────────────────┘
```

### `API.md`

Defines the current public API.

### `TUTORIAL.md`

Explains how a modder can use the API.

### `CAPABILITIES.md`

Documents verified capabilities and practical recipes.

### `PROJECT/`

Contains engineering context, architecture, debugging history, implementation notes, and development plans.

> If these documents disagree, implementation and `API.md` take priority over historical engineering notes.

---

# 📌 Verification Policy

This document contains both verified engineering knowledge and development context.

Do not automatically treat planned or historical functionality as part of the current public API.

A feature should be considered:

```text
✅ VERIFIED
```

only when it has been implemented and tested against the current LuaAPI build.

A feature that is only designed or partially implemented should be marked:

```text
🚧 IN PROGRESS
```

A future idea should be marked:

```text
📋 PLANNED
```

This distinction is important for both human developers and AI coding agents.

---

# 🤖 Instructions for an AI Technical Director

When modifying LuaAPI:

1. Read `API.md` before using or inventing API functions.
2. Read `CAPABILITIES.md` before implementing mechanics already demonstrated by the project.
3. Read `TUTORIAL.md` to understand the intended Lua-facing workflow.
4. Use the `PROJECT/` documentation for engineering history and known engine traps.
5. Do not resurrect deprecated API names from historical code.
6. Do not document unverified behavior as production functionality.
7. Preserve multiplayer determinism when changing gameplay logic.
8. Treat engine pointers as unsafe until validated.
9. Avoid autonomous gameplay behavior inside passive C++ managers.
10. Update documentation when the public API changes.
11. Test fresh scenarios and savegame loading for runtime systems.
12. Prefer small, verifiable changes over large speculative rewrites.

---

# 🧭 Recommended Development Workflow

```text
Understand the current API
          ↓
Read relevant engineering context
          ↓
Build a minimal Lua prototype
          ↓
Verify engine behavior
          ↓
Identify unsafe native operations
          ↓
Move unsafe work into C++
          ↓
Expose a safe Lua binding
          ↓
Test fresh scenarios
          ↓
Test savegame loading
          ↓
Test multiplayer compatibility
          ↓
Document the verified capability
```

> **Build small. Test frequently. Verify before documenting.**
