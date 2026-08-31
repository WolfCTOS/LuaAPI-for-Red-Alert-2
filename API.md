# LuaAPI for Red Alert 2 — API Reference

> **Version:** 1.1.0
> **Milestone:** 11
> **Target:** `gamemd.exe` — Yuri's Revenge 1.001
> **Last Updated:** 2026-08-31

This document is the authoritative reference for the public LuaAPI exposed to Lua mods.

If another project document or example conflicts with this document, this document takes precedence. If this document conflicts with the implementation, the implementation is the source of truth and this document must be updated.

---

## 1. API Architecture

LuaAPI exposes functionality through namespaces and object methods.

### Namespaces

| Namespace | Purpose                                  |
| --------- | ---------------------------------------- |
| `House`   | Player and house management              |
| `World`   | Global unit, building, and map queries   |
| `Engine`  | Game-engine and HUD functions            |
| `Game`    | Game state and logical-frame information |
| `AI`      | Reserved for planned AI functionality    |

Lua objects returned by the API represent native Red Alert 2 engine objects. They should be treated as engine-backed handles rather than ordinary Lua tables.

---

## 2. Object Safety and Lifetime

LuaAPI performs defensive validation at the native/Lua boundary to reduce crashes caused by invalid engine pointers.

For `TechnoClass` objects, validation may include:

* `nullptr` checks;
* RTTI/type validation through `WhatAmI()`;
* object lifecycle checks;
* health/liveness checks where required by the binding;
* SEH protection around native operations where applicable.

If an operation cannot safely be performed, the binding may return `nil` or `false` depending on the function's contract.

### Important

`IsAlive()` should be used before operating on an object obtained from a collection or retained across frames:

```lua
local units = World.GetUnits()

for _, unit in ipairs(units) do
    if unit and unit:IsAlive() then
        local health = unit:GetHealth()
    end
end
```

LuaAPI's native validation is a safety mechanism. It is not a guarantee that every stale Lua reference is semantically valid for every operation.

---

# 3. TechnoClass API

Unit and building objects expose the following methods.

## Basic Properties

### `unit:GetOwner()`

Returns the house that owns the object.

```lua
local owner = unit:GetOwner()
```

**Returns:** `HouseClass*` or `nil` when unavailable.

---

### `unit:GetTypeName()`

Returns the object's INI type identifier.

```lua
local typeName = unit:GetTypeName()
-- "HTNK", "E1", "DRED", etc.
```

**Returns:** `string` or `nil`.

---

### `unit:GetHealth()`

Returns current health.

```lua
local health = unit:GetHealth()
```

**Returns:** `number`.

---

### `unit:GetMaxHealth()`

Returns maximum health.

```lua
local maxHealth = unit:GetMaxHealth()
```

**Returns:** `number`.

---

### `unit:IsAlive()`

Checks whether the object is currently considered valid/alive by the binding.

```lua
if unit:IsAlive() then
    -- safe to continue
end
```

**Returns:** `boolean`.

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

or `nil` when the position cannot be obtained.

Coordinates are map-cell coordinates.

---

### `unit:GetDistanceTo(otherUnit)`

Returns the Euclidean distance between two techno objects.

```lua
local distance = unit:GetDistanceTo(otherUnit)
```

**Returns:** `number` or `nil`.

---

## Combat State

### `unit:IsAttacking()`

Checks whether the unit is currently in an attack state.

```lua
if unit:IsAttacking() then
    -- unit is attacking
end
```

**Returns:** `boolean`.

---

### `unit:TakeDamage(amount, warhead)`

Applies damage to the object.

```lua
unit:TakeDamage(50, "HE")
```

**Parameters:**

| Parameter | Type                | Description        |
| --------- | ------------------- | ------------------ |
| `amount`  | `number`            | Damage amount      |
| `warhead` | `string` / optional | Warhead identifier |

**Returns:** `boolean`.

---

### `unit:Disable(frames)`

Temporarily disables the unit.

```lua
unit:Disable(90)
```

**Parameters:**

* `frames` — duration in logical frames.

**Returns:** `boolean`.

---

## Sub-Turret API

### `unit:AddSubTurret(section, offX, offY, offZ, rot, rof)`

