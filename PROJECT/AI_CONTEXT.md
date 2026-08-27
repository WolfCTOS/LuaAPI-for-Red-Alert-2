# 🧠 LuaAPI for Red Alert 2: Yuri's Revenge — Engineering Context & Memory Log

> **Target Platform:** `gamemd.exe` (Yuri's Revenge 1.001)  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current Version:** `v1.0.0 Production Release` + `Milestone 10 Core (In Progress)`  
> **Last Updated:** August 2026

---

## 📌 Executive Summary for the Incoming AI Technical Director

LuaAPI is an ultra-low overhead (< 0.01 FPS delta), crash-resilient native x86 Lua 5.4 scripting runtime embedded into `gamemd.exe` at canonical game loop hook `0x55D360`.

- **Milestones 1–9:** `[x] 100% DONE / VERIFIED` (v1.0.0 Production Release on GitHub Releases).
- **Milestone 10 (Advanced Combat & Multi-Turret / Split-Salvo):** `[ ] IN PROGRESS`. Core memory containers, targeting math, and physical `DMISL` missile decoupling are implemented and verified.

---

## 💀 The Hall of Hard-Learned Lessons (The 7 Critical Engine Traps We Solved)

When working on this codebase, **DO NOT REPEAT THESE MISTAKES**. Every one of these traps cost hours of debugging in assembly and memory dumps:

### 1. The 32-Bit Lepton Integer Overflow Trap
- **The Trap:** In RA2, 1 cell = 256 leptons. Calling a spatial search with radius 500 cells results in $500 \times 256 = 128,000$ leptons. Squaring it for euclidean distance ($dx^2 + dy^2$) results in **$16,384,000,000$ (16.38 billion)**.
- **The Crash/Bug:** Maximum 32-bit signed `int` in x86 is **$2.147$ billion**. 16.38B overflows to **`-795,869,184`**. Any check `distSq <= radiusSq` becomes `distSq <= negative`, returning **ZERO units on the entire map**.
- **The Rule:** Always keep spatial search radii $\le 30$ cells, or use 64-bit integers (`int64_t`) for distance squared formulas. Use `World.GetAllUnits()` to iterate the global array directly without coordinate guessing.

### 2. The VTable Destruction on Dead Units (`0xC0000005`)
- **The Trap:** When unit A destroys unit B, unit B's C++ destructor immediately zeroes or frees the virtual method table pointer (`_vptr`).
- **The Crash:** If another turret or script calls `unitB->WhatAmI()` on the same tick, the CPU jumps to `0x00000000`, causing an unrecoverable `STATUS_ACCESS_VIOLATION (0xC0000005)`.
- **The Rule:** Wrap all virtual object methods in SEH `__try / __except (EXCEPTION_EXECUTE_HANDLER)`. Check `DamageState::NowDead` upon `ReceiveDamage` and immediately bail out. Use `InvalidateTargetGlobally(pDeadTarget)` to clear the dead pointer across all turrets in that exact millisecond.

### 3. Hash Map Iterator Invalidation in Combat Loops
- **The Trap:** Iterating `m_turrets` (`std::unordered_map`) while calling a combat damage function that kills a unit.
- **The Crash:** Unit death triggers `OnUnitDestroyed` -> calls `m_turrets.erase(victim)` -> active iterator `it` in `UpdateAll()` is invalidated -> next `++it` reads corrupted heap -> instant crash.
- **The Rule:** Use the **Deferred Cleanup Pattern (`m_pendingRemovals`)**: while `m_isUpdating == true`, queue dead units into a vector and erase them strictly outside the loop. Iterate over copied keys (`activeUnits`).

### 4. The 25-Year `SpawnManagerClass` Umbilical Cord Lock
- **The Trap:** Trying to steer spawned guided missiles (`DMISL` on Dreadnoughts, `HORNET` on Carriers) by calling `pMissile->SetTarget(newTarget)` in mid-air.
- **The Bug:** In Westwood's assembly (`SpawnManagerClass::AI()`), the engine forces all spawned children back to `Owner->Target` **every single frame (every 16 ms)**. It overwrites any target change.
- **The True Fix:** You must decouple the child from the parent manager:
  1. `pMissile->SpawnOwner = nullptr;` (breaks the parent leash).
  2. `node1->Unit = nullptr;` & `node1->Status = SpawnNodeStatus::Dead;` (allows the tube to regenerate).
  3. Force 3D trajectory recalculation via the native COM locomotor interface: `static_cast<FootClass*>(pMissile)->Locomotor->Force_Immediate_Destination(targetCoords);` (at `0x55AC00`).

### 5. C++ Auto-Fire Aura vs. Passive Engine Architecture
- **The Trap:** Writing auto-target scanning and `FireTurret()` inside C++ `SubTurretManager::UpdateAll()`.
- **The Bug:** In RA2, idle units are internally in `Mission::Guard`. The C++ code treated idle ships as active combat guards, creating an invisible, uncontrollable "death aura" that destroyed bases passively without player commands.
- **The Rule:** C++ `SubTurretManager::UpdateAll()` must be **100% PASSIVE** (only decrement `rofTimer--` and step rotation `facing -> targetFacing`). Firing and targeting decisions must be strictly driven by Lua or player commands.

### 6. The Savegame Lifecycle Blindspot (`.sav`)
- **The Trap:** Attaching sub-turrets or initializing data exclusively inside `OnScenarioStart()`.
- **The Bug:** When a player loads a saved game (`.sav`), `OnScenarioStart()` **NEVER FIRES** (it only runs on frame 1 of a fresh match). Existing units on loaded saves had 0 sub-turrets.
- **The Rule:** In `main.lua` `Update()`, dynamically check `if unit:GetSubTurretCount() == 0 then unit:AddSubTurret(...)` so units from loaded saves or factories are equipped on the fly.

### 7. Binding Identifier Synchronization
- **The Trap:** Writing Lua scripts with assumed names (`unit:GetHouse()`, `unit:GetType()`).
- **The Bug:** Real exports in `bindings_techno.cpp` are `unit:GetOwner()` (returns House, compare via `owner:GetName() == player:GetName()`) and `unit:GetTypeName()` (returns string ID like `"DRED"`). Unmatched calls return `nil` and silently break scripts.

---

## 🏛️ Current Working Sub-Systems

1. **`src/sub_turret.h` & `src/sub_turret.cpp`**:
   - `SubTurretManager` singleton.
   - `AddTurret`, `GetTurrets`, `RemoveTechno`, `InvalidateTargetGlobally`, `ClearAll`.
   - `UpdateAll`: Passive timer decrements + `SafeComputeAim` 2D `atan2` angle rotation.
   - `ProcessSpawnedMissiles`: Point-of-launch TakeOff interception with `Force_Immediate_Destination`.
   - `AssignSplitTargets` & `FireSplitSalvo`.
2. **`src/bindings_techno.cpp`**:
   - `World.GetAllUnits()`, `World.GetUnitsInRadius()`, `World.GetWaypoint()`.
   - `Techno` methods: `GetOwner`, `GetTypeName`, `GetCoords`, `IsAlive`, `IsInLimbo`, `GetHealthRatio`, `SetHealthRatio`, `AttachParticleSystem`, `AddSubTurret`, `GetSubTurretCount`, `SetSplitTargets`, `FireSplitSalvo`.
   - `House` methods: `GetPlayer`, `GetCredits`, `AddCredits`.
   - `Engine` methods: `PrintMessage`.
3. **`scripts/mods/multi_turret_battleship/`**:
   - Active showcase mod for Milestone 10.

---

## 🎯 Next Immediate Engineering Goals (Milestone 10 & 11)

1. **Gate 10.3 Polish:** Visual 3D Voxel matrix sub-turret rotation hook (`TechnoClass::Draw` / `VoxelClass::Draw_Matrix`).
2. **Z-Mode Non-Linear Waypoint Interception:** Intercepting `PlanningModeClass` (Z-key) command buffer to route targets to `SetSplitTargets`.
3. **Dynamic Salvo Convergence:** Automatic fallback where destroying Target A causes subsequent missile salvos to double-down on surviving Target B.
4. **Milestone 11 (Tactical AI & Naval Intelligence):** `CellClass` water sampling, island detection, and dynamic build queue overrides for naval maps.
### 8. The RocketLocomotor Pre-Computed Ballistic Spline Trap
- **The Trap:** Attempting to steer an in-flight \DMISL\ missile by changing \pMissile->Target\, \SetDestination\, or calling \NextMission()\ during flight.
- **The Reality:** \RocketLocomotor\ computes a rigid 3D Bezier curve \(t)\$ at the instant of initialization. During flight, it does not re-evaluate navigation targets.
- **The Rule:** Target B must be bound to Node 1 *before* the trajectory spline is instantiated at launch, or dispatched via a native ballistic \BulletClass\ projectile.
