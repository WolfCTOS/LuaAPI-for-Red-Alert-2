# 🎓 LuaAPI Tutorial: Your First Mod

> **Prerequisites:** Basic Lua programming and familiarity with Red Alert 2 modding concepts
> **Difficulty:** Beginner
> **Time:** ~30 minutes

By the end of this tutorial, you will have a working LuaAPI mod that can inspect game objects, spawn units, and respond to game events.

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

* **Red Alert 2: Yuri's Revenge 1.001**
* A LuaAPI release
* A text/code editor such as VS Code or Notepad++

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
    Engine.PrintMessage("MyFirstMod loaded!", 1)
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
# scripts/active_mods.txt
my_first_mod
```

The entry must match the mod directory name.

Lines beginning with `#` are comments.

### Step 4: Test the Mod

Start a skirmish match.

If everything is configured correctly, the message:

```text
MyFirstMod loaded!
```

will appear in the in-game message ticker.

---

## Understanding the API

LuaAPI organizes its functionality into namespaces.

| Namespace | Purpose                          | Example                 |
| --------- | -------------------------------- | ----------------------- |
| `House`   | Player and house management      | `House.GetPlayer()`     |
| `World`   | Global game-world queries        | `World.GetUnits()`      |
| `Engine`  | Engine-level functions           | `Engine.PrintMessage()` |
| `Game`    | Game state and frame information | `Game.GetFrame()`       |

LuaAPI exposes game objects such as units, buildings, and houses as Lua userdata.

Native objects are validated by the API before exposed operations are performed. Scripts should still treat game objects as potentially invalid and use `IsAlive()` where appropriate.

---

## Working with Units

### Get and Inspect Units

`World.GetUnits()` returns the units currently exposed by the world query.

```lua
function MyFirstMod.Update(frame)
    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() then
            local name = unit:GetTypeName()
            local owner = unit:GetOwner()
            local hp = unit:GetHealth()
            local pos = unit:GetPosition()

            -- name: "HTNK", "E1", "DRED", etc.
            -- pos: { x = ..., y = ... }
        end
    end
end
```

### Filter Your Own Units

