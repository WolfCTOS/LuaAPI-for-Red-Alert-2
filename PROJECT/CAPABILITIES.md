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

---

# 🛡️ Case Study 1 — Sub-Frame Reactive Energy Shields

**Status:** ✅ **VERIFIED**  
**Reference implementation:** `shield_overload`

### 🎯 Outcome

LuaAPI can intercept incoming damage **before the engine applies it to the target**.

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

The flow is:

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
Original Modified Cancel
damage   damage  damage
