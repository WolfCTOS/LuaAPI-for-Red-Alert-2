# LuaAPI for Red Alert 2 — API Reference

> **Version:** `1.1.0`  
> **Milestone:** `11`  
> **Target:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Last Updated:** `2026-08-31`

This document is the reference for the **currently implemented LuaAPI interface**.

> ⚠️ **Important:** This file documents implemented bindings. Planned or experimental systems must not be presented as stable API.

---

## 🏗️ API Architecture

LuaAPI exposes native Yuri's Revenge objects through Lua namespaces and userdata bindings.

| Namespace / Object | Purpose |
|---|---|
| `House` | Access to player/house objects and economy |
| `World` | Global unit/building queries and map queries |
| `game` | Lower-level map/event-hook diagnostics |
| `Engine` | Engine/HUD functions |
| `Game` | Currently exposes the debug HUD text helper |
| `Techno` object | Validated units, infantry, aircraft, and buildings |

LuaAPI is designed around a native C++ safety layer with Lua controlling gameplay behavior.

---

## 🛡️ Pointer Safety

Engine objects are represented by native pointers wrapped in Lua userdata. Objects can become invalid when destroyed or when the game session changes.

Bindings therefore validate native objects before using them. Lua scripts should still treat engine objects as short-lived references.

A safe pattern is:

```lua
local units = World.GetUnits()
local unit = units[1]

if unit and unit:IsAlive() then
    local hp = unit:GetHealth()
end
```

> ⚠️ `IsAlive()` is a validity/liveness check at the time of the call. It does not make a previously stored pointer permanently safe.

---

# 🎖️ Techno / Unit API

The following methods are registered on the `LuaAPI.Techno` userdata.

## `unit:GetTypeName()`

Returns the object's INI type identifier.

```lua
local typeName = unit:GetTypeName()
-- "DRED", "APOC", "E1", etc.
```

**Returns:** `string` or no Lua value when validation fails.

---

## `unit:GetHealth()`

Returns current health.

```lua
local hp = unit:GetHealth()
```

**Returns:** `number`.

---

## `unit:GetMaxHealth()`

Returns the object's configured maximum health.

```lua
local maxHp = unit:GetMaxHealth()
```

**Returns:** `number`.

---

## `unit:GetOwner()`

Returns the house that owns the object.

```lua
local owner = unit:GetOwner()
```

**Returns:** `House` userdata or `nil`.

---

## `unit:GetPosition()`

Returns the object's position in map-cell coordinates.

```lua
local pos = unit:GetPosition()
print(pos.x, pos.y, pos.z)
```

**Returns:** table containing `x`, `y`, and `z`.

Coordinates are converted from the engine's 256-lepton cell representation.

---

## `unit:IsAlive()`

Checks whether the object passes the native liveness validation.

```lua
if unit:IsAlive() then
    -- object is currently usable
end
```

**Returns:** `boolean`.

---

## `unit:GetDistanceTo(other)`

Returns Euclidean distance between two techno objects in map cells.

```lua
local distance = unit:GetDistanceTo(enemy)
```

**Returns:** `number`, or `nil` when the second object is invalid.

---

## `unit:GetId()`

Returns the engine-wide unique object ID.

```lua
local id = unit:GetId()
```

**Returns:** `number`.

---

## `unit:GetKind()`

Returns the native object category.

```lua
local kind = unit:GetKind()
```

Possible values include:

```text
building
unit
infantry
aircraft
other
```

**Returns:** `string`.

---

## `unit:Scatter([x, y])`

Orders a mobile techno to scatter from its current position, optionally using a supplied cell position.

```lua
unit:Scatter()
unit:Scatter(100, 100)
```

**Returns:** no value.

Only mobile `FootClass`-derived objects can perform the movement operation.

---

## `unit:MoveTo(x, y)`

Queues a movement order to the specified map cell.

```lua
local ok = unit:MoveTo(100, 120)
```

**Returns:** `boolean`.

---

## `unit:Hunt()`

Queues the native Hunt mission for a mobile unit.

```lua
unit:Hunt()
```

**Returns:** no value.

---

## `unit:IsIdle()`

Checks whether a mobile unit is currently in `Guard`, `Stop`, or `Sleep` mission state.

```lua
if unit:IsIdle() then
    -- idle
end
```

**Returns:** `boolean`.

---

## `unit:IsAttacking()`

Checks whether the object's current mission is `Attack`.

```lua
if unit:IsAttacking() then
    -- attacking
end
```

**Returns:** `boolean`.

---

## `unit:GetTarget()`

Returns the object's current native target when available.

