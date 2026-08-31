# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Architecture Roadmap

## 📍 Project Lifecycle Overview

- **Phase 1: Proof of Concept & MVP (Milestones 1–5)** -> [x] DONE / VERIFIED
- **Phase 2: Alpha-1 - Core Lifecycle & Safety (Milestone 6)** -> [x] DONE / VERIFIED
- **Phase 3: Alpha-2 - Spatial Queries & Extended Events (Milestone 7)** -> [x] DONE / VERIFIED
- **Phase 4: Beta - Feature Freeze & Stress Hardening (Milestone 8)** -> [x] DONE / VERIFIED
- **Phase 5: Production Release v1.0 (Milestone 9)** -> [x] DONE / VERIFIED
- **Phase 6: Multi-Turret & Advanced Combat Systems (Milestone 10)** -> [x] DONE / VERIFIED

---

## 🏆 Detailed Milestones, Gates & Architectural Rationale

---

### [x] Milestone 1–3: Core Engine Hooking & Runtime Sandbox (MVP)

> **Why Milestone 1–3:** Before implementing any gameplay mechanics, we had to prove the technical feasibility of embedding a modern Lua 5.4 runtime into the closed-source, 32-bit `gamemd.exe` binary (2001) without framerate degradation or corrupting the original assembly loop.

- [x] **Gate 1.1: Hook MainLoop at `0x55D360`**
  - *Why:* Address `0x55D360` is the canonical per-frame tick of the engine. If this hook is slow or unstable, the entire platform fails. Zero overhead was proven.
- [x] **Gate 1.2: Lua 5.4 VM with isolated `pcall` execution**
  - *Why:* In native C++, any script typo crashes the process. `pcall` sandboxing guarantees script errors are logged while `gamemd.exe` continues running.
- [x] **Gate 1.3: Basic TechnoClass Property Manipulation**
  - *Why:* Validates the two-way memory bridge — safe reading and mutation of native engine objects from Lua.

---

### [x] Milestone 4: Inbound Events (Sub-Frame Reactive Control)

> **Why Milestone 4:** Per-frame polling (`Update(frame)`) is inherently 1 frame too late. Reactive inbound events allow intercepting and modifying combat math *before* the engine applies damage, enabling true energy shields and custom armor.

- [x] **Gate 4.1: `OnPreDamage` Engine Interception Hook**
  - *Why:* Intercepts the damage calculation entry point (`ReceiveDamage`), enabling scripts to modify combat math in real time.
- [x] **Gate 4.2: Damage Modification & Cancellation Pipeline**
  - *Why:* Allows scripts to return a modified damage value (e.g. absorb 50%) or `0` (full immunity) without touching INI files.
- [x] **Gate 4.3: End-to-End Validation via `shield_overload`**
  - *Why:* Practical proof that damage absorption math works under authentic combat projectile impacts.

---

### [x] Milestone 5: Multiplayer Determinism & External Benchmarking

> **Why Milestone 5:** Yuri's Revenge netcode (CnCNet) operates on a strict Lockstep model. Any discrepancy in random number generation or thread delays causes an immediate Out-of-Sync (OOS) crash.

- [x] **Gate 5.1: Headless CnCNet Spawner Mode (`--withcncnet`)**
  - *Why:* Allows the injector to passively attach to `gamemd.exe` spawned by `YRLauncher.exe` without tampering with command-line arguments or network sockets.
- [x] **Gate 5.2: Synchronized Frame-Seeded RNG (Seed 12345)**
  - *Why:* Replacing unstable `os.clock()` / `os.time()` with frame-based seeds guarantees 100% identical random sequences across all client PCs.
- [x] **Gate 5.3: Hardware ETW Benchmark via Intel PresentMon**
  - *Why:* Eliminates subjective bias. Hardware ETW capture proved a benchmark of **60.04 Avg FPS / 55.11 1% Lows** (0.00% overhead).

---

### [x] Milestone 6: Alpha-1 — Lifecycle Hardening & RTTI Safety

> **Why Milestone 6:** The primary curse of C&C modding for 20 years has been `0xC0000005` (Access Violation) crashes on destroyed objects. This milestone transformed a basic injector into a hardened, crash-resilient SDK.

- [x] **Gate 6.1: `ResetSession()` on Map Load, Restart, and Exit**
  - *Why:* Cleans Lua callback registries and state on mission restarts, preventing memory leaks and stale references to freed session objects.
- [x] **Gate 6.2: `ValidateTechno()` RTTI (`WhatAmI()`) & Lifecycle Flags**
  - *Why:* Validates memory pool objects before passing them to Lua. If an object is dead, it returns `nil, error` instead of crashing the game.
- [x] **Gate 6.3: Core Economy & HUD Message APIs**
  - *Why:* Gives mod authors essential gameplay verbs (`house_AddCredits`, `game_PrintMessage`) without requiring low-level memory hacks.
