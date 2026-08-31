# LuaAPI for Red Alert 2 — API Reference 

> **Version:** 1.1.0
> **Milestone:** 11
> **Target:** `gamemd.exe` — Yuri's Revenge 1.001
> **Last Updated:** 2026-08-31
> **Safety:** Protected by RTTI (`WhatAmI()`) validation and SEH against `0xC0000005`.

This document is the authoritative reference for the LuaAPI public scripting interface.

It documents the namespaces, object methods, callbacks, game-state queries, multiplayer requirements, CnCNet integration, and mod-loading behavior available in the current implementation.

---

## 🏗️ API Architecture

LuaAPI uses a namespace-based architecture:

* **`House`** — Player and house management
* **`World`** — Global queries for units, buildings, and spatial searches
* **`Engine`** — Game engine functions such as HUD messages
* **`Game`** — Game state and logical frame information
* **`AI`** — AI control functions *(planned; not currently available)*

Object methods operate on validated game objects exposed to Lua.

> **Important:** This document describes the implemented public API. Planned or experimental functionality must not be treated as stable API.

---

## 🛡️ Pointer Safety & Session Lifecycle

LuaAPI crosses the boundary between Lua and the native RA2 engine. Native pointers can become invalid when game objects are destroyed or when the game session changes.

LuaAPI therefore performs defensive validation at the native boundary.

### Object Validation

Bindings validate native objects before accessing them.

For `TechnoClass` objects, validation includes:

* `nullptr` checks
* RTTI/type validation through `WhatAmI()`
* object lifecycle validation
* health/liveness checks where applicable

Invalid or destroyed objects must not be dereferenced by Lua code.

### Graceful Failure

When a native object is no longer valid, API functions may return `nil` or another documented failure value instead of dereferencing the invalid pointer.

Lua scripts should still perform normal validity checks:

```lua
local unit = World.GetUnits()[1]

if unit and unit:IsAlive() then
    local health = unit:GetHealth()
end
```

Pointer validation is a native safety mechanism. It does not make stale Lua references permanently valid.

### Session Lifecycle

LuaAPI resets Lua callback state and related session data when the game scenario/session lifecycle requires it.

This prevents callback references and Lua state from surviving into an incompatible game session.

---

# 🎖️ Unit API

Unit and building objects are exposed through validated `TechnoClass` bindings.

## Basic Properties

### `unit:GetOwner()`

Returns the house that owns the object.

```lua
local owner = unit:GetOwner()
```

**Returns:** `HouseClass*` or `nil`.

---

### `unit:GetTypeName()`

Returns the object's INI type identifier.

```lua
local typeName = unit:GetTypeName()
```

**Returns:** `string`

Examples:

```text
HTNK
E1
DRED
APOC
```

---

### `unit:GetHealth()`

Returns the current health value.

```lua
local health = unit:GetHealth()
```

**Returns:** `number`

---

### `unit:GetMaxHealth()`

Returns the object's maximum health.

```lua
local maxHealth = unit:GetMaxHealth()
```

**Returns:** `number`

---

### `unit:IsAlive()`

Checks whether the object is currently valid and alive.

```lua
if unit:IsAlive() then
    -- Safe to continue working with the object
end
```

**Returns:** `boolean`

---

### `unit:GetPosition()`

Returns the object's map position.

```lua
local pos = unit:GetPosition()

print(pos.x, pos.y)
```

**Returns:**

```lua
{
    x = number,
    y = number
}
```

Coordinates are map cell coordinates.

---

### `unit:GetDistanceTo(otherUnit)`

Returns the Euclidean distance between two objects in cells.

```lua
local distance = unit:GetDistanceTo(otherUnit)
```

**Returns:** `number`

---

## Combat State

### `unit:IsAttacking()`

Checks whether the object is currently executing an Attack mission.

```lua
if unit:IsAttacking() then
    -- Unit is attacking
end
```

**Returns:** `boolean`

