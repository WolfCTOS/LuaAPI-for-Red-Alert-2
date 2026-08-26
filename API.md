# 📚 LuaAPI for Red Alert 2: Yuri's Revenge — API Reference Manual

## 🛡️ Introduction & Security Architecture

### `ValidateTechno()` — Pointer & RTTI Safety

The engine employs a rigorous pointer validation system to prevent the most common failure mode in C&C modding: **`0xC0000005` Access Violations** when dereferencing destroyed or invalid objects.

#### Validation Checks (performed in every binding):

| Check | Method | Purpose |
|-------|--------|---------|
| **Null** | `ptr != nullptr` | Ensures object exists in memory |
| **RTTI Type** | `ptr->WhatAmI()` | Verifies type is `Building`, `Unit`, `Infantry`, or `Aircraft` |
| **Life Flag** | `ptr->Health > 0 && !ptr->InLimbo` | Confirms object is alive and not awaiting cleanup |

#### Safety Guarantee:

> **If `ValidateTechno()` returns `false`, every binding immediately returns `nil` / `false` / `0` to Lua and logs a `LUA_LOG_WARN` entry.** The host process (`gamemd.exe`) is **never** crashed by invalid pointer dereference.

#### Why This Matters:

- C&C RA2 ships (1.001) have no built-in Lua guardrails
- A single `nil` check missing in a binding can destroy the host process
- `ValidateTechno()` provides a **single source of truth** for all 20+ TechnoClass bindings
- Enables safe iteration over `TechnoClass::Array` without fear of dangling pointers

---

## 📡 Event Bus & Callbacks (`game_RegisterEvent`)

The engine provides a flexible event system for inter-module communication. Callbacks are registered once and fired each game frame from `OnGameFrame()`.

### `game_RegisterEvent(eventName, callbackFunction)`

| Parameter | Type | Description |
|-----------|------|-------------|
| `eventName` | `string` | One of the predefined event names below |
| `callbackFunction` | `function(Lua_State* L)` | Lua function to be called each frame the event is active |

#### Registered events are **persistent** across map restarts unless explicitly cleared by `ResetSession()`.

---

### `"OnPreDamage"` Event

Fired **before** damage is applied to a target unit/structure. Return value determines final damage:

| Return Value | Effect |
|------------|--------|
| `number` (positive) | Damage is **modified** to this amount |
| `0` | Damage is **completely cancelled** (unit takes no damage) |
| `nil` | Damage proceeds **unmodified** (original pipeline) |

#### Signature:

```lua
function myCallback(attacker, target, damage, dmg_type, frame, subc)
    -- attacker  : TechnoClass pointer (or nil)
    -- target    : TechnoClass pointer (the unit taking damage)
    -- damage    : number (original damage amount)
    -- dmg_type  : string (e.g. "fire", "explosive", "energy")
    -- frame     : number (current game frame)
    -- subc      : table (sub-parameters from rules, optional)
    
    -- Example: absorb 50% energy/explosive damage
    if dmg_type == "energy" or dmg_type == "explosive" then
        return damage * 0.5   -- return modified half-damage
    end
    return nil               -- let original damage through
end
```

---

### `"OnScenarioStart"` Event

Fired **exactly once** on game frame 1 after a new map/mission loads. Use for army setup, initial buffs, or cinematic triggers.

#### Signature:

```lua
function myStartupCallback()
    -- This runs once when the match begins
    -- Ideal for: spawning fortified starts, setting initial HP, etc.
end
```

#### Usage:

```lua
function OnScenarioStart()
    -- Called once on frame 1 after map load
    Engine.PrintMessage("[System] Scenario initialized!")
end
```

---

### `"OnUnitDestroyed"` Event

Fired when a unit/structure is slated for removal from the engine arrays. Provides victim and killer context.

#### Signature:

```lua
function myDestroyCallback(victim, killer)
    -- victim    : TechnoClass pointer (the unit that died)
    -- killer    : TechnoClass pointer (the unit that dealt killing blow, or nil)
    
    if killer then
        -- Award bounty, play effects, etc.
        house_AddCredits(killer:GetHouse(), 100)
        game_PrintMessage("[Bounty] +$100 for kill!", 1)
    end
end
```

---

## 🛠️ TechnoClass Methods (Units & Buildings)

All units and structures expose the following Lua methods via the `LuaAPI.Techno` metatable.

| Method | Return Type | Description |
|--------|-------------|-------------|
| `unit:GetHouse()` | `HouseClass*` | Returns the owning house pointer |
| `unit:GetHealthRatio()` | `number` | Float 0.0 (dead) → 1.0 (full health) |
| `unit:SetHealthRatio(ratio)` | `boolean` | `true` if successfully set, `false` otherwise |
| `unit:AttachParticleSystem(sys_name)` | `boolean` | Attaches a named particle effect ("DamageSmokeSys", "DamageFireSys", etc.) |
| `unit:IsAlive()` | `boolean` | `true` if `Health > 0` and not in Limbo |
| `unit:GetType()` | `string` | Engine type ID (e.g., `"HTNK"` = Apache, `"E1"` = Allied Battle Tank, `"INF"` = Infantry) |