```lua
local target = unit:GetTarget()
```

**Returns:** a `Techno` object or `nil`.

---

## `unit:TakeDamage(amount, [warhead])`

Applies damage through the native `ReceiveDamage` pipeline when a suitable warhead is available.

```lua
local remainingHp = unit:TakeDamage(100, "TerrorBombWH")
```

**Parameters:**

- `amount` — positive damage amount
- `warhead` — optional warhead ID

**Returns:** remaining health as `number`.

The implementation uses a fallback warhead chain when the requested warhead cannot be resolved.

> ⚠️ This is a real engine damage operation, not merely a Lua-side health assignment.

---

## `unit:Disable(frames)`

Temporarily disables a techno using native engine mechanisms.

```lua
unit:Disable(90)
```

**Parameters:**

- `frames` — duration in logical game frames

Buildings use their power/disabled state; mobile objects use the engine's paralysis mechanism. State is restored when the timer expires.

---

## `unit:SetHealthRatio(ratio)`

Sets health using a normalized ratio.

```lua
unit:SetHealthRatio(0.35)
```

Examples:

```text
0.35 = 35%
1.00 = 100%
```

**Returns:** `boolean`.

---

## `unit:AttachParticleSystem(name)`

Attaches a particle system to the object.

```lua
unit:AttachParticleSystem("DamageSmokeSys")
```

**Parameters:**

- `name` — particle-system identifier

**Returns:** `boolean`.

---

# 🔫 Sub-Turret API

LuaAPI exposes a native sidecar sub-turret system for additional turret state and explicit firing.

## `unit:AddSubTurret(section, offX, offY, offZ, rot, rof)`

Adds a sub-turret to a techno.

```lua
unit:AddSubTurret(1, 40, 0, 15, 12, 90)
```

| Parameter | Type | Description |
|---|---|---|
| `section` | `number` | Voxel section index |
| `offX` | `number` | X offset in leptons |
| `offY` | `number` | Y offset in leptons |
| `offZ` | `number` | Z offset in leptons |
| `rot` | `number` | Rotation step/speed |
| `rof` | `number` | Base rate of fire in logical frames |

**Returns:** `boolean`.

---

## `unit:GetSubTurretCount()`

Returns the number of sub-turrets currently attached.

```lua
local count = unit:GetSubTurretCount()
```

**Returns:** `number`.

---

## `unit:GetSubTurret(index)`

Returns the internal state of a sub-turret.

```lua
local turret = unit:GetSubTurret(1)
```

**Returns:** table or `nil`.

The returned table contains:

```text
section
offX
offY
offZ
facing
targetFacing
rot
rofTimer
baseRof
```

Indexes are **1-based**.

---

## `unit:SetSubTurretTarget(index, target)`

Assigns one explicit target to one sub-turret.

```lua
unit:SetSubTurretTarget(1, enemy)
```

**Returns:** `boolean`.

---

## `unit:FireSubTurret(index, target)`

Explicitly fires one sub-turret at a validated target.

```lua
unit:FireSubTurret(1, enemy)
```

**Returns:** `boolean`.

---

## `unit:ClearSubTurrets()`

Removes the unit's sub-turret state from the native manager.

```lua
unit:ClearSubTurrets()
```

**Returns:** no value.

---

## `unit:SetSplitTargets(targets)`

Assigns a Lua array of targets across available sub-turrets.

```lua
unit:SetSplitTargets({enemyA, enemyB, enemyC})
```

One supplied target is assigned per available turret according to the native split-target implementation.

**Returns:** `boolean`.

---

## `unit:FireSplitSalvo()`

Fires the configured sub-turrets at their assigned targets.

```lua
unit:FireSplitSalvo()
```

**Returns:** `boolean`.

> 🧠 Target acquisition remains gameplay logic. The native sub-turret manager stores state, handles safe references, rotation/timers, and performs explicit firing requested by Lua.

---

# 💰 House API

## `House.GetPlayer()`

Returns the current human player's house.

```lua
local player = House.GetPlayer()
```

**Returns:** `House` userdata or `nil`.

---

## `House.GetCount()`

Returns the number of houses in the engine house array.

```lua
local count = House.GetCount()
```

**Returns:** `number`.

---

## `House.GetByIndex(index)`

Returns a house by engine-array index.

```lua
local house = House.GetByIndex(0)
```

**Returns:** `House` userdata or `nil`.

Indexes are **0-based**.

---

## `house:GetCredits()`

Returns current available money.

```lua
local credits = house:GetCredits()
```

**Returns:** `number`.

---

## `house:SetCredits(amount)`

Sets the house's available credits by applying the required transaction delta.