You can compare a unit's owner with the local player house:

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

    Engine.PrintMessage("You control " .. #mine .. " units", 1)
end
```

### Spatial Queries

`World.GetUnitsInRadius()` searches for units around a map position.

```lua
local nearbyUnits = World.GetUnitsInRadius(baseX, baseY, 10)
```

The radius is specified in cells.

A typical use is finding units around a building:

```lua
local player = House.GetPlayer()
if not player then
    return
end

local basePos

for _, building in ipairs(World.GetBuildings()) do
    if building:IsAlive() and building:GetOwner() == player then
        basePos = building:GetPosition()
        break
    end
end

if basePos then
    local nearbyUnits =
        World.GetUnitsInRadius(basePos.x, basePos.y, 10)

    Engine.PrintMessage(
        "Units near base: " .. #nearbyUnits,
        1
    )
end
```

---

## Responding to Events

LuaAPI has two callback mechanisms:

1. **Lifecycle callbacks** defined as methods on the table returned by a mod.
2. **The global `OnDebugCommand` callback** used by the debug input layer.

### Lifecycle Callbacks

A mod can define the following callbacks:

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    -- Called once when scenario initialization reaches
    -- the scenario-start callback.
end

function MyMod.Update(frame)
    -- Called once per logical game frame.
end

function MyMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
    -- Return a number to override the damage.
    -- Return 0 to cancel the damage.
    -- Return nil to leave the original damage unchanged.

    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- Called when a unit or building is destroyed.
    -- killer may be nil.
end

return MyMod
```

### `Update(frame)`

`Update()` receives the current logical game frame.

```lua
function MyMod.Update(frame)
    if frame % 300 == 0 then
        Engine.PrintMessage("Five seconds passed", 1)
    end
end
```

Use logical frames for gameplay timing rather than wall-clock time.

### `OnScenarioStart()`

Use `OnScenarioStart()` for scenario initialization:

```lua
function MyMod.OnScenarioStart()
    Engine.PrintMessage("Scenario initialized", 1)
end
```

Do not assume that this callback is invoked again when loading a saved game unless the current LuaAPI implementation explicitly guarantees that behavior.

### `OnPreDamage(...)`

`OnPreDamage` can modify incoming damage:

```lua
function MyMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
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

* `number` — replaces the incoming damage.
* `0` — cancels the damage.
* `nil` — leaves the original damage unchanged.

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
        victimType .. " destroyed by " .. killerType,
        2
    )
end
```

The `killer` argument may be `nil`.

---

## Global Debug Callback

`OnDebugCommand` is different from the lifecycle callbacks.

It is a **global Lua function**, not a method on the mod table.

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text, 1)
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

    local baseX, baseY = 100, 100

    for _, building in ipairs(World.GetBuildings()) do
        if building:IsAlive() and building:GetOwner() == player then
            local pos = building:GetPosition()

            baseX = math.floor(pos.x)
            baseY = math.floor(pos.y)
            break
        end
    end

    local spawned = player:SpawnUnit(
        "APOC",
        5,
        baseX + 5,
        baseY + 5,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage(
        "Spawned " .. spawned .. " APOC",
        1
    )
end
```

`SpawnUnit()` returns the number of units actually created.

When `force` is `false`, blocked spawn locations can be searched for a nearby valid cell according to the current implementation.

### Debug Console

A mod can provide the global `OnDebugCommand` callback:

```lua
function OnDebugCommand(text)
    local count, typeId =
        text:match("^(%d+)%s+(%u+)$")

    if not count or not typeId then
        Engine.PrintMessage(
            "[DEBUG] Invalid command",
            2
        )
        return
    end

    count = tonumber(count)

    local player = House.GetPlayer()

    if not player then
        return
    end

    local baseX, baseY = 100, 100

    for _, building in ipairs(World.GetBuildings()) do
        if building:IsAlive() and building:GetOwner() == player then
            local pos = building:GetPosition()

            baseX = math.floor(pos.x)
            baseY = math.floor(pos.y)
            break
        end
    end

    local spawned = player:SpawnUnit(
        typeId,
        count,
        baseX + 5,
        baseY + 5,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage(
        "[DEBUG] Spawned "
            .. spawned
            .. " "
            .. typeId,
        1
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
function MyMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
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

If your mod maintains state that must survive save/load operations, verify the actual lifecycle behavior and design the state initialization accordingly.

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

Engine.PrintMessage(text, color)

Game.GetFrame()
Game.IsInMatch()
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

unit:TakeDamage(amount, warhead)
unit:Disable(frames)

unit:AddSubTurret(
    section,
    offX,
    offY,
    offZ,
    rot,
    rof
)

unit:SetSplitTargets(targets)
unit:FireSplitSalvo()

unit:SetHealthRatio(ratio)
unit:AttachParticleSystem(sysName)
```

### House Methods

```lua
house:GetName()
house:IsHuman()
house:IsAlliedWith(other)

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

### Lifecycle Callbacks

```lua
function MyMod.Update(frame)
end

function MyMod.OnScenarioStart()
end

function MyMod.OnPreDamage(
    attacker,
    target,
    damage,
    dmgType,
    frame,
    subc
)
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

* **[API.md](API.md)** — complete API reference and callback contracts.
* **[CAPABILITIES_AND_COOKBOOK.md](PROJECT/CAPABILITIES_AND_COOKBOOK.md)** — proven mechanics and implementation recipes.
* **Sample mods in `scripts/mods/`** — practical examples of LuaAPI usage.
* **[ROADMAP.md](ROADMAP.md)** — current development status and planned milestones.

Start with small scripts, verify behavior in-game, and use `LuaAPI.log` when debugging native/Lua interactions.
