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

After starting the game, inspect `LuaAPI.log` if you need to diagnose loading or script errors.

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

function MyFirstMod.OnScenarioStart()
    Engine.PrintMessage("MyFirstMod loaded!")
end

function MyFirstMod.Update(frame)
    -- Called once per logical game frame.
end

return MyFirstMod
```

A mod returns a Lua table. The loader calls lifecycle methods defined on that table.

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

Start a skirmish. The `OnScenarioStart()` callback should display the message through the engine message system.

---

## Understanding the API

LuaAPI exposes implemented functionality through namespaces and validated engine-backed objects.

| Namespace / Object | Purpose | Example |
|---|---|---|
| `House` | House/player access and economy | `House.GetPlayer()` |
| `World` | Unit, building, and map queries | `World.GetUnits()` |
| `Engine` | Engine/HUD helpers | `Engine.PrintMessage("Hello")` |
| `game` | Lower-level/legacy diagnostics and map helpers | `game.GetEventHookOverrideCount()` |
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

### `OnScenarioStart()`

Use it for post-scenario initialization:

```lua
function MyFirstMod.OnScenarioStart()
    Engine.PrintMessage("Scenario initialized")
end
```

Do not assume this callback restores runtime state after a savegame is loaded. Systems that require runtime state must handle that lifecycle explicitly.

### `OnPreDamage(...)`

This callback runs at the damage-processing boundary and can replace incoming damage:

```lua
function MyFirstMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    local player = House.GetPlayer()

    if player and target and target:GetOwner() == player then
        return damage * 0.5
    end

    return nil
end
```

Return values:

- `number` — replaces the incoming damage.
- `0` — cancels the damage.
- `nil` — leaves the original damage unchanged.

Never return negative damage values.

### `OnUnitDestroyed(victim, killer)`

```lua
function MyFirstMod.OnUnitDestroyed(victim, killer)
    if not victim then
        return
    end

    local victimType = victim:GetTypeName()
    local killerType = killer and killer:GetTypeName() or "unknown"

    Engine.PrintMessage(
        victimType .. " destroyed by " .. killerType
    )
end
```

`killer` may be `nil` for engine-side causes such as environmental damage or other non-unit sources.

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
function MyFirstMod.OnScenarioStart()
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

The return value is the number of units actually created. Normal spawning can use the implementation's nearby-cell fallback when the requested location is unavailable.

### Sub-Turrets

Milestone 11 also exposes explicit sub-turret state and firing:

```lua
unit:AddSubTurret(1, 40, 0, 15, 12, 90)
unit:SetSubTurretTarget(1, enemy)
unit:FireSubTurret(1, enemy)
```

For split-salvo behavior:

```lua
unit:SetSplitTargets({enemyA, enemyB, enemyC})
unit:FireSplitSalvo()
```

Target acquisition remains Lua gameplay logic; the native layer maintains turret state and performs the explicit operation requested by Lua.

---

## ⚠️ Common Pitfalls

### 1. Do not assume engine objects remain valid

```lua
if unit and unit:IsAlive() then
    local hp = unit:GetHealth()
end
```

`IsAlive()` checks liveness at that moment. It does not make a stored engine reference permanently safe.

### 2. Handle `OnPreDamage` correctly

Pass damage through:

```lua
return nil
```

Reduce it:

```lua
return damage * 0.5
```

Cancel it:

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
unit:TakeDamage(amount, warhead)
unit:Disable(frames)
unit:SetHealthRatio(ratio)
unit:AttachParticleSystem(name)
```

### Sub-Turret

```lua
unit:AddSubTurret(section, offX, offY, offZ, rot, rof)
unit:GetSubTurretCount()
unit:GetSubTurret(index)
unit:SetSubTurretTarget(index, target)
unit:FireSubTurret(index, target)
unit:ClearSubTurrets()
unit:SetSplitTargets(targets)
unit:FireSplitSalvo()
```

### Engine / Diagnostics

```lua
Engine.PrintMessage(text)

game.GetEventHookOverrideCount()
game.ClearEventHookOverrides()
```

### Lifecycle

```lua
function MyMod.Update(frame)
end

function MyMod.OnScenarioStart()
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
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