- [x] **Gate 6.4: Validation via `bounty_hunter` & `v0.1.0-alpha` Release**
  - *Why:* Provided the first functional binary package for external community testers.

---

### [x] Milestone 7: Alpha-2 — Spatial Map API & Extended Events

> **Why Milestone 7:** Direct response to modder feedback. Eliminates 20-year-old FinalAlert2 trigger workarounds (invisible artillery hacks for smoke, dummy buildings on waypoints).

- [x] **Gate 7.1: Scenario Start Hook (`OnScenarioStart`)**
  - *Why:* First frame after map load. Enables clean, silent setup of damaged starting fleets and cutscenes without trigger glitches or false EVA alarms.
- [x] **Gate 7.2: Destruction Event Hook (`OnUnitDestroyed`)**
  - *Why:* Foundation for bounty systems, evacuation scripts, boss phases, and victory/defeat triggers.
- [x] **Gate 7.3: Spatial Queries (`game_GetWaypoint`, `game_GetUnitsInRadius`)**
  - *Why:* Allows binding logic to map waypoints and scanning radius areas without hardcoding raw pixel coordinates.
- [x] **Gate 7.4: Showcase Validation via `damaged_fleet`**
  - *Why:* Demonstrates instant 35% HP assignment and attaching `DamageSmokeSys` particles on mission start.

---

### [x] Milestone 8: Beta — Feature Freeze & Hardening

> **Why Milestone 8:** Transition from feature additions to industrial stability. Proves engine resilience under extreme load and establishes comprehensive developer documentation.

- [x] **Gate 8.1: Long-Run Combat Stress Test (18k Frames / 5-min Battle)**
  - *Why:* Proves zero memory creep and no GC degradation under heavy load. Passed: 17,993 frames, 7 Brutal AIs, **56.24 FPS 1% Low**, 0 crashes.
- [x] **Gate 8.2: Comprehensive API Reference Manual (`API.md`)**
  - *Why:* External modders need an authoritative manual with complete function signatures, argument types, and code snippets.
- [x] **Gate 8.3: CnCNet ModBase Native Integration**
  - *Why:* Enables standalone modpacks (DTA, Mental Omega style) to load `LuaAPI.dll` headlessly with automatic process termination.
- [x] **Gate 8.4: Feature Freeze & API Stabilization**
  - *Why:* Locks all existing Lua function signatures to guarantee backward compatibility for all future community mods.

---

### [x] Milestone 9: Production Release v1.0

> **Why Milestone 9:** Delivering a polished, production-grade SDK to the global C&C community with verified binaries, tools, and modder showcases.

- [x] **Gate 9.1: Public Release Package (`v1.0.0` Stable ZIP)**
  - *Why:* Publishes a pre-compiled binary package with the GUI launcher and sample mods on GitHub Releases.
- [x] **Gate 9.2: Community Showcase Announcement (Haven & PPM)**
  - *Why:* Official announcement presenting the platform, the `0xC0000005` crash prevention architecture, and hardware performance benchmarks.

---

### [ ] Milestone 10: Multi-Turret & Advanced Combat Systems (v1.1) (CURRENT)

> **Why Milestone 10:** In vanilla RA2/YR, `TechnoClass` is hardcoded to a single primary turret and one target. This milestone breaks that 25-year limitation, enabling capital ships and heavy combat vehicles to engage multiple ground, air, and naval targets simultaneously with independent 3D voxel turret rotations.

- [x] **Gate 10.1: Sub-Turret Memory Model & State Lifecycle**
  - *Why:* A safe C++ sidecar container attached to `TechnoClass*` storing sub-turret state (facing, target, ROF timer, weapon type, 3D offset) with automatic cleanup on unit destruction.
- [x] **Gate 10.2: Independent Targeting & Combat Dispatch**
  - *Why:* Enables per-turret/per-slot target acquisition (AG vs AA vs AS) with smooth Rate-of-Turn (ROT) angle stepping and native `BulletClass::Create` projectile spawning.
- [ ] **Gate 10.3: Voxel Matrix Rendering Hook (3D Sub-Turret Drawing)** — DEFERRED to Milestone 11
  - *Why:* Hooks into `TechnoClass::Draw` to compute `Matrix3D` transformations for each sub-turret, rendering distinct 3D voxel sections rotated towards their specific targets during hull movement. Carried over to Milestone 11 (v1.1 presentation relies on tracers, not rotating voxels).
- [x] **Gate 10.4: Lua Multi-Turret API & Showcase Mod (`multi_turret_battleship`)**
  - *Why:* Exposes high-level bindings (`unit:AddSubTurret`, `unit:SetSubTurretTarget`, `unit:FireSubTurret`) and validates a 3-turret capital ship engaging multiple targets in real combat.