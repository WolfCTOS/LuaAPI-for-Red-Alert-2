# LuaAPI for Red Alert 2: API Reference Manual

> **Version:** 1.1.0 (Milestone 11)
> **Target:** `gamemd.exe` — Yuri's Revenge 1.001
> **Last Updated:** 2026-08-31
> **Safety:** Protected by RTTI (`WhatAmI()`) validation and SEH against `0xC0000005`.

---

## API Architecture

LuaAPI uses a namespace-based architecture for clean separation of concerns:

* **`House`** — Player and house management
* **`World`** — Global queries (units, buildings, spatial)
* **`Engine`** — Game engine functions (messages, system)
* **`Game`** — Game state and frame information
* **`AI`** — AI control functions (planned)

All exposed engine objects are validated before use, with native exception protection applied where supported.

---

## Security Architecture & Pointer Safety

In bare C++ modding, referencing a destroyed or invalid game object can cause `0xC0000005` (Access Violation) crashes.

LuaAPI uses defensive validation at the C++/Lua boundary.

### `ValidateTechno(TechnoClass* ptr)`

Techno object bindings validate:

* `nullptr`
* Engine RTTI type through `WhatAmI()`
* Object lifecycle state
* Health/liveness where applicable

Invalid objects are rejected before unsafe access.

### Graceful Failure

When a script references an invalid or destroyed object, the binding fails safely instead of dereferencing the pointer.

Depending on the operation, the binding returns `nil` or `false` and may write diagnostic information to `LuaAPI.log`.

### Session Lifecycle

LuaAPI resets Lua callback references and VM/session state when a scenario is loaded, restarted, or exited.

This prevents callbacks and Lua state from surviving across incompatible game sessions.

---

# Unit API

The Unit API operates on `TechnoClass` objects, including units and buildings exposed by the engine.

## Basic Properties

### `unit:GetOwner()`

```lua
local house = unit:GetOwner()
```

**Returns:** `HouseClass*` or `nil`

Returns the house that owns the unit.

---

### `unit:GetTypeName()`

```lua
local typeName = unit:GetTypeName()
```

**Returns:** `string` or `nil`

Returns the unit's INI type identifier.

Examples:

```text
HTNK
E1
DRED
APOC
```

---

### `unit:GetHealth()`

```lua
local hp = unit:GetHealth()
```

**Returns:** `number`

Returns the unit's current health.

---

### `unit:GetMaxHealth()`

```lua
local maxHp = unit:GetMaxHealth()
```

**Returns:** `number`

Returns the unit's maximum health.

---

### `unit:IsAlive()`

```lua
if unit:IsAlive() then
    -- Unit is alive and valid
end
```

**Returns:** `boolean`

Checks whether the unit is currently valid and alive.

---

### `unit:GetPosition()`

```lua
local pos = unit:GetPosition()

print(pos.x, pos.y)
```

**Returns:** `{ x = number, y = number }` or `nil`

Returns the unit's map-cell coordinates.

---

### `unit:GetDistanceTo(otherUnit)`

```lua
local distance = unit:GetDistanceTo(otherUnit)
```

**Returns:** `number` or `nil`

Returns the Euclidean distance between two units in map cells.

---

# Combat API

### `unit:IsAttacking()`

```lua
if unit:IsAttacking() then
    -- Unit is currently attacking
end
```

**Returns:** `boolean`

Returns `true` when the unit is currently operating under an Attack mission.

---

### `unit:TakeDamage(amount, warhead)`

```lua
unit:TakeDamage(100, "AP")
```

**Returns:** `boolean`

Applies damage to the unit.

`warhead` specifies the warhead used by the damage operation.

---

### `unit:Disable(frames)`

```lua
unit:Disable(90)
```

**Returns:** `boolean`

Temporarily disables the unit for the specified number of logical game frames.

---

# Sub-Turret API

LuaAPI provides a sub-turret system for attaching additional firing points and target assignments to existing `TechnoClass` objects.

## `unit:AddSubTurret(section, offX, offY, offZ, rot, rof)`

```lua
unit:AddSubTurret(section, offX, offY, offZ, rot, rof)
```

**Returns:** `boolean`

Adds a sub-turret to the unit.

### Parameters

| Parameter | Type     | Description                    |
| --------- | -------- | ------------------------------ |
| `section` | `number` | Voxel section index (`0–7`)    |
| `offX`    | `number` | X offset from the unit center  |
| `offY`    | `number` | Y offset from the unit center  |
| `offZ`    | `number` | Z offset from the unit center  |
| `rot`     | `number` | Rotation speed (`0–255`)       |
| `rof`     | `number` | Rate of fire in logical frames |