Adds a sub-turret to a unit.

```lua
unit:AddSubTurret(1, 40, 0, 15, 12, 90)
```

**Parameters:**

| Parameter | Type     | Description                    |
| --------- | -------- | ------------------------------ |
| `section` | `number` | Voxel section index            |
| `offX`    | `number` | X offset                       |
| `offY`    | `number` | Y offset                       |
| `offZ`    | `number` | Z offset                       |
| `rot`     | `number` | Rotation speed                 |
| `rof`     | `number` | Rate of fire in logical frames |

Offsets are specified in engine units/leptons.

**Returns:** `boolean`.

---

### `unit:SetSplitTargets(targets)`

Assigns targets to the unit's sub-turrets.

```lua
unit:SetSplitTargets(targets)
```

**Parameters:**

* `targets` — Lua table containing `TechnoClass` objects.

**Returns:** `boolean`.

The exact target-to-turret assignment behavior is implementation-defined and should not be assumed beyond the behavior demonstrated by the tested implementation.

---

### `unit:FireSplitSalvo()`

Fires the configured sub-turrets at their assigned targets.

```lua
unit:FireSplitSalvo()
```

**Returns:** `boolean`.

---

## Health and Particle Effects

### `unit:SetHealthRatio(ratio)`

Sets the object's health using a normalized ratio.

```lua
unit:SetHealthRatio(0.35)
```

`0.35` represents 35% of maximum health.

**Returns:** `boolean`.

---

### `unit:AttachParticleSystem(sysName)`

Attaches a particle system to the object.

```lua
unit:AttachParticleSystem("DamageSmokeSys")
```

**Parameters:**

* `sysName` — particle system identifier.

**Returns:** `boolean`.

---

# 4. House API

The `House` namespace provides static house queries. Returned `HouseClass` objects expose instance methods.

## Static Functions

### `House.GetPlayer()`

Returns the local human player's house.

```lua
local player = House.GetPlayer()
```

**Returns:** `HouseClass*` or `nil`.

---

### `House.GetCount()`

Returns the number of houses available through the engine's house collection.

```lua
local count = House.GetCount()
```

**Returns:** `number`.

---

### `House.GetByIndex(index)`

Returns a house by index.

```lua
local house = House.GetByIndex(0)
```

**Returns:** `HouseClass*` or `nil`.

---

## Instance Methods

### `house:GetName()`

Returns the house name.

```lua
local name = house:GetName()
```

**Returns:** `string` or `nil`.

---

### `house:IsHuman()`

Checks whether the house is human-controlled.

```lua
if house:IsHuman() then
    -- human player
end
```

**Returns:** `boolean`.

---

### `house:IsAlliedWith(otherHouse)`

Checks the alliance relationship between two houses.

```lua
if house:IsAlliedWith(otherHouse) then
    -- allied
end
```

**Returns:** `boolean`.

---

### `house:GetCredits()`

Returns the current credits balance.

```lua
local credits = house:GetCredits()
```

**Returns:** `number`.

---

### `house:AddCredits(amount)`

Adds or subtracts credits.

```lua
house:AddCredits(500)
house:AddCredits(-100)
```

**Parameters:**

* `amount` — positive to add credits, negative to subtract credits.

**Returns:** `boolean`.

---

### `house:GetPowerOutput()`

Returns total power production.

```lua
local power = house:GetPowerOutput()
```

**Returns:** `number`.

---

### `house:GetPowerDrain()`

Returns total power consumption.

```lua
local drain = house:GetPowerDrain()
```

**Returns:** `number`.

---

## Development Tools

### `house:SpawnUnit(typeId, count, x, y, facing, force, action)`

Spawns units for development and gameplay scripting.

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

**Parameters:**

| Parameter | Type                | Description                               |
| --------- | ------------------- | ----------------------------------------- |
| `typeId`  | `string`            | INI type identifier                       |
| `count`   | `number`            | Number of units to spawn                  |
| `x`       | `number`            | X cell coordinate                         |
| `y`       | `number`            | Y cell coordinate                         |
| `facing`  | `number`            | Facing direction                          |
| `force`   | `boolean`           | Whether to bypass normal spawn validation |
| `action`  | `string` / optional | Optional initial action                   |