---

### `unit:TakeDamage(amount, warhead)`

Applies damage to the object.

```lua
local success = unit:TakeDamage(100, "SA")
```

**Parameters:**

* `amount` — damage amount
* `warhead` — optional warhead identifier

**Returns:** `boolean`

---

### `unit:Disable(frames)`

Temporarily disables the object.

```lua
local success = unit:Disable(90)
```

**Parameters:**

* `frames` — duration in logical frames

**Returns:** `boolean`

---

# 🔫 Sub-Turret API

LuaAPI provides native support for adding and controlling additional weapon turrets.

## `unit:AddSubTurret(section, offX, offY, offZ, rot, rof)`

Adds a sub-turret to a unit.

```lua
unit:AddSubTurret(1, 40, 0, 15, 12, 90)
```

**Parameters:**

| Parameter | Type     | Description                    |
| --------- | -------- | ------------------------------ |
| `section` | `number` | Voxel section index            |
| `offX`    | `number` | X offset in leptons            |
| `offY`    | `number` | Y offset in leptons            |
| `offZ`    | `number` | Z offset in leptons            |
| `rot`     | `number` | Rotation speed                 |
| `rof`     | `number` | Rate of fire in logical frames |

**Returns:** `boolean`

---

## `unit:SetSplitTargets(targets)`

Assigns targets to the unit's sub-turrets.

```lua
unit:SetSplitTargets(targets)
```

**Parameters:**

* `targets` — Lua table containing `TechnoClass` objects.

The implementation assigns the supplied targets to the available sub-turrets according to the current split-target logic.

**Returns:** `boolean`

---

## `unit:FireSplitSalvo()`

Fires the configured sub-turrets at their assigned targets.

```lua
unit:FireSplitSalvo()
```

**Returns:** `boolean`

---

# ✨ Particle Effects

## `unit:SetHealthRatio(ratio)`

Sets the object's health using a normalized ratio.

```lua
unit:SetHealthRatio(0.35)
```

Example:

```text
0.35 = 35% health
1.00 = 100% health
```

**Returns:** `boolean`

---

## `unit:AttachParticleSystem(sysName)`

Attaches a particle system to the object.

```lua
unit:AttachParticleSystem("DamageSmokeSys")
```

Examples include:

```text
DamageSmokeSys
DamageFireSys
```

**Returns:** `boolean`

---

# 💰 House API

`House` provides access to player and house objects.

## Static Functions

### `House.GetPlayer()`

Returns the local human player's house.

```lua
local player = House.GetPlayer()
```

**Returns:** `HouseClass*` or `nil`.

---

### `House.GetCount()`

Returns the number of available houses.

```lua
local count = House.GetCount()
```

**Returns:** `number`

---

### `House.GetByIndex(index)`

Returns a house by index.

```lua
local house = House.GetByIndex(index)
```

**Returns:** `HouseClass*` or `nil`.

---

## Instance Methods

### `house:GetName()`

Returns the house name.

```lua
local name = house:GetName()
```

**Returns:** `string`

---

### `house:IsHuman()`

Checks whether the house is controlled by a human player.

```lua
if house:IsHuman() then
    -- Human-controlled
end
```

**Returns:** `boolean`

---

### `house:IsAlliedWith(otherHouse)`

Checks alliance status between two houses.

```lua
if house:IsAlliedWith(otherHouse) then
    -- Allied
end
```

**Returns:** `boolean`

---

### `house:GetCredits()`

Returns the current credits balance.

```lua
local credits = house:GetCredits()
```

**Returns:** `number`

---

### `house:AddCredits(amount)`

Adds or subtracts credits.

```lua
house:AddCredits(1000)
house:AddCredits(-500)
```

**Parameters:**

* `amount` — positive to add, negative to subtract

**Returns:** `boolean`

---

### `house:GetPowerOutput()`

Returns total power production.

```lua
local power = house:GetPowerOutput()
```