Offsets are specified in engine leptons.

Example:

```lua
unit:AddSubTurret(1, 40, 0, 15, 12, 90)
```

---

## `unit:SetSplitTargets(targets)`

```lua
unit:SetSplitTargets(targets)
```

**Returns:** `boolean`

Assigns targets to the unit's sub-turrets from a Lua table containing valid `TechnoClass` objects.

The targeting system uses the unit's native target for turret `0` when available. Remaining sub-turrets can receive independently selected targets.

---

## `unit:FireSplitSalvo()`

```lua
unit:FireSplitSalvo()
```

**Returns:** `boolean`

Fires the configured sub-turrets at their assigned targets.

The split-salvo firing path creates tracer projectiles and applies the configured projectile/warhead behavior.

---

# Health and Particle Effects

### `unit:SetHealthRatio(ratio)`

```lua
unit:SetHealthRatio(0.35)
```

**Returns:** `boolean`

Sets the unit's health as a ratio of its maximum health.

For example:

```lua
unit:SetHealthRatio(0.35)
```

sets the unit to approximately 35% of its maximum health.

---

### `unit:AttachParticleSystem(sysName)`

```lua
unit:AttachParticleSystem("DamageSmokeSys")
```

**Returns:** `boolean`

Attaches a particle system to the unit.

Examples:

```text
DamageSmokeSys
DamageFireSys
```

---

# House API

The `House` namespace provides access to player and house-level information.

## Static Functions

### `House.GetPlayer()`

```lua
local player = House.GetPlayer()
```

**Returns:** `HouseClass*` or `nil`

Returns the local human player's house.

---

### `House.GetCount()`

```lua
local count = House.GetCount()
```

**Returns:** `number`

Returns the number of houses currently available to the game engine.

---

### `House.GetByIndex(index)`

```lua
local house = House.GetByIndex(index)
```

**Returns:** `HouseClass*` or `nil`

Returns the house at the specified engine index.

---

## Instance Methods

### `house:GetName()`

```lua
local name = house:GetName()
```

**Returns:** `string`

Returns the house name.

Examples:

```text
Allies
Soviets
```

---

### `house:IsHuman()`

```lua
if house:IsHuman() then
    -- Human-controlled house
end
```

**Returns:** `boolean`

Checks whether the house is human-controlled.

---

### `house:IsAlliedWith(otherHouse)`

```lua
if house:IsAlliedWith(otherHouse) then
    -- Houses are allied
end
```

**Returns:** `boolean`

Checks the alliance relationship between two houses.

---

### `house:GetCredits()`

```lua
local credits = house:GetCredits()
```

**Returns:** `number`

Returns the current credit balance.

---

### `house:AddCredits(amount)`

```lua
house:AddCredits(1000)
```

**Returns:** `boolean`

Adds or subtracts credits from the house.

Negative values can be used to subtract credits.

---

### `house:GetPowerOutput()`

```lua
local output = house:GetPowerOutput()
```

**Returns:** `number`

Returns the house's total power production.

---

### `house:GetPowerDrain()`

```lua
local drain = house:GetPowerDrain()
```

**Returns:** `number`

Returns the house's total power consumption.

---

# Development Tools

## `house:SpawnUnit(typeId, count, x, y, facing, force, action)`

```lua
local spawned = house:SpawnUnit(
    typeId,
    count,
    x,
    y,
    facing,
    force,
    action
)
```

**Returns:** `number`

Spawns one or more units for the specified house.

### Parameters

| Parameter | Type      | Description                                         |
| --------- | --------- | --------------------------------------------------- |
| `typeId`  | `string`  | Unit type identifier, e.g. `"APOC"`                 |
| `count`   | `number`  | Number of units to spawn                            |
| `x`       | `number`  | X map-cell coordinate                               |
| `y`       | `number`  | Y map-cell coordinate                               |
| `facing`  | `number`  | Facing direction (`0–255`)                          |
| `force`   | `boolean` | If `true`, bypasses normal spawn/pathfinding checks |
| `action`  | `string`  | Optional action, e.g. `"hunt"`                      |

Example:

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

### Spawn Behavior

When `force` is `false`:

* The requested cell is checked for occupancy.
* If occupied, the spawn system searches nearby cells using a spiral pattern.
* The current search radius is 3 cells.

If the requested coordinates are outside the supported map bounds, the spawn request is rejected.

If:

```lua
action == "hunt"
```

the spawned units receive a Hunt mission.

The function returns the number of units successfully created.

---

# World API

## Global Collections

### `World.GetBuildings()`

```lua
local buildings = World.GetBuildings()
```

**Returns:** `table`

