# LuaAPI for Red Alert 2 — API Reference

> **Version:** `2.0.0` (Beta)
> **Milestone:** `11`  
> **Target:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Last Updated:** `2026-09-21` (Extras-2 sweep; no version bump, no behavior change)

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

> 🧩 **Lua Gameplay Framework** — On top of the native bindings below, LuaAPI
> ships a small **Lua-side gameplay framework** (`scripts/framework/`): an
> EventBus, frame-based Timer, Query helpers, a Task primitive, and a
> UnitController. It composes the native methods and adds **no** native
> bindings. See [`docs/FRAMEWORK.md`](docs/FRAMEWORK.md).

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

## `unit:Attack(target)`

Orders a mobile unit to attack a specific target object.

```lua
local ok = unit:Attack(enemy)
```

Uses the native `SetTarget` + `QueueMission(Attack)` path (not the
player-click path). Only works for mobile `FootClass`-derived objects;
returns `false` for buildings and invalid targets. The assigned target
can be read back with `unit:GetTarget()`.

**Returns:** `boolean`.

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

## `unit:GetMission()`

Reads the object's current native mission (`TechnoClass::CurrentMission`).

```lua
local mission = unit:GetMission()
-- "Guard", "Move", "Attack", "Stop", ... (or a numeric code when no name resolves)
```

**Returns:** `string` or `number` (`nil` when validation fails).

---

## `unit:IsOnFloor()`

Checks whether the object is currently on the ground (landed).

```lua
if unit:IsOnFloor() then
    -- unit is on the ground
end
```

For aircraft: returns `true` when landed on a helipad or airfield; `false` when airborne.
For ground units: typically always `true`.

**Returns:** `boolean`.

---

## `unit:IsInAir()`

Checks whether the object is currently airborne.

```lua
if unit:IsInAir() then
    -- unit is flying
end
```

For aircraft: returns `true` when flying; `false` when landed.
For ground units: typically always `false`.

**Returns:** `boolean`.

---

## `unit:IsLanding()`

Checks whether an aircraft is currently in the landing descent phase.

```lua
if unit:IsLanding() then
    -- aircraft is descending to land
end
```

Only meaningful for `AircraftClass` with `FlyLocomotionClass`. Returns `false` for non-aircraft.

**Returns:** `boolean`.

---

## `unit:Return()`

Orders an aircraft to return to the nearest airfield/helipad and land (native `Mission::Return`).

```lua
local ok = unit:Return()
```

Only works for `AircraftClass`. Returns `true` if the mission was queued successfully.

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

## `unit:SetHealthRatio(percent)`

Sets health using a percent value (the implementation divides by 100).

```lua
unit:SetHealthRatio(35)
```

Examples:

```text
35  = 35%
100 = 100%
```

> ⚠️ Fractional 0–1 inputs do not work as fractions here: the value is
> truncated to an integer first, so `1.0` means ~1%, not 100%.
> Pass 0–100.

**Returns:** no value.

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

## `unit:MarkBounty([color [, durationFrames]])`

Registers a draw-only bounty overlay (rectangle + `BOUNTY` label) for the
unit, drawn in the `DrawAsVXL` detour after the original draw call. Keyed by
`UniqueID`, never by pointer; expires by logical frame, explicit clear, or
session reset. Simulation state is untouched (CnCNet-safe).

```lua
unit:MarkBounty()            -- green (money), until cleared
unit:MarkBounty(0x00FF00, 4500)
```

**Parameters:**

- `color` — `COLORREF` (default `0x00FF00`, green); converted internally to
  raw 5-6-5 for the rectangle, passed as-is to the label text
- `durationFrames` — `0`/omitted = until cleared

**Returns:** `boolean` (`false` for non-`Unit` kinds — the detour covers
vehicles/ships only — and invalid objects).

---

## `unit:ClearBountyMark()`

Removes the unit's bounty overlay registration.

```lua
unit:ClearBountyMark()
```

**Returns:** no value.

---

# 🔫 Sub-Turret API — REMOVED 2026-09-21

> The native `SubTurretManager` sidecar (`src/sub_turret.*`), its Lua
> bindings (`AddSubTurret` / `GetSubTurretCount` / `GetSubTurret` /
> `SetSubTurretTarget` / `FireSubTurret` / `ClearSubTurrets` /
> `SetSplitTargets` / `FireSplitSalvo`), the spawned-missile decoupling,
> the `BulletHook` Detonate hook (`src/bullet_hook.*`), the `EventHook`
> spawner-target module (`src/event_hook.*`), and `FireProjectile` were
> removed — zero live consumers in `scripts/`, per-frame full-array sweep
> and per-detonation hook overhead on the game thread. History preserved in
> git and in `PROJECT/ROADMAP.md` (Milestone 10). `IronCurtain` (standalone,
> synchronous) is kept.

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

