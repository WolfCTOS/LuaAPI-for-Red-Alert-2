# 🎓 LuaAPI Tutorial: Your First Mod

> **Prerequisites:** Basic Lua programming and familiarity with Red Alert 2 modding
> **Difficulty:** Beginner
> **Time:** ~30 minutes
> **Target:** Yuri's Revenge 1.001

This tutorial walks you through creating your first LuaAPI mod. By the end, you will have a working mod that responds to game events, reads game objects, and can spawn units.

For complete API signatures and return-value details, see [`API.md`](../API.md).

---

## 📋 Table of Contents

1. [Installation & Setup](#installation--setup)
2. [Creating Your First Mod](#creating-your-first-mod)
3. [Understanding the API](#understanding-the-api)
4. [Working with Units](#working-with-units)
5. [Responding to Events](#responding-to-events)
6. [Testing Your Mod](#testing-your-mod)
7. [Multiplayer Considerations](#multiplayer-considerations)
8. [Common Pitfalls](#common-pitfalls)
9. [Next Steps](#next-steps)

---

## Installation & Setup

### Prerequisites

You need:

* **Red Alert 2: Yuri's Revenge 1.001**
* **LuaAPI**
* A text editor such as VS Code or Notepad++

### Installation

Extract the LuaAPI distribution into your Yuri's Revenge directory.

A typical installation looks like:

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

Check the distribution package and project documentation for the exact injector workflow used by your LuaAPI build.

After starting the game with LuaAPI, check `LuaAPI.log` for initialization messages.

---

# Creating Your First Mod

## Step 1: Create the Mod Directory

Create a directory under:

```text
scripts/mods/
```

For example:

```text
scripts/mods/my_first_mod/
```

## Step 2: Create `main.lua`

Create:

```text
scripts/mods/my_first_mod/main.lua
```

Start with:

```lua
local MyFirstMod = {}

function MyFirstMod.OnScenarioStart()
    Engine.PrintMessage("MyFirstMod loaded!", 1)
end

function MyFirstMod.Update(frame)
    -- Game logic goes here.
end

return MyFirstMod
```

The returned table is the mod's callback table. LuaAPI uses the functions defined on this table to dispatch supported events.

## Step 3: Enable the Mod

Open:

```text
scripts/active_mods.txt
```

Add:

```text
my_first_mod
```

The entry must correspond to the mod directory name.

For example:

```text
scripts/mods/my_first_mod/
```

corresponds to:

```text
my_first_mod
```

## Step 4: Start a Match

Launch the game using your normal LuaAPI injector workflow and start a scenario.

If the mod loads successfully, the game should display:

```text
MyFirstMod loaded!
```

through the in-game message system.

---

# Understanding the API

LuaAPI organizes its functionality into namespaces.

| Namespace | Purpose                          | Example                 |
| --------- | -------------------------------- | ----------------------- |
| `House`   | Player and house management      | `House.GetPlayer()`     |
| `World`   | Global game-world queries        | `World.GetUnits()`      |
| `Engine`  | Engine-level functions           | `Engine.PrintMessage()` |
| `Game`    | Game state and frame information | `Game.GetFrame()`       |

The API also exposes methods on game objects such as `TechnoClass` and `HouseClass`.

For the authoritative list of currently exposed functions, see [`API.md`](../API.md).

---

# Working with Units

## Getting Units

You can retrieve units through `World.GetUnits()`:

```lua
function MyFirstMod.Update(frame)
    local units = World.GetUnits()

    for _, unit in ipairs(units) do
        if unit:IsAlive() then
            local typeName = unit:GetTypeName()
            Engine.PrintMessage("Unit: " .. typeName, 1)
        end
    end
end
```

Avoid printing every unit every frame in a real mod. The example is intentionally simple.

A more practical approach is to run the operation periodically:

```lua
function MyFirstMod.Update(frame)
    if frame % 60 ~= 0 then
        return
    end

    local units = World.GetUnits()

    for _, unit in ipairs(units) do
        if unit:IsAlive() then
            local typeName = unit:GetTypeName()
            -- Your logic here.
        end
    end
end
```

`Update(frame)` is driven by the game's logical frame counter.

---

## Getting the Local Player

Use:

```lua
local player = House.GetPlayer()
```

Always handle the possibility that no player house is available in the current game state:

```lua
local player = House.GetPlayer()

if not player then
    return
end
```

You can then compare a unit's owner with the player house:

```lua
if unit:IsAlive() and unit:GetOwner() == player then
    -- Player-owned unit.
end
```

---

## Getting Unit Information

Common operations include:

```lua
local typeName = unit:GetTypeName()
local health = unit:GetHealth()
local maxHealth = unit:GetMaxHealth()
local position = unit:GetPosition()
local owner = unit:GetOwner()
```

For example:

```lua
function MyFirstMod.Update(frame)
    if frame % 60 ~= 0 then
        return
    end

    local units = World.GetUnits()

    for _, unit in ipairs(units) do
        if unit:IsAlive() then
            local position = unit:GetPosition()

            Engine.PrintMessage(
                unit:GetTypeName()
                .. " at "
                .. position.x
                .. ","
                .. position.y,
                1
            )
        end
    end
end
```

Consult [`API.md`](../API.md) for the complete object-method reference.

---

# Spatial Queries

LuaAPI provides spatial queries through `World.GetUnitsInRadius()`.

For example:

```lua
local nearbyUnits = World.GetUnitsInRadius(x, y, 10)
```

A practical example is finding units around one of the player's buildings:

```lua
function MyFirstMod.Update(frame)
    if frame % 60 ~= 0 then
        return
    end

    local player = House.GetPlayer()
    if not player then
        return
    end

    local basePosition = nil

    for _, building in ipairs(World.GetBuildings()) do
        if building:IsAlive() and building:GetOwner() == player then
            basePosition = building:GetPosition()
            break
        end
    end

    if not basePosition then
        return
    end

    local nearbyUnits = World.GetUnitsInRadius(
        basePosition.x,
        basePosition.y,
        10
    )

    Engine.PrintMessage(
        "Units near base: " .. #nearbyUnits,
        1
    )
end
```

---

# Responding to Events

LuaAPI exposes callback events through functions on your mod table.

A basic mod can look like this:

```lua
local MyFirstMod = {}

function MyFirstMod.OnScenarioStart()
    -- Scenario initialization.
end

function MyFirstMod.OnUnitDestroyed(victim, killer)
    -- Destruction event.
end

function MyFirstMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Damage interception.
    return damage
end

function MyFirstMod.OnDebugCommand(text)
    -- Debug command handling.
end

function MyFirstMod.Update(frame)
    -- Per-logical-frame logic.
end

return MyFirstMod
```

The supported callback names and their exact arguments are documented in [`API.md`](../API.md).

---

## OnScenarioStart

`OnScenarioStart()` is intended for scenario initialization.

For example:

```lua
function MyFirstMod.OnScenarioStart()
    Engine.PrintMessage("Scenario initialized.", 1)
end
```

Use this callback for initialization that should happen once when the scenario starts.

Do not assume that `OnScenarioStart()` is a general-purpose "Lua DLL loaded" callback. It is a gameplay/scenario lifecycle event.

---

## Update

`Update(frame)` receives the current logical frame:

```lua
function MyFirstMod.Update(frame)
    if frame % 300 == 0 then
        Engine.PrintMessage("5 seconds elapsed.", 1)
    end
end
```

The exact relationship between logical frames and real-time seconds depends on the game's simulation timing. Avoid using wall-clock functions for deterministic gameplay logic.

---

## OnPreDamage

`OnPreDamage()` can inspect or modify incoming damage:

```lua
function MyFirstMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
    local player = House.GetPlayer()

    if not player then
        return damage
    end

    if target and target:GetOwner() == player then
        return damage * 0.5
    end

    return damage
end
```

In this example, player-owned targets receive half of the incoming damage.

The callback can return:

* a number to replace the incoming damage value;
* `0` to cancel the damage;
* `nil` to leave the original damage unchanged.

Use the behavior documented in [`API.md`](../API.md) as the authoritative contract.

---

## OnUnitDestroyed

`OnUnitDestroyed(victim, killer)` is called when a supported unit or building destruction event occurs.

The victim may no longer be considered alive when the callback executes. Therefore, do not require `victim:IsAlive()` before processing the destruction event.

Example:

```lua
function MyFirstMod.OnUnitDestroyed(victim, killer)
    if not victim then
        return
    end

    local victimType = victim:GetTypeName()

    local killerType = "unknown"

    if killer then
        killerType = killer:GetTypeName()
    end

    Engine.PrintMessage(
        victimType .. " destroyed by " .. killerType,
        2
    )
end
```

If you need to inspect the victim's state after destruction, rely only on operations that are explicitly documented as valid for that event.

---

## OnDebugCommand

A mod can receive debug input through:

```lua
function MyFirstMod.OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text, 1)
end
```

The exact mechanism used to enter debug commands depends on the LuaAPI debug-console implementation. Refer to the project's current debug-console documentation for the input workflow.

---

# Testing Your Mod

## Spawning Units

LuaAPI exposes `SpawnUnit()` for development and gameplay scripting.

A simple example:

```lua
function MyFirstMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    local buildings = World.GetBuildings()
    local basePosition = nil

    for _, building in ipairs(buildings) do
        if building:IsAlive() and building:GetOwner() == player then
            basePosition = building:GetPosition()
            break
        end
    end

    if not basePosition then
        return
    end

    local spawned = player:SpawnUnit(
        "APOC",
        5,
        basePosition.x + 5,
        basePosition.y + 5,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage(
        "Spawned " .. spawned .. " Apocalypses.",
        1
    )
end
```

The complete `SpawnUnit()` parameter and behavior reference is maintained in [`API.md`](../API.md).

---

# Multiplayer Considerations

LuaAPI gameplay code must remain deterministic when used in multiplayer.

Do not base gameplay decisions on wall-clock time:

```lua
-- Avoid this in deterministic gameplay logic.
if os.time() % 10 == 0 then
    -- ...
end
```

Prefer the logical frame:

```lua
function MyFirstMod.Update(frame)
    if frame % 300 == 0 then
        -- Deterministic frame-based logic.
    end
end
```

For random gameplay decisions, ensure that all clients execute the same deterministic sequence. Do not introduce external or client-specific sources of randomness into synchronized gameplay logic.

Before releasing a multiplayer mod, test it on CnCNet and verify that no Out-of-Sync errors occur.

For the current LuaAPI/CnCNet integration workflow, see the relevant section of [`API.md`](../API.md).

---

# Common Pitfalls

## 1. Assuming Every Object Is Available

Game objects are tied to the game's lifecycle. Always handle missing references:

```lua
local player = House.GetPlayer()

if not player then
    return
end
```

For objects obtained from world queries:

```lua
for _, unit in ipairs(World.GetUnits()) do
    if unit:IsAlive() then
        -- Safe place to perform operations that require a live unit.
    end
end
```

Do not treat `IsAlive()` as a universal guarantee for every possible native operation. Follow the validity requirements documented for each API method.

---

## 2. Forgetting the `OnPreDamage` Return Value

If your callback is not intentionally changing or cancelling damage, pass the original value through:

```lua
function MyFirstMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
    return damage
end
```

Returning `nil` has a defined meaning for this callback: the original damage is preserved.

---

## 3. Using Wall-Clock Time for Gameplay Logic

Avoid:

```lua
os.time()
os.clock()
```

for synchronized gameplay decisions.

Use:

```lua
function MyFirstMod.Update(frame)
    if frame % 300 == 0 then
        -- Periodic deterministic logic.
    end
end
```

---

## 4. Running Expensive Logic Every Frame

This is legal:

```lua
function MyFirstMod.Update(frame)
    for _, unit in ipairs(World.GetUnits()) do
        -- ...
    end
end
```

But it may become unnecessarily expensive in large matches.

If the logic does not need to execute every frame, gate it:

```lua
function MyFirstMod.Update(frame)
    if frame % 30 ~= 0 then
        return
    end

    for _, unit in ipairs(World.GetUnits()) do
        -- ...
    end
end
```

Choose the interval according to the actual gameplay requirement.

---

# Advanced Example: Sub-Turrets

LuaAPI can expose advanced unit functionality such as sub-turrets.

A minimal example:

```lua
function MyFirstMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive()
            and unit:GetOwner() == player
            and unit:GetTypeName() == "DRED"
        then
            unit:AddSubTurret(
                1,
                40,
                0,
                15,
                12,
                90
            )

            unit:AddSubTurret(
                2,
                -40,
                0,
                15,
                12,
                90
            )

            unit:AddSubTurret(
                3,
                0,
                40,
                15,
                12,
                90
            )
        end
    end
end
```

For a complete multi-turret implementation, see:

[`CAPABILITIES_AND_COOKBOOK.md`](../PROJECT/CAPABILITIES_AND_COOKBOOK.md)

---

# 📚 Quick Reference

## Namespaces

```lua
House.GetPlayer()
House.GetCount()
House.GetByIndex(index)

World.GetBuildings()
World.GetUnits()
World.GetAllUnits()
World.GetUnitsInRadius(x, y, radius)
World.GetWaypoint(id)

Engine.PrintMessage(text, colorIndex)

Game.GetFrame()
Game.IsInMatch()
```

## Common Unit Methods

```lua
unit:GetOwner()
unit:GetTypeName()
unit:GetHealth()
unit:GetMaxHealth()
unit:IsAlive()
unit:GetPosition()
unit:GetDistanceTo(otherUnit)

unit:IsAttacking()
unit:TakeDamage(amount, warhead)
unit:Disable(frames)
```

## House Methods

```lua
house:GetName()
house:IsHuman()
house:IsAlliedWith(otherHouse)
house:GetCredits()
house:AddCredits(amount)

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

## Events

```lua
function MyMod.OnScenarioStart()
end

function MyMod.OnUnitDestroyed(victim, killer)
end

function MyMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
end

function MyMod.OnDebugCommand(text)
end

function MyMod.Update(frame)
end
```

For complete signatures, parameters, return values, and behavioral constraints, use [`API.md`](../API.md).

---

# 🚀 Next Steps

After completing this tutorial, explore:

* [`API.md`](../API.md) — Complete API reference
* [`CAPABILITIES_AND_COOKBOOK.md`](../PROJECT/CAPABILITIES_AND_COOKBOOK.md) — Proven mechanics and practical recipes
* [`ENGINEERING_LESSONS.md`](ENGINEERING_LESSONS.md) — Native-engine and implementation lessons
* [`MOD_MANAGER.md`](MOD_MANAGER.md) — Mod loading and distribution
* [`ROADMAP.md`](ROADMAP.md) — Project development status

The recommended progression is:

```text
Tutorial
   ↓
API Reference
   ↓
Cookbook / Examples
   ↓
Advanced Mod Development
```

Start with small deterministic scripts, validate game objects before operating on them, and test gameplay changes in both single-player and multiplayer environments.