**Returns:** `number` — number of successfully created units.

When normal placement is used, the implementation may search for a valid nearby cell when the requested position is unavailable.

---

# 5. World API

`World` provides global queries over objects currently present in the game world.

## `World.GetBuildings()`

Returns buildings currently available to the binding.

```lua
local buildings = World.GetBuildings()

for _, building in ipairs(buildings) do
    if building:IsAlive() then
        -- ...
    end
end
```

**Returns:** Lua table containing techno objects.

---

## `World.GetUnits()`

Returns units currently available to the binding.

```lua
local units = World.GetUnits()
```

**Returns:** Lua table.

---

## `World.GetAllUnits()`

Returns the broader unit collection exposed by the binding, including infantry where applicable.

```lua
local units = World.GetAllUnits()
```

**Returns:** Lua table.

---

## `World.GetUnitsInRadius(x, y, radius)`

Returns units within a specified radius.

```lua
local units = World.GetUnitsInRadius(100, 100, 15)
```

**Parameters:**

| Parameter | Type     | Description            |
| --------- | -------- | ---------------------- |
| `x`       | `number` | Center X cell          |
| `y`       | `number` | Center Y cell          |
| `radius`  | `number` | Search radius in cells |

The spatial test uses Euclidean distance.

Invalid/dead objects are filtered according to the binding's validation rules.

**Returns:** Lua table.

---

## `World.GetWaypoint(id)`

Returns the map position associated with a waypoint.

```lua
local pos = World.GetWaypoint(0)

if pos then
    print(pos.x, pos.y)
end
```

**Parameters:**

* `id` — waypoint identifier.

**Returns:**

```lua
{
    x = number,
    y = number
}
```

or `nil`.

---

# 6. Engine API

## `Engine.PrintMessage(text, colorIndex)`

Displays a message through the game's message/HUD system.

```lua
Engine.PrintMessage("LuaAPI loaded!", 1)
```

**Parameters:**

| Parameter    | Type     | Description         |
| ------------ | -------- | ------------------- |
| `text`       | `string` | Message text        |
| `colorIndex` | `number` | Message color/index |

**Returns:** `boolean`.

The exact visual result depends on the underlying game message system.

---

# 7. Game API

## `Game.GetFrame()`

Returns the current logical game frame.

```lua
local frame = Game.GetFrame()
```

The value corresponds to the engine's logical frame counter.

**Returns:** `number`.

---

## `Game.IsInMatch()`

Checks whether the game is currently in an active match.

```lua
if Game.IsInMatch() then
    -- gameplay state
end
```

**Returns:** `boolean`.

---

# 8. Event Bus and Callbacks

LuaAPI exposes two callback mechanisms:

1. **Mod-table callbacks** — functions defined on the table returned by `main.lua`.
2. **Global callbacks** — Lua global functions looked up by name.

These mechanisms must not be conflated.

---

## Lifecycle Callbacks

Lifecycle callbacks are methods of the mod table returned by `main.lua`.

```lua
local MyMod = {}

function MyMod.Update(frame)
    -- ...
end

function MyMod.OnScenarioStart()
    -- ...
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- ...
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- ...
end

return MyMod
```

### `Update(frame)`

Called once per logical frame when dispatched by the LuaAPI loader.

**Arguments:**

* `frame` — current logical frame.

**Return value:** ignored.

If the callback is not defined, the mod is skipped for this callback.

---

### `OnScenarioStart()`

Called during scenario initialization.

**Arguments:** none.

**Return value:** ignored.

If the callback is not defined, the mod is skipped.

---

### `OnPreDamage(attacker, target, damage, dmgType, frame, subc)`

Allows a mod to intercept incoming damage.

**Arguments:**

| Argument   | Type                   | Description               |
| ---------- | ---------------------- | ------------------------- |
| `attacker` | `TechnoClass*` / `nil` | Attacking object          |
| `target`   | `TechnoClass*`         | Damaged object            |
| `damage`   | `number`               | Incoming damage           |
| `dmgType`  | `string`               | Damage/warhead identifier |
| `frame`    | `number`               | Current logical frame     |
| `subc`     | `number`               | Sub-cell index            |