**Returns:** `number`

---

### `house:GetPowerDrain()`

Returns total power consumption.

```lua
local drain = house:GetPowerDrain()
```

**Returns:** `number`

---

# 🧪 Development Tools

## `house:SpawnUnit(typeId, count, x, y, facing, force, action)`

Spawns units at the specified map position.

```lua
local spawned = player:SpawnUnit(
    "APOC",
    5,
    100,
    100,
    0,
    false,
    "hunt"
)
```

### Parameters

| Parameter | Description                                       |
| --------- | ------------------------------------------------- |
| `typeId`  | INI unit type identifier, e.g. `"APOC"`           |
| `count`   | Number of units to spawn                          |
| `x`       | X map cell                                        |
| `y`       | Y map cell                                        |
| `facing`  | Facing direction, `0–255`                         |
| `force`   | Whether to bypass normal spawn/pathfinding checks |
| `action`  | Optional action/mission identifier                |

### Spawn behavior

When `force=false`, the implementation attempts to find a valid nearby cell if the requested cell cannot be used.

The spawn search is bounded by the implementation's configured search radius.

If the requested coordinates are outside the valid map area, spawning fails for those units.

If an action such as `"hunt"` is supplied and supported, the spawned unit receives that action after creation.

**Returns:** `number` — number of successfully created units.

---

# 🗺️ World API

`World` provides global queries over objects currently present in the game world.

## Global Collections

### `World.GetBuildings()`

Returns buildings currently available to the API.

```lua
local buildings = World.GetBuildings()

for _, building in ipairs(buildings) do
    -- ...
end
```

**Returns:** `table`

---

### `World.GetUnits()`

Returns units currently available to the API.

```lua
local units = World.GetUnits()
```

**Returns:** `table`

---

### `World.GetAllUnits()`

Returns all supported unit objects, including infantry.

```lua
local units = World.GetAllUnits()
```

**Returns:** `table`

---

# 📍 Spatial Queries

## `World.GetUnitsInRadius(x, y, radius)`

Returns units located within the specified radius.

```lua
local units = World.GetUnitsInRadius(100, 100, 15)
```

**Parameters:**

* `x` — center X cell
* `y` — center Y cell
* `radius` — radius in cells

The query uses Euclidean distance and filters invalid/dead objects according to the native validation rules.

**Returns:** `table`

---

# 🗺️ Map Information

## `World.GetWaypoint(id)`

Returns the coordinates of a map waypoint.

```lua
local pos = World.GetWaypoint(5)

if pos then
    print(pos.x, pos.y)
end
```

**Parameters:**

* `id` — waypoint identifier

**Returns:**

```lua
{
    x = number,
    y = number
}
```

or `nil` if the waypoint does not exist.

---

# 💬 Engine API

## `Engine.PrintMessage(text, colorIndex)`

Displays a message through the standard in-game message system.

```lua
Engine.PrintMessage("Hello, Commander!", 1)
```

**Parameters:**

* `text` — message string
* `colorIndex` — message color index

**Returns:** `boolean`

---

# 🎮 Game API

## `Game.GetFrame()`

Returns the current logical game frame.

```lua
local frame = Game.GetFrame()
```

The value corresponds to the engine's logical frame counter.

**Returns:** `number`

---

## `Game.IsInMatch()`

Checks whether a match is currently active.

```lua
if Game.IsInMatch() then
    -- Match is active
end
```

**Returns:** `boolean`

---

# 📡 Event Bus & Callbacks

LuaAPI exposes two kinds of callbacks.

**Lifecycle callbacks** are methods on the mod table returned by `main.lua`. They are dispatched independently for each enabled mod.

**Global callbacks** are Lua global functions looked up by name. They are not methods of the mod table.

## Callback Contract

