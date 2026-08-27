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

Case Study 2: Dynamic Bounties, Economy & HUD Feeds

    Outcome: Real-time credit rewards awarded directly to the killer's treasury upon scoring hits or destroying targets, accompanied by standard in-game HUD alerts.

    Why: Eliminates the need for dozens of complex FinalAlert2 triggers to track mission kills and economic rewards.

    Verification Status: [x] VERIFIED (bounty_hunter mod)

🛠️ How It Works (Architecture)

    Subscribe to OnPreDamage or OnUnitDestroyed.

    Extract the attacker's HouseClass* via attacker:GetHouse().

    Safely mutate the economy balance via house_AddCredits(house, amount).

    Output real-time notifications to the player's message ticker via game_PrintMessage(text, color).
```
⚠️ Hard Lessons Learned:
Null Attacker Traps: When units die from map triggers, crushing, or environmental hazards, attacker can be nil. Always guard with if attacker and attacker:GetHouse() then before accessing house methods to avoid script aborts.
📝 Lua Recipe:
```
code Lua

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

⚠️ Hard Lessons Learned (The Critical Crashes We Solved):
    VTable Dereference on Death (0xC0000005):
    When Turret 1 destroys an enemy, the target's destructor destroys its virtual method table (_vptr). If Turret 2 accesses target->WhatAmI() in the same frame, the CPU crashes on null vtable reading.
    Solution: Guard virtual calls via SEH __try / __except, check DamageState::NowDead, and call InvalidateTargetGlobally() to immediately clear the dead target across all turrets.
    Leptons vs. Cells Coordinate Trap:
    In RA2, 
    1 cell=256 leptons
    1 cell=256 leptons
    . Passing cell numbers
    (50,50)
    (50,50)
    to lepton distance formulas scans the top-left map corner. Always convert coordinates consistently.
    InLimbo / Transport Explosion Bug:
When tanks enter amphibious transports, their state switches to InLimbo = true. If combat scripts damage units in limbo, the explosion triggers inside the transport's cargo, destroying both. Always check !unit:IsInLimbo().

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