```lua
house:SetCredits(5000)
```

**Parameters:**

- `amount` — target credit balance

**Returns:** no value.

---

## `house:AddCredits(amount)`

Adds or subtracts credits.

```lua
house:AddCredits(500)
house:AddCredits(-100)
```

**Parameters:**

- `amount` — credit delta

**Returns:** no value.

---

## `house:GetPowerOutput()`

Returns total power production.

```lua
local output = house:GetPowerOutput()
```

**Returns:** `number`.

---

## `house:GetPowerDrain()`

Returns total power consumption.

```lua
local drain = house:GetPowerDrain()
```

**Returns:** `number`.

---

## `house:GetName()`

Returns the engine house ID/name.

```lua
local name = house:GetName()
```

**Returns:** `string`.

---

## `house:IsHuman()`

Checks whether the house is controlled by a human.

```lua
if house:IsHuman() then
    -- human-controlled
end
```

**Returns:** `boolean`.

---

## `house:IsAlliedWith(otherHouse)`

Checks alliance status between two houses.

```lua
if house:IsAlliedWith(enemyHouse) then
    -- allied
end
```

**Returns:** `boolean`.

---

## `house:SpawnUnit(typeId, count, x, y, facing, force, action)`

Development/gameplay helper for creating units.

```lua
local created = player:SpawnUnit(
    "APOC",
    5,
    100,
    100,
    0,
    false,
    "hunt"
)
```

| Parameter | Description |
|---|---|
| `typeId` | INI unit type identifier |
| `count` | Number of units; defaults to `1` |
| `x` | X map cell |
| `y` | Y map cell |
| `facing` | Direction `0–255`; defaults to `0` |
| `force` | Force-spawn flag; defaults to `false` |
| `action` | Optional action; `"hunt"` queues Hunt |

When normal spawning is used, the implementation searches for a nearby valid cell within its configured radius. The current implementation uses a radius of **3 cells** for the fallback search.

**Returns:** `number` — successfully created units.

---

# 🌍 World API

## `World.GetBuildings()`

Returns building objects from the engine building array.

```lua
local buildings = World.GetBuildings()
```

**Returns:** Lua table of `Techno` objects.

---

## `World.GetUnits()`

Returns mobile technos: vehicles, infantry, and aircraft.

```lua
local units = World.GetUnits()
```

**Returns:** Lua table of `Techno` objects.

---

## `World.GetAllUnits()`

Returns every supported techno in the engine techno array, including buildings.

```lua
local objects = World.GetAllUnits()
```

**Returns:** Lua table of validated `Techno` objects.

> 💡 Use this for global scans. It does not depend on an arbitrary spatial radius.

---

## `World.GetWaypoint(id)`

Returns the coordinates of a map waypoint.

```lua
local pos = World.GetWaypoint(5)

if pos then
    print(pos.x, pos.y)
end
```

**Returns:** position table or `nil`.

---

## `World.GetUnitsInRadius(x, y, radius)`

Returns techno objects within a specified radius in map cells.

```lua
local units = World.GetUnitsInRadius(100, 100, 15)
```

**Parameters:**

- `x` — center X cell
- `y` — center Y cell
- `radius` — radius in cells

**Returns:** Lua table of matching `Techno` objects.

### ⚠️ Integer-width warning

RA2 uses **256 leptons per cell**. Native squared-distance calculations must use sufficiently wide arithmetic for large radii.

For whole-map searches, prefer `World.GetAllUnits()` instead of using an unnecessarily large radius.

---

# 🔧 `game` Diagnostics API

The lowercase `game` namespace is a separate low-level/diagnostic namespace retained by the current implementation.

## `game:GetWaypoint(id)`

Legacy/global form of the waypoint query.

```lua
local pos = game.GetWaypoint(5)
```

**Returns:** position table or `nil`.

---

## `game:GetUnitsInRadius(x, y, radius)`

Legacy/global form of the spatial unit query.

```lua
local units = game.GetUnitsInRadius(100, 100, 15)
```

**Returns:** Lua table.

---

## `game:GetEventHookOverrideCount()`

Returns the current number of entries in the event-hook target override cache.

```lua
local count = game.GetEventHookOverrideCount()
```

**Returns:** `number`.

> ⚠️ This is a diagnostic API, not a general gameplay targeting API.

---

## `game:ClearEventHookOverrides()`

Clears the event-hook target override cache.

```lua
game.ClearEventHookOverrides()
```

**Returns:** no value.

---

# 💬 Engine API

## `Engine.PrintMessage(text)`

Displays a message through the game's message-list system.

