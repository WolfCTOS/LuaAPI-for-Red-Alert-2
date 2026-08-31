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
5. [Case Study 5: CnCNet Multiplayer Compatibility & Deterministic Injection](#case-study-5-cncnet-multiplayer-compatibility--deterministic-injection)

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
