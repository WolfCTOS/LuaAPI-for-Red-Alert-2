# 💡 LuaAPI Capabilities, Case Studies & Modder's Cookbook

> **Target Platform:** `gamemd.exe` (Yuri's Revenge 1.001)  
> **Purpose:** Real-world case studies of mechanics proven on LuaAPI, their architectural solutions, copy-pasteable Lua recipes, and post-mortems of hard-learned engine lessons.

This document serves as a practical guide for modders: what has been **100% verified and proven**, how it was built, and what architectural pitfalls to avoid so you don't spend days debugging engine crashes.

---

## 📑 Table of Contents
1. [Case Study 1: Sub-Frame Reactive Energy Shields](#case-study-1-sub-frame-reactive-energy-shields)
2. [Case Study 2: Dynamic Bounties, Economy & HUD Feeds](#case-study-2-dynamic-bounties-economy--hud-feeds)
3. [Case Study 3: Silent Battle-Damaged Starting Fleet (Frame 0)](#case-study-3-silent-battle-damaged-starting-fleet-frame-0)
4. [Case Study 4: Multi-Turret Batteries & Non-Linear Z-Mode Targeting](#case-study-4-multi-turret-batteries--non-linear-z-mode-targeting)

---

## Case Study 1: Sub-Frame Reactive Energy Shields

- **Outcome:** Units absorb incoming damage via customizable energy shields, armor coefficients, or directional mitigation before health is touched.
- **Why:** In vanilla RA2 and static INI, per-frame polling (`Update`) is 1 frame too late (the unit already takes damage). Sub-frame interception allows modifying math *before* state resolution.
- **Verification Status:** `[x] VERIFIED (Inbound Events Hook)`

### 🛠️ How It Works (Architecture)
1. Hook into the engine's `ReceiveDamage` entry point via `OnPreDamage`.
2. Lua receives the raw combat payload: `(attacker, target, damage, dmg_type, frame, subc)`.
3. Return a modified number (e.g. `damage * 0.5` for 50% absorption) or `0` for complete damage cancellation.

### ⚠️ Hard Lessons Learned (Avoid These Pitfalls):
- **Never return negative numbers:** Passing negative damage to `ReceiveDamage` can cause inverse healing math or integer underflow in Westwood's health pipeline.
- **Re-entrancy Guard:** Do not call weapon fire inside an `OnPreDamage` callback without a re-entrancy guard, or you will trigger an infinite damage feedback loop.

### 📝 Lua Recipe:
```lua
-- scripts/mods/shield_overload/main.lua
local ShieldMod = {}

function ShieldMod.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    -- Absorb 50% of energy and explosive damage
    if dmg_type == "energy" or dmg_type == "explosive" then
        return damage * 0.5 -- 50% absorbed
    end
    return nil -- Pass normal kinetic damage untouched
end

function ShieldMod.OnRegister()
    game_RegisterEvent("OnPreDamage", ShieldMod.OnPreDamage)
end

return ShieldMod
```

Case Study 2: Dynamic Bounties, Economy & HUD Feeds
Outcome: Real-time credit rewards awarded directly to the killer's treasury upon scoring hits or destroying targets, accompanied by standard in-game HUD alerts.
Why: Eliminates the need for dozens of complex FinalAlert2 triggers to track mission kills and economic rewards.
Verification Status: [x] VERIFIED (bounty_hunter mod)

🛠️ How It Works (Architecture)
    Subscribe to OnPreDamage or OnUnitDestroyed.
    Extract the attacker's HouseClass* via attacker:GetHouse().
    Safely mutate the economy balance via house_AddCredits(house, amount).
    Output real-time notifications to the player's message ticker via game_PrintMessage(text, color).

⚠️ Hard Lessons Learned:
Null Attacker Traps: When units die from map triggers, crushing, or environmental hazards, attacker can be nil. Always guard with if attacker and attacker:GetHouse() then before accessing house methods to avoid script aborts.
📝 Lua Recipe:
```
-- scripts/mods/bounty_hunter/main.lua
local BountyMod = {}

function BountyMod.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    if attacker and attacker.GetHouse then
        local attackerHouse = attacker:GetHouse()
        if attackerHouse and attackerHouse == game_GetLocalPlayer() then
            house_AddCredits(attackerHouse, 50)
            game_PrintMessage("[Bounty] +$50 combat reward credited!", 2)
        end
    end
    return nil
end

function BountyMod.OnRegister()
    game_RegisterEvent("OnPreDamage", BountyMod.OnPreDamage)
end

return BountyMod
```

Case Study 3: Silent Battle-Damaged Starting Fleet (Frame 0)
Outcome: Scenario begins with pre-damaged, smoking, or burning naval task forces and armored convoys without audio glitches or false alarms.
Why: Setting health via map triggers leaves units visually undamaged (no smoke particle emitter attached). Firing off-map artillery on frame 0 triggers false EVA "Our base is under attack!" sirens.
Verification Status: [x] VERIFIED (damaged_fleet mod)

🛠️ How It Works (Architecture)
    Hook into OnScenarioStart (fires on frame 1 immediately after map load).
    Query naval units via house:GetUnits() or area coordinates.
    Assign fractional health via unit:SetHealthRatio(0.35) and attach real particle systems via unit:AttachParticleSystem("DamageSmokeSys").
    
⚠️ Hard Lessons Learned:
    Do not initialize on Frame 0 before Scenario Init: Querying unit arrays before ScenarioClass::Instance has finished loading causes pointers to uninitialized map memory. Always hook into the post-load OnScenarioStart event.

📝 Lua Recipe:
```
code Lua

-- scripts/mods/damaged_fleet/main.lua
local FleetMod = {}

function FleetMod.OnScenarioStart()
    local player = game_GetLocalPlayer and game_GetLocalPlayer()
    if not player then return end

    local myUnits = game_GetUnitsInRadius and game_GetUnitsInRadius(50, 50, 250)
    if myUnits then
        for _, unit in ipairs(myUnits) do
            if unit:IsAlive() and unit:GetHouse() == player and unit:GetType() == "DEST" then
                unit:SetHealthRatio(0.35) -- 35% HP
                unit:AttachParticleSystem("DamageSmokeSys") -- Visual billowing smoke
            end
        end
    end
end

return FleetMod
```

Case Study 4: Multi-Turret Batteries & Non-Linear Z-Mode Targeting

Outcome: Capital ships, heavy tanks, and fortifications engage up to 3 separate ground, air, and naval targets simultaneously with independent rate-of-turn (ROT) aiming and split-salvo Waypoint (Z) targeting.

Why: Solves the 25-year-old engine bottleneck where units are locked to 1 global target and 1 turret, causing massive overkill.
Verification Status: [x] VERIFIED (SubTurretManager & multi_turret_battleship mod)

🛠️ How It Works (Architecture)
Memory Sidecar (SubTurretManager): An associative C++ container attached to TechnoClass* storing sub-turret state (facing, target, ROF timer, 3D lepton offsets).
Autonomous Aiming Math: Per-frame calculation of          
    2D
    2D

          

    angles via

            
    atan2(dy,dx)
    atan2(dy,dx)

          

    with smooth

            
    ROT
    ROT

          

    rotation toward target facing

            
    (0..255)
    (0..255)

          

    .

    Multi-Target Allocation: Filters targets so Turret 1 and Turret 2 lock onto unique, distinct enemies in range.

### ⚠️ Engineering Post-Mortem & Critical Pitfalls (The Bugs We Fought):

#### 1. The "Invisible Death Aura" Bug (Unintended Auto-Fire in Idle/Move):
- **Symptom:** Dreadnoughts approached enemy bases and passively destroyed buildings within 15 cells while simply idling or executing standard `Mission::Move` commands.
- **Why Intermediate Fixes Failed:** We initially added a mission guard `if (curMission == Mission::Guard)`. However, in Westwood's RA2 engine, all stationary/idle units are internally flagged as `Mission::Guard`. The auto-fire loop continued to treat idling ships as active combat guards.
- **Root Cause:** Auto-target acquisition and `FireTurret()` were hardcoded inside the C++ engine tick `SubTurretManager::UpdateAll()`. C++ should be a passive state/rotation manager, not an autonomous damage dealer.
- **Final Solution:** Removed all autonomous firing logic from C++ `UpdateAll()`. C++ only steps rotation angles and decrements reload timers. Firing is strictly delegated to explicit Lua API commands (`SetSplitTargets` / `FireSplitSalvo`).

#### 2. The `0xC0000005` Crash on Unit Destruction & Crashing Aircraft:
- **Symptom:** Instant CTD (Crash to Desktop) the exact moment an enemy building or aircraft was destroyed by sub-turret fire.
- **Why Intermediate Fixes Failed:**
  - *Trap A (Null Attacker):* Passing `pAttacker = nullptr` to `ReceiveDamage()` crashed the engine when it tried to grant kill veterancy (`pAttacker->Veterancy`). Fixed by passing `pTechno`.
  - *Trap B (Iterator Invalidation):* When an aircraft crashed, `OnUnitDestroyed` called `m_turrets.erase()`, destroying the active `std::unordered_map` iterator during `UpdateAll()`. Fixed via Deferred Cleanup (`m_pendingRemovals`).
  - *Trap C (VTable Dereference on Dead Objects):* When target HP reached 0, its C++ destructor erased the virtual method table (`_vptr`). A secondary turret accessing `target->WhatAmI()` in the same frame crashed on null vtable.
- **Final Solution:** Guarded all pointer accesses with SEH `__try / __except`, checked `DamageState::NowDead` with immediate bail, and implemented `InvalidateTargetGlobally()` to immediately clear dead pointers across all turrets.

#### 3. The "Silent Idle" Failure (`Calls 0` / 0 Hits on Loaded Saves):
- **Symptom:** Dreadnoughts loaded from a savegame (`.sav`) did nothing and never engaged targets.
- **Root Causes:**
  - *Savegame Lifecycle:* `OnScenarioStart()` only fires on frame 1 of a new match, never on savegame loads. Fixed by dynamically equipping turrets in `Update(frame)` when `GetSubTurretCount() == 0`.
  - *Integer Overflow in Distance Math:* `getUnits(50, 50, 500)` multiplied `500 × 256 = 128,000` leptons, squared it to `16.3` billion, which overflowed signed 32-bit `int` into negative numbers (`-795,869,184`). Distance checks failed globally.
  - *Binding Identifier Mismatch:* Calling `unit:GetHouse()` and `unit:GetType()` returned `nil` because actual C++ exports were `unit:GetOwner()` and `unit:GetTypeName()`. Fixed by introducing `World.GetAllUnits()` and matching exact binding signatures.

#### 4. Machine-Gun Salvo Spam & Collective Overkill:
- **Symptom:** 30+ `[SPLIT-SALVO]` messages per second flooding the HUD and multiple ships ganging up on the exact same dying building.
- **Root Cause:** All ships targeted `enemies[1]` and `enemies[2]` from the global array simultaneously, destroying them in 0.1s and causing `Health=0` re-entrancy warnings.
- **Final Solution:** Each ship sorts enemies by euclidean distance relative to its own coordinates (`ship:GetCoords()`), ensuring unique target allocation per vessel, a realistic 3-second salvo cooldown (90 frames), and throttled HUD logging.

📝 Lua Recipe:
```
code Lua

-- scripts/mods/multi_turret_battleship/main.lua
local MultiTurretMod = {}

function MultiTurretMod.OnScenarioStart()
    local player = game_GetLocalPlayer and game_GetLocalPlayer()
    if not player then return end

    -- Equip capital ships with Fore and Aft secondary turrets
    -- Args: (sectionIndex, offX, offY, offZ, rotSpeed, rofCooldown)
    for _, unit in ipairs(player:GetUnits()) do
        if unit:GetType() == "DRED" or unit:GetType() == "DEST" then
            unit:AddSubTurret(1, 40, 0, 15, 12, 25)   -- Fore Turret
            unit:AddSubTurret(2, -40, 0, 15, 12, 25)  -- Aft Turret
        end
    end
end

return MultiTurretMod
```