Returns the coordinates of a map waypoint, read live from the scenario's
waypoint table (`ScenarioClass`, engine range `[0..701]`).

```lua
local pos = World.GetWaypoint(5)

if pos then
    print(pos.x, pos.y)
end
```

**Returns:** position table `{x, y}` in map-cell coordinates, or `nil`
when the id is out of range (negative or `>= 702`), the waypoint is not
defined on the current map, or no scenario is loaded.

Waypoint IDs are **0-based** engine indices. The lookup reads live
scenario state on every call (nothing cached), so it is safe across
match resets.

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

## `game.GetWaypoint(id)`

Legacy/global form of the waypoint query (same implementation and
contract as `World.GetWaypoint` — dot-call only, like all plain
namespace functions).

```lua
local pos = game.GetWaypoint(5)
```

**Returns:** position table `{x, y}` or `nil` (same rules as above).

---

## `game.GetUnitsInRadius(x, y, radius)`

Legacy/global form of the spatial unit query.

```lua
local units = game.GetUnitsInRadius(100, 100, 15)
```

**Returns:** Lua table.

---

> `game:GetEventHookOverrideCount()` / `game:ClearEventHookOverrides()`
> were removed 2026-09-21 with the `EventHook` module.

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

## `Engine.ClearBountyMarks()`

Clears every bounty overlay registration (global reset; also runs
automatically on session reset).

```lua
Engine.ClearBountyMarks()
```

**Returns:** no value.

---

## `Engine.SetBountyDrawMode(mode)`

Crash-isolation diagnostic switch: `0`=off (registry live, no pixels),
`1`=rectangle only, `2`=text only, `3`=full (default).

```lua
Engine.SetBountyDrawMode(1)
```

**Returns:** `boolean` (`false` for out-of-range input).

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

# 🧰 Implemented Extras (undocumented until 2026-09-20)

The following capabilities exist in the current build but were previously
absent from this reference. They are **implemented and source-verified**
(`src/lua_engine.cpp`, `src/bindings_techno.cpp`, `src/weapon_override.cpp`);
unless a live-game consumer is on record they are graded UNVERIFIED LIVE, and
all of them are **dev/diagnostic helpers first** — treat stability and exact
behavior as experimental.

### `Engine.WeaponExists(id)` → `boolean`

Checks whether a weapon ID exists in rules (`WeaponTypeClass::Find`). SEH-wrapped;
returns `false` on error or empty id.

```lua
if Engine.WeaponExists("ZeusTrail") then
    -- safe to reference the weapon
end
```

### `Engine.SetHudMuted(bool)` / `Engine.IsHudMuted()` → `nil` / `boolean`

Mutes/unmutes LuaAPI HUD output globally (affects `Engine.PrintMessage`
display); state is also logged. Use to silence a mod's HUD without touching
the game's own messages.

```lua
Engine.SetHudMuted(true)
if Engine.IsHudMuted() then ... end
```

### Global `WeaponOverride` table (dev/diagnostic — weapon swap at vet levels)

Installs a `GetPrimaryWeapon` hook (installed at DLL init; degrades to vanilla
behavior with a warning if the hook fails).

```lua
WeaponOverride.Set(typeId, vetLevel, weaponId) -- -> boolean
WeaponOverride.Get(typeId, vetLevel)           -- -> string | nil
WeaponOverride.Clear()                         -- clear all
WeaponOverride.Clear(typeId)                   -- clear one type
WeaponOverride.Clear(typeId, vetLevel)         -- clear one entry
```

Per-session state; cleared on session reset. Veteran level naming (`Rookie`/
`Veteran`/`Elite`-class keys) follows the implementation in
`src/weapon_override.cpp` — verify the exact key form in source before use.

### `World.GetAircraft()` → table of airborne technos

Returns airborne technos (same object contract as `World.GetUnits()`).

### `World.GetSelectedUnits()` / `World.GetSelectedTechnos()` → table

Returns the player's currently selected objects (reads
`ObjectClass::CurrentObjects`). `GetSelectedUnits` returns `UnitClass` objects;
`GetSelectedTechnos` the general set. Consumers must re-validate per use
(`IsAlive`) — selection contents change with player input at any moment.
Used live by the `dynamic_objective_defense` showcase (Gate 12.2).

> These entries completed the docs-vs-source sweep for Gate 1 (2026-09-20).
> The 2026-09-21 sweep below reopens the list: it found ~25 further
> implemented-but-undocumented bindings. If you find an implemented binding
> missing from this reference, record it rather than relying on it silently.

### Implemented Extras-2 (sweep 2026-09-21 — source-verified, mostly UNVERIFIED LIVE)