Returns an array of building objects currently available to the script.

---

### `World.GetUnits()`

```lua
local units = World.GetUnits()
```

**Returns:** `table`

Returns an array of unit objects.

---

### `World.GetAllUnits()`

```lua
local units = World.GetAllUnits()
```

**Returns:** `table`

Returns the complete unit collection, including infantry.

---

# Spatial Queries

## `World.GetUnitsInRadius(x, y, radius)`

```lua
local units = World.GetUnitsInRadius(x, y, 15)
```

**Returns:** `table`

Returns valid units within the specified radius.

Parameters:

* `x` — X map-cell coordinate.
* `y` — Y map-cell coordinate.
* `radius` — Radius in map cells.

Distance is calculated using Euclidean distance.

Invalid or dead objects are filtered from the result.

---

# Map Information

## `World.GetWaypoint(id)`

```lua
local position = World.GetWaypoint(0)

if position then
    print(position.x, position.y)
end
```

**Returns:** `{ x = number, y = number }` or `nil`

Returns the map coordinates associated with a FinalAlert2 waypoint.

Supported waypoint IDs are `0–99`.

Returns `nil` if the requested waypoint does not exist.

---

# Engine API

## Messages and HUD

### `Engine.PrintMessage(text, colorIndex)`

```lua
Engine.PrintMessage("Hello, commander!", 0)
```

**Returns:** `boolean`

Prints a message through the standard Red Alert 2 in-game message system.

### Color Indices

| Index | Color  |
| ----: | ------ |
|   `0` | White  |
|   `1` | Yellow |
|   `2` | Red    |
|   `3` | Green  |
|   `4` | Blue   |

---

# Game API

### `Game.GetFrame()`

```lua
local frame = Game.GetFrame()
```

**Returns:** `number`

Returns the current logical game frame from `Unsorted::CurrentFrame`.

---

### `Game.IsInMatch()`

```lua
if Game.IsInMatch() then
    -- Gameplay is active
end
```

**Returns:** `boolean`

Returns whether an active match is currently running.

---

# Event Bus and Callbacks

LuaAPI exposes script callbacks for selected engine events.

Callbacks are implemented by returning a table from a mod's `main.lua`.

## Basic Mod Structure

```lua
local MyMod = {}

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Your logic here
    return damage
end

function MyMod.OnScenarioStart()
    -- Initialization logic
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- Death event logic
end

function MyMod.OnDebugCommand(text)
    -- Debug console command
end

function MyMod.Update(frame)
    -- Per-frame logic
end

return MyMod
```

---

# Supported Events

## `OnPreDamage(attacker, target, damage, dmgType, frame, subc)`

Intercepts damage before it is applied.

### Arguments

| Argument   | Type                   | Description                    |
| ---------- | ---------------------- | ------------------------------ |
| `attacker` | `TechnoClass*` / `nil` | Attacking object, if available |
| `target`   | `TechnoClass*`         | Object receiving damage        |
| `damage`   | `number`               | Incoming damage amount         |
| `dmgType`  | `string`               | Warhead/damage type            |
| `frame`    | `number`               | Current logical frame          |
| `subc`     | `number`               | Sub-cell index                 |

### Return Values

Return a modified damage value:

```lua
return damage * 0.5
```

Return `0` to cancel the incoming damage:

```lua
return 0
```

Return `nil` to leave the original damage unchanged:

```lua
return nil
```

---

## `OnScenarioStart()`

Called when a new scenario or match session starts.

### Arguments

None.

### Use Cases

* Initialize mod state.
* Configure units.
* Apply initial effects.
* Initialize custom gameplay systems.

---

## `OnUnitDestroyed(victim, killer)`

Called when a unit or building is destroyed.

### Arguments

| Argument | Type                   | Description                                          |
| -------- | ---------------------- | ---------------------------------------------------- |
| `victim` | `TechnoClass*`         | Destroyed object                                     |
| `killer` | `TechnoClass*` / `nil` | Object responsible for the destruction, if available |

### Use Cases

* Bounty systems.
* Death-triggered effects.
* Mission triggers.
* Statistics.

---

## `OnDebugCommand(text)`

Called when the debug input system submits a command.

### Arguments

```text
text: string
```

Contains the command entered by the user.

### Use Cases

* Development tools.
* Rapid testing.
* Debug spawning.
* Runtime diagnostics.

---

## `Update(frame)`

Called once per logical game frame while gameplay is active.

### Arguments

```text
frame: number
```

The current logical frame from `Unsorted::CurrentFrame`.

### Use Cases

* Continuous gameplay logic.
* Polling.
* State management.
* Custom AI systems.

LuaAPI gates this callback against the logical game frame rather than the render frame.

