# 🎓 LuaAPI Tutorial: Your First Mod

> **Prerequisites:** Basic Lua programming and familiarity with Red Alert 2 modding concepts  
> **Difficulty:** Beginner  
> **Time:** ~30 minutes

By the end of this tutorial, you will have a working LuaAPI mod that can inspect game objects, spawn units, and respond to game events.

> ⚠️ This tutorial documents the current LuaAPI interface. For the authoritative function signatures and return values, see [`API.md`](../API.md).

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

Extract LuaAPI into your Yuri's Revenge game directory:

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

Run the injector according to the installation instructions provided with the release.

After starting the game, check `LuaAPI.log` for messages indicating that the Lua engine and mod loader initialized successfully.

---

## Creating Your First Mod

### Step 1: Create the Mod Directory

Create a new directory inside `scripts/mods/`:

```text
scripts/mods/my_first_mod/
```

### Step 2: Create `main.lua`

Create `scripts/mods/my_first_mod/main.lua`:

```lua
local MyFirstMod = {}

function MyFirstMod.OnScenarioStart()
    Engine.PrintMessage("MyFirstMod loaded!")
end

function MyFirstMod.Update(frame)
    -- Called once per logical game frame.
    -- Add your gameplay logic here.
end

return MyFirstMod
```

A LuaAPI mod returns a **table**. The loader uses the functions defined on that table as lifecycle callbacks.

### Step 3: Enable the Mod

Open:

```text
scripts/active_mods.txt
```

Add the folder name:

```text
my_first_mod
```

The entry must match the mod directory name. Lines beginning with `#` are comments.

### Step 4: Test the Mod

Start a skirmish match. If everything is configured correctly, the message `MyFirstMod loaded!` will appear through the in-game message system.

---

## Understanding the API

LuaAPI organizes its implemented functionality into namespaces.

| Namespace | Purpose | Example |
|---|---|---|
| `House` | Player/house access and economy | `House.GetPlayer()` |
| `World` | Global unit, building, and map queries | `World.GetUnits()` |
| `Engine` | Engine/HUD functions | `Engine.PrintMessage("Hello")` |
| `game` | Lower-level diagnostics and legacy map/event-hook helpers | `game.GetEventHookOverrideCount()` |

LuaAPI exposes game objects such as units, buildings, infantry, and aircraft as validated Lua userdata.

Native validation protects the engine boundary, but scripts should still treat game objects as potentially invalid and use `IsAlive()` where appropriate.

> **Note:** The current `Game` namespace is not the source of the logical-frame API. Use the `frame` argument supplied to `Update(frame)` for gameplay timing.

---

## Working with Units

### Get and Inspect Units

`World.GetUnits()` returns mobile technos exposed by the current world query.

```lua
function MyFirstMod.Update(frame)
    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() then
            local name = unit:GetTypeName()
            local owner = unit:GetOwner()
            local hp = unit:GetHealth()
            local pos = unit:GetPosition()

            -- name: "HTNK", "E1", "DRED", etc.
            -- pos: { x = ..., y = ..., z = ... }
        end
    end
end
```