**Return contract:**

* `number` — replaces the incoming damage;
* `0` — cancels the damage;
* `nil` — leaves the original damage unchanged.

Example:

```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    local player = House.GetPlayer()

    if player and target and target:IsAlive() then
        if target:GetOwner() == player then
            return damage * 0.5
        end
    end

    return nil
end
```

Do not return negative damage values unless the native implementation explicitly documents such behavior.

---

### `OnUnitDestroyed(victim, killer)`

Called when a unit or building destruction event is dispatched.

**Arguments:**

* `victim` — destroyed techno object;
* `killer` — killing techno object, or `nil` when unavailable.

**Return value:** ignored.

Do not assume that `victim:IsAlive()` will return `true` inside this callback. The callback concerns a destruction event, so code should treat the victim as potentially non-live.

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

# 9. Global Callback

## `OnDebugCommand(text)`

`OnDebugCommand` is a global Lua callback rather than a mod-table method.

Define it as:

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text, 1)
end
```

Do **not** define it only as:

```lua
MyMod.OnDebugCommand = function(text)
    -- ...
end
```

unless the loader explicitly dispatches the global callback through the mod table.

**Arguments:**

* `text` — command entered by the user.

**Return value:** ignored.

Because this callback uses a global Lua function name, multiple mods defining the same global callback can conflict with one another.

---

# 10. Multiplayer Determinism

Lua gameplay logic must remain deterministic in multiplayer.

Avoid using wall-clock or machine-local state to make gameplay decisions.

Do not use:

```lua
os.time()
os.clock()
```

for gameplay decisions.

Prefer the logical game frame:

```lua
function MyMod.Update(frame)
    if frame % 30 == 0 then
        -- deterministic periodic logic
    end
end
```

Randomized gameplay should use a deterministic strategy appropriate for the engine's multiplayer model.

A simple frame-based example is:

```lua
math.randomseed(frame + 12345)

local roll = math.random(1, 100)
```

However, mods should avoid repeatedly reseeding global Lua RNG state unless this behavior has been verified against the intended multiplayer environment.

The safest rule is:

> Do not introduce nondeterministic external state into gameplay logic.

---

# 11. Logical Frame Semantics

`Update(frame)` operates on the game's logical frame rather than the render frame rate.

This means gameplay code should use:

```lua
frame
```

or:

```lua
Game.GetFrame()
```

for timing-sensitive gameplay logic.

Example:

```lua
function MyMod.Update(frame)
    if frame % 60 == 0 then
        -- Execute once every 60 logical frames.
    end
end
```

Do not use render-time polling as a substitute for logical-frame timing.

---

# 12. Mod Loading

Mods are loaded from the `scripts/mods/` directory.

Typical structure:

```text
scripts/
├── init.lua
├── active_mods.txt
└── mods/
    ├── my_first_mod/
    │   └── main.lua
    └── another_mod/
        └── main.lua
```

Enable a mod through:

```text
my_first_mod
another_mod
```

in:

```text
scripts/active_mods.txt
```

The name corresponds to the mod directory.

A basic mod should return its callback table:

```lua
local MyMod = {}

function MyMod.Update(frame)
    -- ...
end

return MyMod
```

---

# 13. Complete Minimal Example

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    Engine.PrintMessage("MyMod loaded!", 1)
end

function MyMod.Update(frame)
    if frame % 300 == 0 then
        local player = House.GetPlayer()

        if player then
            Engine.PrintMessage(
                "Current credits: " .. player:GetCredits(),
                1
            )
        end
    end
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    if not victim then
        return
    end

    local typeName = victim:GetTypeName() or "unknown"
    Engine.PrintMessage(typeName .. " destroyed.", 2)
end

return MyMod
```

# 14. Related Documentation

* `TUTORIAL.md` — beginner guide for creating LuaAPI mods.
* `CAPABILITIES_AND_COOKBOOK.md` — tested capabilities and practical recipes.
* `ENGINEERING_LESSONS.md` — implementation lessons, limitations, and technical findings.
* `MOD_MANAGER.md` — mod loading and distribution.
* `ROADMAP.md` — project milestones and development status.

These documents provide additional context but must not redefine the API contract documented here.