| Callback                          | Called by             | Lookup                        | Arguments                                        | Return value      | If undefined |
| --------------------------------- | --------------------- | ----------------------------- | ------------------------------------------------ | ----------------- | ------------ |
| `Update(frame)`                   | `init.lua`            | Mod-table method              | `frame: number`                                  | Ignored           | Skipped      |
| `OnScenarioStart()`               | `init.lua`            | Mod-table method              | None                                             | Ignored           | Skipped      |
| `OnPreDamage(...)`                | C++ event bridge      | Registered callback reference | `attacker, target, damage, dmgType, frame, subc` | `number` or `nil` | Skipped      |
| `OnUnitDestroyed(victim, killer)` | C++ event bridge      | Registered callback reference | `victim, killer`                                 | Ignored           | Skipped      |
| `OnDebugCommand(text)`            | C++ debug input layer | Global `lua_getglobal` lookup | `text: string`                                   | Ignored           | No-op        |

> **Implementation note:** `OnPreDamage` and `OnUnitDestroyed` are registered as Lua callback references by the native event system. Their dispatch path must remain consistent with the implementation.

---

## Lifecycle Callback Registration

A mod exposes lifecycle callbacks by returning a Lua table:

```lua
local MyMod = {}

function MyMod.Update(frame)
    -- Called once per logical frame.
end

function MyMod.OnScenarioStart()
    -- Scenario initialization.
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Damage interception.
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- Destruction event.
end

return MyMod
```

---

## `Update(frame)`

Called once per logical game frame.

**Arguments:**

* `frame` — `number`, current logical frame.

**Return value:** ignored.

Use this callback for continuous gameplay logic, polling, timers, and state management.

The callback is gated against the game's logical frame rather than the render frame.

---

## `OnScenarioStart()`

Called when the scenario starts.

**Arguments:** none.

**Return value:** ignored.

Use this callback for scenario initialization.

---

## `OnPreDamage(attacker, target, damage, dmgType, frame, subc)`

Called during the damage pipeline before incoming damage is applied.

### Arguments

| Argument   | Type                    | Description                    |
| ---------- | ----------------------- | ------------------------------ |
| `attacker` | `TechnoClass*` or `nil` | Attacking object, if available |
| `target`   | `TechnoClass*`          | Object receiving damage        |
| `damage`   | `number`                | Incoming damage                |
| `dmgType`  | `string`                | Damage/warhead type            |
| `frame`    | `number`                | Current logical frame          |
| `subc`     | `number`                | Sub-cell index                 |

### Return contract

* `number` — replaces the incoming damage.
* `0` — cancels the damage completely.
* `nil` — passes the original damage through unchanged.
* Negative damage values must not be returned.

Example:

```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    local player = House.GetPlayer()

    if not player or not target then
        return nil
    end

    if target:GetOwner() == player then
        return damage * 0.5
    end

    return nil
end
```

---

## `OnUnitDestroyed(victim, killer)`

Called when a unit or building is destroyed.

### Arguments

| Argument | Type                    | Description                                          |
| -------- | ----------------------- | ---------------------------------------------------- |
| `victim` | `TechnoClass*`          | Destroyed object                                     |
| `killer` | `TechnoClass*` or `nil` | Object responsible for the destruction, if available |

**Return value:** ignored.

Example:

```lua
function MyMod.OnUnitDestroyed(victim, killer)
    if not victim then
        return
    end

    local victimType = victim:GetTypeName()
    local killerType = killer and killer:GetTypeName() or "unknown"

    Engine.PrintMessage(
        victimType .. " destroyed by " .. killerType,
        2
    )
end
```

---

# 🐛 Global Debug Callback

## `OnDebugCommand(text)`

`OnDebugCommand` is a **global Lua function**, not a lifecycle callback on the mod table.

The debug input layer looks up this function through the Lua global environment.

Define it as:

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("[DEBUG] " .. text, 1)
end
```

Do not define it as:

```lua
local MyMod = {}

function MyMod.OnDebugCommand(text)
    -- This is not the global debug callback.
end