#### Example — Damage Over Time Script:

```lua
local DoT = {}

function DoT.OnTick(frame)
    for _, unit in ipairs(World.GetUnits()) do
        if not unit:IsAlive() then continue end
        if unit:GetHealthRatio() < 0.25 then
            -- Apply 5% damage per frame when below 25% HP
            unit:SetHealthRatio(unit:GetHealthRatio() - 0.05)
        end
    end
end

return DoT
```

---

## 🏘️ Economy & Players (`HouseClass`)

All player-facing economy functions operate on `HouseClass` pointers.

| Function | Return Type | Description |
|----------|-------------|-------------|
| `game_GetLocalPlayer()` | `HouseClass*` | Pointer to the local player's house (first player in slot) |
| `house_GetCredits(house)` | `number` | Current credit balance of the given house |
| `house_AddCredits(house, amount)` | `number` | **New** credit balance after adding `amount` |

#### Example — Bounty System:

```lua
local Bounty = {}

function Bounty.OnUnitDestroyed(victim, killer)
    if not killer then return end
    local killerHouse = killer:GetHouse()
    if not killerHouse then return end
    
    local newCredits = house_AddCredits(killerHouse, 50)
    game_PrintMessage(string.format("[Bounty] +$50! Total: $%d", newCredits), 1)
end

return Bounty
```

---

## 🌍 Spatial & Map Queries

### `game_GetWaypoint(waypoint_id)`

Looks up a waypoint by its integer ID (as defined in the map or rules).

```lua
local wp = game_GetWaypoint(1)
if wp then
    -- wp = { x = 1024, y = 2048 } -- map coordinates in pixels
    -- Or: wp.cell = cell_index
end
```

Returns `nil` if the waypoint ID does not exist.

---

### `game_GetUnitsInRadius(cellX, cellY, radiusCells)`

Returns a table of TechnoClass pointers for all units within the specified radius (in **cells**, where 1 cell = 256 pixels).

```lua
local enemies = game_GetUnitsInRadius(50, 30, 10)  -- 10-cell radius center at (50,30)
if #enemies > 0 then
    for _, unit in ipairs(enemies) do
        if unit:IsAlive() then
            -- Apply logic to nearby enemies
        end
    end
end
```

**Filtering:** Only units passing `ValidateTechno()` are included. Dead or invalid objects are silently excluded.

---

## 🖥️ HUD & UI Messaging

### `game_PrintMessage(text, colorIndex)`

Writes a message to the in-game message feed (the same system used for unit selections, build completions, and system alerts).

| Parameter | Type | Description |
|-----------|------|-------------|
| `text` | `string` | Message text to display |
| `colorIndex` | `number` | Color slot (1 = green/standard, 2 = red/alert, etc.) |

#### Example:

```lua
game_PrintMessage("[LuaAPI] Mod loaded successfully!", 1)
```

Returns `true` on success.

---

## 🌐 Netcode & Determinism (CnCNet Rules)

### Mandatory RNG Seeding

All gameplay randomness **must** use the frame-seeded pattern. **Under no circumstances** may `os.clock()` or `os.time()` be used in multiplayer-designated logic.

```lua
-- ✅ CORRECT (deterministic across all clients)
math.randomseed(frame + 12345)
local roll = math.random(1, 100)

-- ❌ FORBIDDEN (causes OOS in CnCNet multiplayer)
-- math.randomseed(os.time())  -- Different on every PC!
-- math.randomseed(os.clock()) -- Different every frame, varies by host
```

#### RNG Pattern Details:

- `frame` is `Unsorted::CurrentFrame` from the engine main loop
- Seed `12345` is fixed and synchronized across all CnCNet clients
- All mods must adhere to this pattern; violations result in **immediate OOS (Out of Sync)** and match desynchronization

---

## 📦 Release History

| Version | Date | Changes |
|---------|------|---------|
| v0.1.0-alpha | 2026-08-26 | Initial release with core API, event bus, Techno methods, economy, spatial queries, HUD, and determinism rules |
| v1.0.0-stable | TBD | Planned: full API reference expansion, CnCNet ModBase integration, stress test validation |

---

## 📜 License

LuaAPI for Red Alert 2: Yuri's Revenge is free to use, modify, and redistribute for non-commercial modding purposes. Commercial redistribution requires permission from the original authors.

Yuri's Revenge is a trademark of Electronic Arts. This API is community-driven and is not affiliated with EA.

---

## 📞 Contact & Support

- **Issue Tracker:** [GitHub Issues](https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2/issues)
- **Community Discord:** [Haven / CnCNet Modding](https://discord.gg/cncnet)
- **Author:** LuaAPI Project Maintainers