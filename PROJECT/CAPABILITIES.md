# 💡 LuaAPI — Capabilities, Case Studies & Modder's Cookbook

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **API Version:** `1.1.0` — Milestone 11  
> **Purpose:** Proven mechanics, real-world case studies, reusable Lua recipes, and engineering lessons learned while developing LuaAPI.

This document describes functionality that has been **implemented and verified** in LuaAPI.

It focuses on practical examples rather than API definitions:

- What can LuaAPI currently do?
- How was each mechanic implemented?
- What problems were encountered?
- What architectural decisions solved them?
- How can modders reproduce the same functionality?

> ⚠️ **Important:** Only mechanics explicitly marked as **VERIFIED** should be treated as proven capabilities of the current implementation.

---

## 📚 Table of Contents

- [Case Study 1 — Sub-Frame Reactive Energy Shields](#-case-study-1--sub-frame-reactive-energy-shields)
- [Case Study 2 — Dynamic Bounties & Economy](#-case-study-2--dynamic-bounties--economy)
- [Case Study 3 — Battle-Damaged Starting Fleet](#-case-study-3--battle-damaged-starting-fleet)
- [Case Study 4 — Multi-Turret Batteries](#-case-study-4--multi-turret-batteries)
- [Case Study 5 — CnCNet Multiplayer & Dev Tools](#-case-study-5--cncnet-multiplayer--dev-tools)
- [Universal Engineering Principles](#-universal-engineering-principles)
- [Verification Policy](#-verification-policy)
- [Related Documentation](#-related-documentation)

---

# 🛡️ Case Study 1 — Sub-Frame Reactive Energy Shields

**Status:** ✅ **VERIFIED**  
**Reference implementation:** `shield_overload`

### 🎯 Outcome

LuaAPI can intercept incoming damage before the engine applies it to the target.

This makes it possible to implement:

- Energy shields
- Damage reduction
- Armor systems
- Directional mitigation
- Damage filters
- Custom defensive mechanics

### 💡 Why Sub-Frame Interception?

Polling damage from `Update()` is too late.

By the time a regular frame callback runs, the engine may have already resolved the damage.

`OnPreDamage` operates at the damage-processing boundary instead.

### ⚙️ Architecture

The intended flow is:

```text
Engine damage event
        ↓
   OnPreDamage
        ↓
      Lua
        ↓
 ┌──────┼────────┐
 ↓      ↓        ↓
nil   number     0
 ↓      ↓        ↓
Pass   Modify   Cancel
damage damage  damage
```

### 📝 Lua Recipe

```lua
-- scripts/mods/shield_overload/main.lua

local ShieldMod = {}

function ShieldMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    if dmgType == "energy" or dmgType == "explosive" then
        return damage * 0.5
    end

    return nil
end

return ShieldMod
```

### ⚠️ Engineering Lessons

- Never return negative damage values.
- Avoid triggering additional damage from inside `OnPreDamage` without a re-entrancy guard.
- Always validate `attacker` and `target` before accessing their methods.

---

# 💰 Case Study 2 — Dynamic Bounties & Economy

**Status:** ✅ **VERIFIED**  
**Reference implementation:** `bounty_hunter`

### 🎯 Outcome

LuaAPI can implement dynamic credit rewards based on combat events.

This enables:

- Kill rewards
- Combat bounties
- Economy modifiers
- Dynamic income systems
- HUD notifications

### ⚙️ Architecture

The general flow is:

1. Receive a combat callback.
2. Identify the attacker's house.
3. Modify the house's credits.
4. Display a message through the HUD.

### 📝 Lua Recipe

```lua
-- scripts/mods/bounty_hunter/main.lua

local BountyMod = {}

function BountyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    if attacker and attacker.GetOwner then
        local ownerHouse = attacker:GetOwner()
        local player = House.GetPlayer()

        if ownerHouse and player and ownerHouse == player then
            ownerHouse:AddCredits(50)
            Engine.PrintMessage("[Bounty] +$50 combat reward", 2)
        end
    end

    return nil
end

return BountyMod
```

### ⚠️ Engineering Lessons

Combat callbacks may receive a `nil` attacker.

This can happen when damage or destruction originates from:

- Environmental effects
- Triggers
- Crushing
- Other engine-side causes

Always validate the attacker before accessing its owner.

---

# 🚢 Case Study 3 — Battle-Damaged Starting Fleet

**Status:** ✅ **VERIFIED**  
**Reference implementation:** `damaged_fleet`

### 🎯 Outcome

LuaAPI can modify units immediately after scenario initialization, allowing mods to create pre-damaged starting forces.

Example uses:

- Battle-damaged fleets
- Damaged starting armies
- Custom scenario states
- Visual damage effects

### ⚙️ Architecture

`OnScenarioStart()` runs after scenario initialization.

The mod can then:

1. Query units.
2. Filter them by owner and type.
3. Modify their health.
4. Attach visual effects.

### 📝 Lua Recipe

```lua
-- scripts/mods/damaged_fleet/main.lua

local FleetMod = {}

function FleetMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive()
            and unit:GetOwner() == player
            and unit:GetTypeName() == "DEST" then

            unit:SetHealthRatio(0.35)
            unit:AttachParticleSystem("DamageSmokeSys")
        end
    end
end

return FleetMod
```

### ⚠️ Engineering Lessons

Do not access scenario-dependent game objects before the scenario has finished initializing.

Use `OnScenarioStart()` for post-load initialization.

> **Savegame note:** `OnScenarioStart()` does not fire when loading a savegame. Systems that require persistent runtime state must account for the savegame lifecycle separately.

---

# 🚢 Case Study 4 — Multi-Turret Batteries

**Status:** ✅ **VERIFIED**  
**Reference implementation:** `multi_turret_battleship`

### 🎯 Outcome

LuaAPI can extend units with additional turret systems and split targeting.

This enables:

- Multi-turret ships
- Multiple simultaneous targets
- Independent turret facing
- Split-salvo attacks
- Custom capital-ship weapon systems

### ⚙️ Architecture

The multi-turret system consists of a C++ state layer and Lua-controlled gameplay logic.

The C++ side manages:

- Turret state
- Target references
- Reload timers
- Turret offsets
- Safe pointer handling

Lua controls:

- Target selection
- Target allocation
- When a salvo is fired

### 🧠 Design Principle

C++ manages engine state. Lua decides gameplay behavior.

The C++ layer should not autonomously decide when units attack.

### ⚠️ Engineering Lessons

#### 1. Avoid autonomous firing in the C++ update loop

Target acquisition and firing inside the C++ tick caused unintended attacks while units were idle or moving.

The solution was to move firing decisions into explicit Lua calls.

#### 2. Defer container cleanup

Never erase entries from a container while iterating over it.

Use deferred cleanup instead.

#### 3. Invalidate dead targets

Target references must be invalidated when their underlying game objects are destroyed.

#### 4. Savegame lifecycle matters

`OnScenarioStart()` does not run after loading a savegame.

Systems that attach runtime state should detect and restore missing state during `Update()`.

#### 5. Use safe distance calculations

Red Alert 2 uses 256 leptons per cell.

Squared distance calculations can overflow 32-bit integers.

Use 64-bit arithmetic for large coordinate differences.

### 📝 Lua Recipe

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
            local typeName = unit:GetTypeName()

            if typeName == "DRED" or typeName == "DEST" then
                unit:AddSubTurret(1, 40, 0, 15, 12, 90)
                unit:AddSubTurret(2, -40, 0, 15, 12, 90)
            end
        end
    end
end

function MultiTurretMod.Update(frame)
    if frame % 30 ~= 0 then
        return
    end

    local player = House.GetPlayer()

    if not player then
        return
    end

    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive()
            and unit:GetOwner() == player
            and unit:GetTypeName() == "DRED" then

            local pos = unit:GetPosition()
            local targets = {}

            for _, enemy in ipairs(
                World.GetUnitsInRadius(pos.x, pos.y, 15)
            ) do
                if enemy:IsAlive() and enemy:GetOwner() ~= player then
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

return MultiTurretMod
```

---

# 🌐 Case Study 5 — CnCNet Multiplayer & Development Tools

**Status:** ✅ **VERIFIED**  
**Reference implementations:** `spawn_test`, `debug_console`

### 🎯 Outcome

LuaAPI supports CnCNet environments and provides development-oriented tooling for rapid mod testing.

This includes:

- CnCNet process attachment
- Compatibility with existing hooks
- Logical-frame-based callbacks
- Runtime unit spawning
- Debug commands

### ⚙️ CnCNet Architecture

CnCNet can launch `gamemd-spawn.exe` rather than the standard `gamemd.exe`.

The injector therefore resolves the actual game module from the running process instead of assuming a fixed executable name.

### 🔗 Hook Compatibility

LuaAPI may encounter an existing hook at the game's main loop.

A signature mismatch does not necessarily mean that injection failed.

When another modification such as Ares or Phobos has already hooked the function, LuaAPI can chain its hook through MinHook.

### ⏱️ Logical Frame Gating

Gameplay callbacks should be synchronized to the game's logical frame rather than the render rate.

Conceptually:

```text
Render frames
      ↓
 ┌────┴────┐
 ↓    ↓    ↓
120  90    60 FPS
 └────┬────┘
      ↓
Logical game frame
      ↓
 LuaAPI.Update(frame)
```

This prevents gameplay logic from executing multiple times simply because one client renders at a higher FPS.

### 📝 Lua Recipe — Development Spawn

```lua
-- scripts/mods/spawn_test/main.lua

local SpawnTest = {}

function SpawnTest.Update(frame)
    if frame ~= 300 then
        return
    end

    local player = House.GetPlayer()

    if not player then
        return
    end

    local count = player:SpawnUnit(
        "APOC",
        5,
        100,
        100,
        0,
        false,
        "hunt"
    )

    Engine.PrintMessage(
        "Spawned " .. count .. " APOC",
        1
    )
end

return SpawnTest
```

### 🐞 Lua Recipe — Debug Console

`OnDebugCommand` is a global callback.

```lua
-- scripts/mods/debug_console/main.lua

function OnDebugCommand(text)
    local count, typeId =
        text:match("^(%d+)%s+(%u+)$")

    if not count or not typeId then
        return
    end

    local player = House.GetPlayer()

    if not player then
        return
    end

    player:SpawnUnit(
        typeId,
        tonumber(count),
        100,
        100,
        0,
        false,
        "hunt"
    )
end
```

---

# 🔧 Universal Engineering Principles

These principles emerged from the development and debugging of LuaAPI.

### 1. 🧠 C++ manages state, Lua controls gameplay

The C++ layer should provide safe access to the engine and maintain runtime state.

Lua should decide gameplay behavior.

### 2. 🛡️ Validate engine pointers

Engine objects can become invalid after destruction, map transitions, and other lifecycle events.

Never assume a previously obtained object remains valid.

Use the API's validity checks before accessing engine-backed objects.

### 3. 🧹 Defer cleanup

Do not erase objects from containers while iterating over those containers.

Queue removals and process them after iteration.

### 4. 💀 Invalidate destroyed targets

Any system storing references to engine objects must clear references when those objects are destroyed.

### 5. 💾 Test savegame loading

A system that works on a fresh scenario may fail after loading a save.

Do not assume scenario-start callbacks cover the entire lifecycle.

### 6. 🔢 Watch integer overflow

Large squared distances can overflow 32-bit integers.

Use appropriate integer widths for coordinate calculations.

### 7. ⏱️ Use logical frames

Gameplay state changes should be tied to logical game frames rather than render-frame frequency.

### 8. 🔗 Treat hook conflicts carefully

An existing hook or signature mismatch does not automatically indicate an injection failure.

Compatibility with other engine modifications must be evaluated before treating the condition as fatal.

---

# 📌 Verification Policy

This document intentionally distinguishes between implemented functionality and speculative API possibilities.

A capability should only be marked **✅ VERIFIED** when it has been implemented and tested against the current LuaAPI build.

Examples and recipes in this document are intended to reflect the current API contract.

If an API name or behavior changes, this document must be updated together with `API.md` and `TUTORIAL.md`.

---

# 📖 Related Documentation

- [`API.md`](../API.md) — Complete LuaAPI reference
- [`TUTORIAL.md`](../docs/TUTORIAL.md) — Beginner tutorial
- [`README.md`](../README.md) — Project overview and installation
- [`ENGINEERING_LESSONS.md`](../docs/ENGINEERING_LESSONS.md) — Engineering notes and debugging history

---

# 🎓 Development Workflow

LuaAPI development follows a verification-first workflow:

```text
Understand the API
        ↓
Build a small Lua prototype
        ↓
Verify the behavior
        ↓
Move unsafe engine work into C++
        ↓
Expose a safe Lua interface
        ↓
Document the proven capability
```

> Build small. Test frequently. Verify before documenting.