Source of truth: `src/bindings_techno.cpp` (`kTechnoMethods`,
`RegisterTechnoBindings`), `src/lua_engine.cpp` (`CreateEngine`),
`src/barrel_pitch.cpp` (`RegisterBindings`), `src/bindings_production.cpp`.
Grading: implemented + source-verified; UNVERIFIED LIVE unless a live
consumer is named. Dev/diagnostic framing — stability not promised.

#### `Techno` info / economy reads (used live by `bounty_hunter` v2)

```lua
local vet  = unit:GetVeterancy() -- "rookie" | "veteran" | "elite" (pcall-guarded by mods)
local cost = unit:GetCost()       -- rules price, number (reward math base)
```

#### `Techno` ammo / speed (no live consumer on record)

```lua
local ammo = unit:GetAmmo()
unit:SetAmmo(n)
local speed = unit:GetBaseSpeed()
unit:SetSpeedPercent(pct)
```

#### `Techno` orders beyond `MoveTo/Attack/Hunt` (no `Stop` section existed before)

```lua
unit:Stop()        -- native Stop mission; the mission name checked by IsIdle
unit:Unload()      -- transport unload primitive
unit:Scatter()     -- documented above; listed here for completeness
```

#### `Techno` harvest primitives (no live consumer on record)

```lua
local loc = unit:GetHarvestLocation() -- harvest anchor query
unit:HarvestAt(x, y)                  -- harvest order primitive
```

#### `Techno` deploy family (consumer: `heli_repair_test` diagnostic)

```lua
unit:Deploy()          -- returns boolean
unit:TryToDeploy()
unit:Undeploy()
unit:CanDeployNow()    -- -> boolean (gate before Deploy)
unit:IsDeployed()      -- -> boolean
unit:IsDeploying()     -- -> boolean
unit:IsUndeploying()   -- -> boolean
```

#### `Techno` experimental combat (no live consumer on record)

```lua
unit:IronCurtain(frames)    -- invulnerability primitive (standalone, kept)
```

> `FireProjectile` removed 2026-09-21 with `BulletHook` (was its only
> `Register` caller). The whole M10 sub-turret family
> (`AddSubTurret` / `SetSplitTargets` / `FireSplitSalvo` / …) removed
> the same day — see §Sub-Turret API.

#### `Input.WasKeyPressed(vk)` → `boolean` (live consumers: `command_authority` hotkeys, `tesla_mcv` T)

Edge-triggered (pressed-now AND not-pressed-before), per
`src/bindings_techno.cpp:1678`. State in `g_keyPrevState[256]`.

```lua
if Input.WasKeyPressed(0x54) then -- T, once per press
end
```

#### Global `AI` table — production (BLOCKED live, do not design on it)

Registered in `src/bindings_production.cpp:120-124`; absent from this
reference until now. `PROJECT/GATES.md` Gate 2 records
`AI.QueueUnit` as accepted-but-no-output live (2 runs) — BLOCKED.

```lua
AI.QueueUnit(house, "APOC") -- -> boolean (accepted, not produced live)
AI.CountUnit(house, "APOC") -- -> integer (factory queue counts)
```

#### `Engine` extras beyond §Engine API

```lua
Engine.GetBarrelPitchOverride(unitId)   -- -> number | nil (manual pitch read-back)
Engine.GetBarrelPitchAuto(unitId)       -- -> number, live sample (used by barrel_elevation_diag)
Engine.ClearBarrelPitchOverride(unitId) -- clear one manual override
Engine.ClearAllBarrelPitchOverrides()   -- clear all manual overrides
Engine.SetPersistentBarrelPitch(typeId, deg)   -- persistent type-field path (M16 static experiment)
Engine.ClearPersistentBarrelPitch(typeId)
Engine.version -- string field on the Engine table (project version, `"2.0.0"`)
```

---

# 📡 Callback Model

LuaAPI uses a mod-table callback model for the per-frame gameplay callback. Mods return a table and the loader dispatches its `Update` method.

A typical mod has the form:

```lua
local MyMod = {}

function MyMod.Update(frame)
    -- logical-frame gameplay logic
end

return MyMod
```

Engine event callbacks (`OnPreDamage`, `OnScenarioStart`,
`OnUnitDestroyed`) are looked up as **globals**, not mod-table methods:
defining `MyMod.OnPreDamage` alone never fires. Define a global
function instead.

### `OnPreDamage(...)` (NOT WIRED — intended contract only)

> ⚠️ Status: the engine collects the global `OnPreDamage` reference
> every frame but currently never invokes it with damage arguments —
> no `ReceiveDamage` hook is installed in this build. Live engine
> damage does NOT reach Lua. The contract below describes the intended
> design for when interception is wired, not working behavior. Do not
> build a reactive-armor mechanic on this callback yet.