For a global scan that includes buildings, use `World.GetAllUnits()`.

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

    Engine.PrintMessage("You control " .. #mine .. " units")
end
```

### Spatial Queries

`World.GetUnitsInRadius()` searches for units around a map position.

```lua
local nearbyUnits = World.GetUnitsInRadius(baseX, baseY, 10)
```

The radius is specified in cells.

For large searches, keep the radius reasonable. RA2 uses 256 leptons per cell, so squared-distance calculations can overflow 32-bit integers at sufficiently large distances. Prefer `World.GetAllUnits()` when a global scan is more appropriate.

---

## Responding to Events

LuaAPI uses lifecycle callbacks defined as methods on the table returned by a mod. `OnDebugCommand` is a separate global callback.

### Lifecycle Callbacks

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    -- Scenario initialization callback.
end

function MyMod.Update(frame)
    -- Called once per logical game frame.
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Return a number to override the damage.
    -- Return 0 to cancel the damage.
    -- Return nil to leave the original damage unchanged.
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- killer may be nil.
end

return MyMod
```

### `Update(frame)`

`Update()` receives the current logical game frame.

```lua
function MyMod.Update(frame)
    if frame % 300 == 0 then
        Engine.PrintMessage("Five seconds passed")
    end
end
```

Use logical frames for gameplay timing rather than wall-clock time.

### `OnScenarioStart()`

Use this callback for post-scenario initialization:

```lua
function MyMod.OnScenarioStart()
    Engine.PrintMessage("Scenario initialized")
end
```

Do not assume this callback restores runtime state after loading a saved game. Systems that require runtime state must account for the savegame lifecycle explicitly.

### `OnPreDamage(...)`

`OnPreDamage` can modify incoming damage:

```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    local player = House.GetPlayer()

    if not player then
        return nil
    end

    if target and target:GetOwner() == player then
        return damage * 0.5
    end

    return nil
end
```

Return values:

- `number` — replaces the incoming damage.
- `0` — cancels the damage.
- `nil` — leaves the original damage unchanged.

Do not return negative damage values.

### `OnUnitDestroyed(victim, killer)`

Use this callback for destruction-related gameplay:

```lua
function MyMod.OnUnitDestroyed(victim, killer)
    if not victim then
        return
    end

    local victimType = victim:GetTypeName()
    local killerType = "unknown"

    if killer then
        killerType = killer:GetTypeName()
    end

    Engine.PrintMessage(
        victimType .. " destroyed by " .. killerType
    )
end
```

The `killer` argument may be `nil`.

---

## Global Debug Callback

`OnDebugCommand` is a **global Lua function**, not a method on the mod table.

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text)
end
```

Only one global definition should normally be active. Multiple mods defining the same global function can overwrite each other.

---

## Testing Your Mod

### Spawning Units

LuaAPI provides:

```lua
house:SpawnUnit(
    typeId,
    count,
    x,
    y,
    facing,
    force,
    action
)
```

Example:

```lua
function MyMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    local spawned = player:SpawnUnit(
        "APOC",
        5,
        100,
        100,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage(
        "Spawned " .. spawned .. " APOC"
    )
end
```

`SpawnUnit()` returns the number of units actually created. When normal spawning is used, the current implementation can search for a nearby valid cell according to its configured fallback behavior.

### Debug Console

A mod can provide the global `OnDebugCommand` callback:

```lua
function OnDebugCommand(text)
    local count, typeId = text:match("^(%d+)%s+(%u+)$")

    if not count or not typeId then
        Engine.PrintMessage("[DEBUG] Invalid command")
        return
    end

    local player = House.GetPlayer()
    if not player then
        return
    end

    local spawned = player:SpawnUnit(
        typeId,
        tonumber(count),
        100,
        100,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage(
        "[DEBUG] Spawned " .. spawned .. " " .. typeId
    )
end
```

The exact debug-input behavior depends on the current LuaAPI debug input implementation.

---

## ⚠️ Common Pitfalls

### 1. Do not blindly use destroyed objects

Avoid assuming that an object remains valid indefinitely.

Prefer:

```lua
if unit and unit:IsAlive() then
    local hp = unit:GetHealth()
end
```

### 2. Handle `OnPreDamage` correctly

If your callback does not need to modify damage, return `nil`:

```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end
```

To reduce damage:

```lua
return damage * 0.5
```

To cancel it:

```lua
return 0
```

### 3. Do not use wall-clock time for deterministic gameplay

Avoid:

```lua
os.time()
os.clock()
```

for gameplay decisions in multiplayer.

Use the logical frame instead:

```lua
function MyMod.Update(frame)
    if frame % 300 == 0 then
        -- Deterministic frame-based logic
    end
end
```

### 4. Mod does not load

Check:

```text
scripts/active_mods.txt
```

Make sure the entry exactly corresponds to the mod directory:

```text
scripts/mods/my_first_mod/
```

```text
my_first_mod
```

### 5. Do not assume `OnScenarioStart()` restores runtime state

If your mod maintains state that must survive save/load operations, verify the actual lifecycle behavior and design state initialization accordingly.

---

## 📚 Quick Reference

### Namespaces

```lua
House.GetPlayer()
House.GetCount()
House.GetByIndex(i)

World.GetBuildings()
World.GetUnits()
World.GetAllUnits()
World.GetUnitsInRadius(x, y, radius)
World.GetWaypoint(id)

Engine.PrintMessage(text)

game.GetEventHookOverrideCount()
game.ClearEventHookOverrides()
```

### Unit Methods

```lua
unit:GetOwner()
unit:GetTypeName()
unit:GetHealth()
unit:GetMaxHealth()
unit:IsAlive()
unit:GetPosition()
unit:GetDistanceTo(other)
unit:IsAttacking()
unit:GetTarget()
unit:GetId()
unit:GetKind()

unit:MoveTo(x, y)
unit:Scatter()
unit:Hunt()
unit:IsIdle()
unit:TakeDamage(amount, warhead)
unit:Disable(frames)

unit:SetHealthRatio(ratio)
unit:AttachParticleSystem(name)

unit:AddSubTurret(section, offX, offY, offZ, rot, rof)
unit:GetSubTurretCount()
unit:GetSubTurret(index)
unit:SetSubTurretTarget(index, target)
unit:FireSubTurret(index, target)
unit:ClearSubTurrets()
unit:SetSplitTargets(targets)
unit:FireSplitSalvo()
```

### House Methods

```lua
house:GetName()
house:IsHuman()
house:IsAlliedWith(otherHouse)
house:GetCredits()
house:SetCredits(amount)
house:AddCredits(amount)
house:GetPowerOutput()
house:GetPowerDrain()

house:SpawnUnit(typeId, count, x, y, facing, force, action)
```

### Lifecycle Callbacks

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

### Global Callback

```lua
function OnDebugCommand(text)
end
```

---

## 🚀 Next Steps

Once you understand the basics, explore the rest of the project:

- **[API Reference](../API.md)** — complete API reference and callback contracts.
- **[Capabilities & Cookbook](../PROJECT/CAPABILITIES.md)** — proven mechanics and implementation recipes.
- **Sample mods in `scripts/mods/`** — practical examples of LuaAPI usage.
- **[Architecture Roadmap](../PROJECT/ROADMAP.md)** — current development status and planned milestones.

Start with small scripts, verify behavior in-game, and use `LuaAPI.log` when debugging native/Lua interactions.