```lua
Engine.PrintMessage("Hello, Commander!")
```

**Parameters:**

- `text` — UTF-8 message string

**Returns:** no value.

> ⚠️ The current native implementation does **not** expose a `colorIndex` argument.

---

# 🎮 Game API

## `Game.GetDebugHudText()`

Returns the current text used by the debug-console HUD indicator.

```lua
local text = Game.GetDebugHudText()
```

**Returns:** `string`.

This is a development/debug helper. It is not the logical frame API.

---

# 📡 Callback Model

LuaAPI uses a mod-table callback model for gameplay callbacks. Mods return a table and the loader dispatches implemented callback methods.

A typical mod has the form:

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    -- initialization
end

function MyMod.Update(frame)
    -- logical-frame gameplay logic
end

return MyMod
```

The current engine also maintains callback registries for damage, scenario-start, and unit-destruction events.

### `OnPreDamage(...)`

The damage interception callback is used by the verified shield/damage pipeline to modify damage before the engine completes damage resolution.

A typical implementation is:

```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    if dmgType == "energy" then
        return damage * 0.5
    end

    return nil
end
```

Return semantics used by the verified capability:

- `nil` — leave damage unchanged
- non-negative number — replace the damage value
- `0` — cancel the damage

> ⚠️ Never return negative damage. Avoid recursively generating additional damage from inside the callback without a re-entrancy guard.

### `OnScenarioStart()`

Used by mods for post-scenario initialization, such as configuring starting units.

> ⚠️ Do not assume this callback runs when a saved game is loaded. Runtime systems that require persistent state must account for the savegame lifecycle.

### `OnUnitDestroyed(...)`

Used by the destruction-event pipeline for gameplay reactions and cleanup.

The exact native payload should be kept synchronized with the implementation when the callback contract changes.

### `Update(frame)`

Runs on the game's logical-frame dispatch path.

```lua
function MyMod.Update(frame)
    if frame % 30 ~= 0 then
        return
    end

    -- periodic logic
end
```

> ⏱️ Gameplay timing should use the logical game frame rather than render FPS.

### `OnDebugCommand(text)`

Global development callback.

```lua
function OnDebugCommand(text)
    -- parse development command
end
```

Unlike normal mod callbacks, `OnDebugCommand` is global.

---

# 🌐 CnCNet / Multiplayer Notes

LuaAPI's main loop hook dispatches gameplay logic only when `Unsorted::CurrentFrame` changes.

Conceptually:

```text
Render / engine calls
        ↓
   MainLoop hook
        ↓
Current logical frame changed?
        ↓
       yes
        ↓
   LuaAPI dispatch
```

This prevents the same logical-frame gameplay state from being advanced multiple times merely because the process executes the main loop at a different render rate.

CnCNet may launch `gamemd-spawn.exe`; integrations must therefore resolve the actual game module/process rather than assuming the executable name is always `gamemd.exe`.

---

# 📐 Engineering Rules

1. **C++ manages native state; Lua controls gameplay behavior.**
2. **Validate engine-backed objects before use.**
3. **Do not retain stale native pointers across destruction or session transitions.**
4. **Defer container cleanup when iteration can trigger object destruction.**
5. **Invalidate references to destroyed targets immediately.**
6. **Account for savegame lifecycle; scenario-start initialization is not sufficient for loaded saves.**
7. **Use 64-bit or floating-point arithmetic where squared spatial values can exceed 32-bit range.**
8. **Drive gameplay timing from logical frames, not render FPS.**
9. **Treat hook/signature mismatches as compatibility conditions to investigate, not automatically as fatal errors.**

---

# 📚 Related Documentation

- [`README.md`](README.md) — Project overview and installation
- [`docs/TUTORIAL.md`](docs/TUTORIAL.md) — Beginner tutorial
- [`PROJECT/CAPABILITIES.md`](PROJECT/CAPABILITIES.md) — Verified capabilities and case studies
- [`PROJECT/ENGINEERING_LESSONS.md`](PROJECT/ENGINEERING_LESSONS.md) — Engineering lessons and debugging history
- [`PROJECT/ROADMAP.md`](PROJECT/ROADMAP.md) — Architecture roadmap
- [`PROJECT/CHANGELOG.md`](PROJECT/CHANGELOG.md) — Project history

---

## 🎯 Recommended Workflow

```text
Read the API
    ↓
Build a small Lua prototype
    ↓
Verify it in Yuri's Revenge
    ↓
Move unsafe native work into C++
    ↓
Expose a safe Lua binding
    ↓
Document the verified behavior
```

> **Build small. Test frequently. Verify before documenting.**
