# 🎓 LuaAPI Tutorial: Your First Mod

> **Prerequisites:** Basic Lua programming and familiarity with Red Alert 2 modding concepts  
> **Difficulty:** Beginner  
> **Time:** ~30 minutes

This tutorial builds a small LuaAPI mod that reads game objects, modifies units, spawns units, and responds to engine events.

> ⚠️ This tutorial describes the current implemented interface. For authoritative signatures and return values, see [`API.md`](../API.md).

---

## 📋 Table of Contents

1. [Installation & Setup](#installation--setup)
2. [Creating Your First Mod](#creating-your-first-mod)
3. [Understanding the API](#understanding-the-api)
4. [Working with Units](#working-with-units)
5. [Responding to Events](#responding-to-events)
6. [Testing Your Mod](#testing-your-mod)
7. [Common Pitfalls](#common-pitfalls)
8. [Quick Reference](#quick-reference)
9. [Next Steps](#next-steps)

---

## Installation & Setup

### Prerequisites

You need:

- **Red Alert 2: Yuri's Revenge 1.001**
- A LuaAPI release
- A text/code editor such as VS Code or Notepad++

### Install LuaAPI

Extract the release into the Yuri's Revenge directory. A typical installation contains:

```text
Yuri's Revenge/
├── gamemd.exe
├── LuaAPI.dll
├── injector.exe
└── scripts/
    ├── init.lua
    ├── active_mods.txt
    └── mods/
```

Run the injector according to the release instructions.

After starting the game, inspect `LuaAPI.log` (written next to `LuaAPI.dll`) if you need to diagnose loading or script errors.

---

## Creating Your First Mod

### Step 1: Create the Mod Directory

Create:

```text
scripts/mods/my_first_mod/
```

### Step 2: Create `main.lua`

```lua
local MyFirstMod = {}

function MyFirstMod.Update(frame)
    if frame == 30 then
        Engine.PrintMessage("MyFirstMod is running!")
    end
end

return MyFirstMod
```

A mod returns a Lua table. The loader dispatches the table's `Update(frame)`
method every logical frame. Other engine callbacks (`OnScenarioStart`,
`OnUnitDestroyed`) are looked up as **global** functions in the current build —
defining them as mod-table methods does not wire them (see
[Responding to Events](#responding-to-events)).

### Step 3: Enable the Mod

Open:

```text
scripts/active_mods.txt
```

Add:

```text
my_first_mod
```

The entry must match the directory name. Lines beginning with `#` are comments.

### Step 4: Test

Start a skirmish. Thirty frames in, `Update` should display the message through
the engine message system. If nothing appears, inspect `LuaAPI.log` for loader
errors (see [Common Pitfalls](#common-pitfalls)).

---

## Understanding the API

LuaAPI exposes implemented functionality through namespaces and validated engine-backed objects.

| Namespace / Object | Purpose | Example |
|---|---|---|
| `House` | House/player access and economy | `House.GetPlayer()` |
| `World` | Unit, building, and map queries | `World.GetUnits()` |
| `Engine` | Engine/HUD helpers | `Engine.PrintMessage("Hello")` |
| `game` | Lower-level map helpers | `game.GetUnitsInRadius()` |
| `Techno` | Methods available on engine objects | `unit:GetTypeName()` |

The native layer validates engine objects before exposing or operating on them. Lua references should still be treated as short-lived because an engine object can become invalid after destruction or a session transition.

For gameplay timing, use the `frame` argument supplied to `Update(frame)`. Do not substitute wall-clock time for deterministic gameplay logic.

---

## Working with Units

### Inspect Units

`World.GetUnits()` returns mobile technos such as vehicles, infantry, and aircraft.

```lua
function MyFirstMod.Update(frame)
    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() then
            local typeName = unit:GetTypeName()
            local owner = unit:GetOwner()
            local hp = unit:GetHealth()
            local pos = unit:GetPosition()

            -- typeName: "APOC", "E1", "DRED", etc.
            -- pos: { x = ..., y = ..., z = ... }
        end
    end
end
```

For a global techno scan that includes buildings, use `World.GetAllUnits()`.

### Filter Your Own Units

```lua
function MyFirstMod.Update(frame)
    local player = House.GetPlayer()
    if not player then
        return
    end

    local mine = {}

    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() and unit:GetOwner() == player then
            table.insert(mine, unit)
        end
    end

    Engine.PrintMessage("You control " .. #mine .. " mobile units")
end
```

### Spatial Queries

```lua
local nearby = World.GetUnitsInRadius(100, 100, 10)
```

The radius is specified in map cells. RA2 uses 256 leptons per cell. Avoid unnecessarily large radius searches; for whole-map scans, prefer `World.GetAllUnits()`.

---

## Responding to Events

Lifecycle callbacks are methods on the mod table returned from `main.lua`. `OnDebugCommand` is different: it is a global Lua callback.

### `Update(frame)`

```lua
function MyFirstMod.Update(frame)
    if frame % 300 == 0 then
        Engine.PrintMessage("Five seconds of logical game time")
    end
end
```

Use logical frames for deterministic gameplay timing.

### `OnScenarioStart()` — global

Defined as a **global** function, it runs once at `CurrentFrame == 1` after
scenario initialization:

```lua
function OnScenarioStart()
    Engine.PrintMessage("Scenario initialized")
end
```

> ⚠️ Defining `MyFirstMod.OnScenarioStart` (mod-table method) does **not** fire
> in the current build. Only the global lookup is dispatched.

Do not assume this callback restores runtime state after a savegame is loaded —
it does not fire on savegame load. Systems that require runtime state must
handle that lifecycle explicitly.

### `OnPreDamage(...)` — NOT WIRED in the current build

> ⚠️ **This callback does not fire today.** The engine collects the global
> `OnPreDamage` reference but never invokes it with damage arguments (no
> `ReceiveDamage` hook is installed). The contract below is the intended
> design, kept for when interception is wired. Do not build anything
> damage-reactive on it yet — authoritative status: [`API.md`](../API.md),
> Callback Model.

Intended contract (global function, damage-processing boundary):

```lua
function OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    local player = House.GetPlayer()

    if player and target and target:GetOwner() == player then
        return damage * 0.5
    end

    return nil
end
```

Intended return values:

- `number` — replaces the incoming damage.
- `0` — cancels the damage.
- `nil` — leaves the original damage unchanged.

Never return negative damage values.

For damage-adjacent behavior that **does** work today, observe aftermath from
`Update` (HP drops between scans) — see the Command Authority mod for the
proven pattern.

### `OnUnitDestroyed(victim, killer)` — not dispatched in the current build

Defined as a **global** function, but the C++ dispatch branch is unreachable
(nothing ever queues the callback), so it **never fires** — there is no
`(nil, nil)` placeholder call either. Nil-guard and do not build
death-reactive logic on it:

```lua
function OnUnitDestroyed(victim, killer)
    if not victim or not killer then
        return -- current build: never dispatched
    end
end
```

For real death detection today, diff ID-keyed scans between frames
(disappearance = probable death) — the proven pattern in the Command Authority
mod.

---

## Global Debug Callback

`OnDebugCommand` is a global function and is not attached to the returned mod table.

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text)
end
```

Only one active definition should normally exist. Multiple mods defining the same global callback can overwrite one another.

---

## Testing Your Mod

### Spawning Units

The current high-level spawn helper is:

```lua
house:SpawnUnit(typeId, count, x, y, facing, force, action)
```

Example:

```lua
function OnScenarioStart()
    local player = House.GetPlayer()
    if not player then
        return
    end

    local created = player:SpawnUnit(
        "APOC",
        5,
        100,
        100,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage("Spawned " .. created .. " APOC")
end
```

> Scenario-start handlers must be **globals** (see above) — the
> mod-table form `MyFirstMod.OnScenarioStart` never fires.

The return value is the number of units actually created. Normal spawning can use the implementation's nearby-cell fallback when the requested location is unavailable.

### Sub-Turrets — REMOVED 2026-09-21

> The Milestone 10 sub-turret / split-salvo API was removed (zero live
> consumers). Do not use `AddSubTurret` / `SetSplitTargets` /
> `FireSplitSalvo` — the bindings no longer exist. History in
> `PROJECT/ROADMAP.md` (M10) and git.

---

## ⚠️ Common Pitfalls

### 1. Do not assume engine objects remain valid

```lua
if unit and unit:IsAlive() then
    local hp = unit:GetHealth()
end
```

`IsAlive()` checks liveness at that moment. It does not make a stored engine reference permanently safe.

### 2. `OnPreDamage` is NOT wired; `OnUnitDestroyed` is never dispatched

The damage-interception callback **does not fire** in the current build, and
`OnUnitDestroyed` **never fires either** (no placeholder call). See the events section above for
workable alternatives (aftermath polling, ID-diff death detection).

Pass damage through (intended contract only):

```lua
return nil
```

Reduce it (intended contract only):

```lua
return damage * 0.5
```

Cancel it (intended contract only):

```lua
return 0
```

Do not return negative damage.

### 3. Do not use wall-clock time for deterministic gameplay

Avoid using `os.time()` or `os.clock()` for gameplay decisions that must remain synchronized in multiplayer.

Use:

```lua
function MyFirstMod.Update(frame)
    if frame % 300 == 0 then
        -- deterministic frame-based logic
    end
end
```

### 4. Mod does not load

Check:

```text
scripts/active_mods.txt
```

The entry must exactly match the directory:

```text
scripts/mods/my_first_mod/
```

```text
my_first_mod
```

Also inspect `LuaAPI.log` for loader or script errors.

### 5. Savegame behavior

Do not assume `OnScenarioStart()` runs after loading a saved game. If your mod creates runtime state, verify and restore that state as required during subsequent updates.

### 6. Large spatial searches

RA2 coordinates use 256 leptons per cell. Large squared-distance calculations can exceed 32-bit integer range. Prefer a reasonable radius or use `World.GetAllUnits()` for global scans.

### 7. `OnDebugCommand` is global

It is not a method on the returned mod table. Avoid defining competing global implementations across multiple mods.

---

## 📚 Quick Reference

### House

```lua
local player = House.GetPlayer()
local count = House.GetCount()
local house = House.GetByIndex(0)

house:GetName()
house:IsHuman()
house:IsAlliedWith(otherHouse)
house:GetCredits()
house:SetCredits(5000)
house:AddCredits(500)
house:GetPowerOutput()
house:GetPowerDrain()
house:SpawnUnit("APOC", 1, 100, 100, 0, false, "hunt")
```

### World

```lua
World.GetBuildings()
World.GetUnits()
World.GetAllUnits()
World.GetUnitsInRadius(x, y, radius)
World.GetWaypoint(id)
```

### Unit

```lua
unit:GetOwner()
unit:GetTypeName()
unit:GetHealth()
unit:GetMaxHealth()
unit:IsAlive()
unit:GetPosition()
unit:GetDistanceTo(other)
unit:GetId()
unit:GetKind()
unit:IsAttacking()
unit:GetTarget()
unit:IsIdle()
unit:MoveTo(x, y)
unit:Scatter()
unit:Hunt()
unit:TakeDamage(amount, [warhead])
unit:Disable(frames)
unit:SetHealthRatio(percent) -- 0-100 scale (35 = 35%); fractional 0-1 inputs do NOT work as fractions
unit:AttachParticleSystem(name)
```

### Engine / Diagnostics

```lua
Engine.PrintMessage(text)
```

### Lifecycle

```lua
function MyMod.Update(frame)
end

-- Engine event callbacks are GLOBALS (current build), not mod-table methods.
function OnScenarioStart()
end

-- NOT WIRED in the current build — intended contract only.
function OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end

-- Global; NEVER dispatched in the current build (contract without
-- invocation — use ID-diff death detection instead).
function OnUnitDestroyed(victim, killer)
end
```

### Global Debug Callback

```lua
function OnDebugCommand(text)
end
```

---

## 🚀 Next Steps

- **[API Reference](../API.md)** — authoritative interface and callback contract.
- **[Capabilities & Cookbook](../PROJECT/CAPABILITIES.md)** — verified mechanics and practical recipes.
- **Sample mods in `scripts/mods/`** — working examples.
- **[Architecture Roadmap](../PROJECT/ROADMAP.md)** — development status and milestones.

The recommended workflow is simple: start with a small script, test it in-game, verify the behavior, and document only what the current build actually supports.