---

# CnCNet Determinism and Multiplayer

Lua gameplay code must remain deterministic across all clients.

## Critical Rules

Do not use:

```lua
os.time()
os.clock()
```

for gameplay decisions.

These values can differ between clients and may contribute to Out-of-Sync (OOS) conditions.

When deterministic randomness is required, derive the random sequence from synchronized game state.

Example:

```lua
math.randomseed(frame + 12345)

local roll = math.random(1, 100)
```

The important requirement is that all clients execute the same logic with the same inputs.

---

## Logical Frame Gating

LuaAPI dispatches `Update()` according to the logical game frame rather than the render frame.

This prevents differences in rendering FPS from causing additional `Update()` executions.

Your `Update()` callback is therefore driven by synchronized logical game time.

Scripts should base gameplay timing on the supplied `frame` value.

---

# Multiplayer Testing

To test LuaAPI in CnCNet multiplayer:

1. Both players must use identical `LuaAPI.dll` binaries.
2. Both players must use identical `scripts/` contents.
3. Both players must enable the same mods in `scripts/active_mods.txt`.
4. Launch the game through the CnCNet client.
5. Attach LuaAPI to the appropriate game process.
6. Verify that the match completes without OOS errors.

---

# CnCNet Integration

## Attach Mode

Attach mode is recommended when launching through CnCNet.

Place the following in the game directory:

```text
LuaAPI.dll
injector.exe
scripts/
```

Launch the game through the CnCNet client and then run:

```text
injector.exe --attach
```

The injector polls for the appropriate game process, waits for the required Ares/Phobos environment, and injects LuaAPI when the target is ready.

---

## Headless Mode

For automated testing or external modpack integration:

```text
injector.exe --withcncnet
```

The injector runs headlessly, waits for the CnCNet game process, attaches LuaAPI, and exits when the game closes.

---

# ModLoader Configuration

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

The name must correspond to a directory under:

```text
scripts/mods/
```

Lines beginning with `#` are treated as comments.

---

# Complete Example: Multi-Turret Battleship

```lua
-- scripts/mods/multi_turret_battleship/main.lua

local MultiTurretMod = {}

function MultiTurretMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    -- Equip all Dreadnoughts with three sub-turrets.
    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() and unit:GetOwner() == player then
            if unit:GetTypeName() == "DRED" then

                -- Fore
                unit:AddSubTurret(
                    1,
                    40, 0, 15,
                    12,
                    90
                )

                -- Aft
                unit:AddSubTurret(
                    2,
                    -40, 0, 15,
                    12,
                    90
                )

                -- Port
                unit:AddSubTurret(
                    3,
                    0, 40, 15,
                    12,
                    90
                )
            end
        end
    end
end

function MultiTurretMod.Update(frame)
    local player = House.GetPlayer()

    if not player then
        return
    end

    -- Assign targets and fire every 30 logical frames.
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

# Development Example: Debug Console

```lua
-- scripts/mods/debug_console/main.lua

local DebugConsole = {}

function DebugConsole.OnDebugCommand(text)
    -- Parse commands such as:
    -- 5 APOC
    -- 3 LTNK

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

    -- Find the player's first building.
    local baseX, baseY = 0, 0

    for _, building in ipairs(World.GetBuildings()) do
        if building:IsAlive()
            and building:GetOwner() == player then

            local position = building:GetPosition()

            baseX = math.floor(position.x)
            baseY = math.floor(position.y)

            break
        end
    end

    if baseX == 0 and baseY == 0 then
        Engine.PrintMessage(
            "[DEBUG] No player building",
            2
        )
        return
    end

    -- Spawn with Hunt mission.
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
            .. typeId
            .. " with hunt",
        1
    )
end

return DebugConsole
```

---

# Related Documentation

* [`PROJECT/ROADMAP.md`](../PROJECT/ROADMAP.md) — Project lifecycle and milestone status.
* [`PROJECT/CAPABILITIES.md`](../PROJECT/CAPABILITIES.md) — Current capabilities and supported mechanics.
* [`docs/ENGINEERING_LESSONS.md`](ENGINEERING_LESSONS.md) — Technical investigations, findings, and engine-specific pitfalls.
* [`docs/MOD_MANAGER.md`](MOD_MANAGER.md) — Mod loading, configuration, and distribution.
* [`docs/TUTORIAL.md`](TUTORIAL.md) — Step-by-step guide for LuaAPI mod development.

---

## Implementation Status

This document describes the currently exposed LuaAPI surface.

Features marked as planned or deferred in the project roadmap should not be considered part of the stable API until their corresponding milestone or gate has been completed and verified.