return MyMod
```

### Arguments

* `text` — `string`, command entered by the user.

### Return value

Ignored.

### Missing callback

If no global `OnDebugCommand` exists, the debug event is ignored.

Because this callback is global rather than mod-scoped, only one active definition should be used.

---

# 🌐 CnCNet Determinism & Multiplayer

LuaAPI gameplay callbacks execute as part of the game's logical simulation.

Lua scripts used in multiplayer must therefore avoid nondeterministic behavior.

## Critical Rules

Do not use wall-clock time for gameplay decisions:

```lua
-- Do not use in deterministic gameplay logic:
os.time()
os.clock()
```

Use the logical game frame instead:

```lua
function MyMod.Update(frame)
    if frame % 300 == 0 then
        -- Deterministic frame-based logic
    end
end
```

LuaAPI's frame-based callback mechanism does not automatically make arbitrary Lua code deterministic.

The mod itself must also avoid nondeterministic inputs and behavior.

## Randomness

Random behavior must be handled consistently across clients.

Do not base gameplay decisions on local wall-clock time or other client-specific values.

Prefer deterministic state derived from the logical game state and frame sequence.

For example:

```lua
function MyMod.Update(frame)
    if frame % 60 == 0 then
        -- Deterministic timing point
    end
end
```

---

## Logical Frame Gating

`Update(frame)` uses the game's logical frame counter rather than the render frame rate.

This means scripts should use the supplied `frame` argument for frame-based timing.

Do not implement render-rate timers using wall-clock time.

---

## Multiplayer Requirements

When testing LuaAPI through CnCNet:

1. All players must use the same LuaAPI binary/version.
2. All players must use identical gameplay scripts.
3. All players must use the same `scripts/active_mods.txt`.
4. All players must use the same mod configuration.
5. Monitor the match for Out-of-Sync (OOS) errors.

---

# 🔌 CnCNet Integration

LuaAPI can be attached to a CnCNet-launched Yuri's Revenge process.

## Attach Mode

Recommended workflow:

1. Place `LuaAPI.dll`, `injector.exe`, and `scripts/` in the game directory.
2. Launch the game through the CnCNet client.
3. CnCNet starts the game process.
4. Run:

```text
injector.exe --attach
```

5. The injector waits for the target game process and performs the required attachment/injection sequence.

The exact process behavior depends on the current injector implementation.

---

## Headless Mode

For automated testing or integration:

```text
injector.exe --withcncnet
```

The injector operates without an interactive console and waits for the CnCNet game process.

---

# 🔌 Mod Loader

Mods are enabled through:

```text
scripts/active_mods.txt
```

Example:

```text
# scripts/active_mods.txt
multi_turret_battleship
shield_overload
my_custom_mod
```

One mod name is specified per line.

The name corresponds to the mod directory under:

```text
scripts/mods/
```

Comments begin with `#`.

Example:

```text
scripts/
└── mods/
    ├── multi_turret_battleship/
    ├── shield_overload/
    └── my_custom_mod/
```

---

# 📝 Complete Example: Multi-Turret Battleship

```lua
-- scripts/mods/multi_turret_battleship/main.lua

local MultiTurretMod = {}

function MultiTurretMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() and unit:GetOwner() == player then
            if unit:GetTypeName() == "DRED" then
                unit:AddSubTurret(1, 40, 0, 15, 12, 90)
                unit:AddSubTurret(2, -40, 0, 15, 12, 90)
                unit:AddSubTurret(3, 0, 40, 15, 12, 90)
            end
        end
    end
end

function MultiTurretMod.Update(frame)
    local player = House.GetPlayer()

    if not player then
        return
    end

    if frame % 30 ~= 0 then
        return
    end

    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() and unit:GetOwner() == player then
            if unit:GetTypeName() == "DRED" then
                local pos = unit:GetPosition()

                local nearbyUnits =
                    World.GetUnitsInRadius(pos.x, pos.y, 15)

                local targets = {}

                for _, enemy in ipairs(nearbyUnits) do
                    if enemy:IsAlive()
                        and enemy:GetOwner() ~= player then

                        table.insert(targets, enemy)
                    end
                end

                if #targets > 0 then
                    unit:SetSplitTargets(targets)
                    unit:FireSplitSalvo()
                end
            end
        end
    end
end

return MultiTurretMod
```