Intended form:

```lua
function OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    if dmgType == "energy" then
        return damage * 0.5
    end

    return nil
end
```

Intended return semantics (unverified live):

- `nil` — leave damage unchanged
- non-negative number — replace the damage value
- `0` — cancel the damage

> ⚠️ Never return negative damage. Avoid recursively generating additional damage from inside the callback without a re-entrancy guard.

### `OnScenarioStart()`

Dispatched as a global once when the logical frame counter reaches 1.

```lua
function OnScenarioStart()
    -- post-scenario initialization
end
```

> ⚠️ Do not assume this callback runs when a saved game is loaded. Runtime systems that require persistent state must account for the savegame lifecycle.

### `OnUnitDestroyed(...)` (NOT DISPATCHED — contract without invocation)

Defined as a global, but in the current build it is **never invoked**: the
C++ dispatch block (`lua_engine.cpp`, Gate 7.2) only fires when callback refs
are already queued, and nothing ever queues them — the branch is unreachable.
There is currently no destruction detection behind it. Do not build
death-reactive logic on this callback; use ID-diff death detection instead.
When (and if) a native death hook lands, the intended form is:

```lua
function OnUnitDestroyed(victim, killer)
    if victim == nil then return end
    -- not dispatched in the current build; intended form only
end
```

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

### `OnTick(frame)` — loader-owned (Internal)

Global dispatcher owned by the loader (`scripts/init.lua`), not by mods.
The C++ frame handler calls it once per logical frame with the current
engine frame number; it fans out to every loaded mod's `Update(frame)`
inside per-mod error isolation.

Mods must define `Update` on their returned table — never replace the
global `OnTick` (the loader defines it after loading mods, so a
mod-defined `OnTick` would be overwritten and would break dispatch for
every other mod).

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

# 🔭 Barrel Elevation (M16, experimental)

Dynamic barrel pitch/elevation for voxel-turret units. The automatic mode is
always on in the diagnostic mod: every unit with a voxel turret gets a
distance-based pitch (8° at 4 cells → 55° at 14 cells) via a draw-time
`FireAngle` swap in a `UnitClass::DrawAsVXL` detour (`src/barrel_pitch.cpp`).
Draw-only, client-local, simulation untouched — CnCNet-safe.

> ⚠️ **Asset requirement (proven 2026-09-20):** stock YR vehicles fuse turret
> and barrel into one voxel, and `FireAngle` only rotates a SEPARATE barrel
> voxel (ModEnc; community-confirmed). On stock units the mechanism applies
> but cannot show visible movement. Visible elevation requires an asset with
> a separate barrel voxel and/or a multi-frame turret HVA (pose frames).

## `Engine.SetBarrelPitchOverride(unitId, pitchDegrees)`

Manual pitch for one unit. `0` horizontal, `90` straight up, negative allowed
(below horizontal), clamped to `[-90, 90]`.

```lua
Engine.SetBarrelPitchOverride(unitId, 35.0)
```

**Returns:** `true`.

---

## `Engine.SetBarrelPitchAuto(unitId, enabled)`

Per-unit AUTO mode: pitch computed natively each draw call from the live
target distance; without a target the unit draws vanilla.

```lua
Engine.SetBarrelPitchAuto(unitId, true)
```

**Returns:** `true`.

---

## `Engine.SetBarrelPitchAutoAll(enabled)`

Global AUTO mode (used by the diagnostic mod, no keys): every voxel-turret
unit — player and AI alike — pitches by its live target distance.

```lua
Engine.SetBarrelPitchAutoAll(true)
```

**Returns:** `true`.

---

## `Engine.GetBarrelPitchAutoCount()`

Number of units that drew with an AUTO-computed pitch on recent frames.

```lua
local n = Engine.GetBarrelPitchAutoCount()
```

**Returns:** `number`.

---

## `unit:GetTurretAnimFrame()` / `unit:SetTurretAnimFrame(frame)` /
## `unit:GetTurretAnimFrameCount()`

HVA pose-frame access on the turret voxel (the TS elevation mechanism).
`SetTurretAnimFrame` writes `TurretAnimFrame`; the engine wraps by
`FrameCount`. `GetTurretAnimFrameCount` reads
`Type->TurretVoxel.HVA->FrameCount` (0 for SHP turrets / no turret).

```lua
local count = unit:GetTurretAnimFrameCount()
if count > 1 then
    unit:SetTurretAnimFrame(count - 1) -- high pitch pose
end
```

**Returns:** frame `number` / applied frame `number` / frame-count `number`.

---

# 📚 Related Documentation

- [`README.md`](README.md) — Project overview and installation
- [`docs/FRAMEWORK.md`](docs/FRAMEWORK.md) — Lua Gameplay Framework reference (Milestone 14)
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