---

# 🚀 Development Example: Debug Console

Because `OnDebugCommand` is a global callback, a debug console implementation must define it globally:

```lua
-- scripts/mods/debug_console/main.lua

function OnDebugCommand(text)
    local count, typeId =
        text:match("^(%d+)%s+(%u+)$")

    if not count or not typeId then
        Engine.PrintMessage(
            "[DEBUG] Invalid command: " .. text,
            2
        )
        return
    end

    count = tonumber(count)

    local player = House.GetPlayer()

    if not player then
        Engine.PrintMessage(
            "[DEBUG] No player house",
            2
        )
        return
    end

    local baseX, baseY = nil, nil

    for _, building in ipairs(World.GetBuildings()) do
        if building:IsAlive()
            and building:GetOwner() == player then

            local pos = building:GetPosition()

            baseX = math.floor(pos.x)
            baseY = math.floor(pos.y)

            break
        end
    end

    if not baseX or not baseY then
        Engine.PrintMessage(
            "[DEBUG] No player building",
            2
        )
        return
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

## Unit Methods

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

unit:AddSubTurret(section, offX, offY, offZ, rot, rof)
unit:SetSplitTargets(targets)
unit:FireSplitSalvo()

unit:SetHealthRatio(ratio)
unit:AttachParticleSystem(sysName)
```

## House Methods

```lua
house:GetName()
house:IsHuman()
house:IsAlliedWith(otherHouse)
house:GetCredits()
house:AddCredits(amount)
house:GetPowerOutput()
house:GetPowerDrain()

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

## Lifecycle Callbacks

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
end

function MyMod.OnUnitDestroyed(
    victim,
    killer
)
end
```

## Global Callback

```lua
function OnDebugCommand(text)
end
```

---

# ⚠️ Common Pitfalls

## 1. Holding Invalid Object References

Native game objects can be destroyed between frames.

Always validate an object before using it:

```lua
if unit and unit:IsAlive() then
    local health = unit:GetHealth()
end
```

---

## 2. Treating `OnDebugCommand` as a Mod Method

Incorrect:

```lua
function MyMod.OnDebugCommand(text)
end
```

Correct:

```lua
function OnDebugCommand(text)
end
```

`OnDebugCommand` is a global callback.

---

## 3. Forgetting the `OnPreDamage` Return Value

If a mod implements `OnPreDamage`, explicitly return the intended result:

```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end
```

`nil` means that the original damage passes through unchanged.

---

## 4. Using Wall-Clock Time

Avoid:

```lua
os.time()
os.clock()
```

for deterministic gameplay logic.

Use the logical frame:

```lua
if frame % 300 == 0 then
    -- Logic
end
```

---

## 5. Assuming LuaAPI Guarantees Determinism

LuaAPI provides logical-frame-based callbacks, but arbitrary Lua code can still introduce nondeterminism.

Multiplayer-safe behavior remains the responsibility of the mod author.

---

# 🔗 Related Documentation

* [TUTORIAL.md](TUTORIAL.md) — Step-by-step guide for creating a LuaAPI mod
* [CAPABILITIES_AND_COOKBOOK.md](CAPABILITIES_AND_COOKBOOK.md) — Proven mechanics and implementation recipes
* [ENGINEERING_LESSONS.md](ENGINEERING_LESSONS.md) — Technical deep-dives, limitations, and engineering lessons
* [MOD_MANAGER.md](MOD_MANAGER.md) — Mod structure, loading, and distribution
* [ROADMAP.md](ROADMAP.md) — Project milestones and development status
