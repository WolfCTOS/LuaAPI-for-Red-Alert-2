# 🧠 LuaAPI for Red Alert 2: Yuri's Revenge — AI Context & State

> **Last Updated:** August 27, 2026  
> **Target Executable:** `gamemd.exe` (Yuri's Revenge 1.001)  
> **Repository:** https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2  
> **Current Version:** `v1.0.0 Production Release` + `Milestone 10 Core (In Progress)`

---

## 📍 Current Architectural State

### 1. Core Engine & Safety (Milestones 1–9: [x] DONE / VERIFIED)
- **MainLoop Hook:** MinHook at `0x55D360` with lock-free QPC ring-buffer (< 0.01 FPS overhead, 60 FPS lock).
- **Pointer & RTTI Safety:** `ValidateTechno()` checks RTTI `WhatAmI()` and lifecycle flags (`IsAlive`, `Health > 0`, `!InLimbo`), preventing `0xC0000005` Access Violations on dead units.
- **Session Lifecycle:** `ResetSession()` cleanly clears callback references and Lua state on map load/restart/exit.
- **Multiplayer Determinism:** Frame-seeded RNG (seed `12345`) preventing OOS in CnCNet (`--withcncnet` headless spawner).
- **GUI Injector:** Win32 dark theme, High-DPI aware, async launch thread, Unicode `LoadLibraryW` support, vector GDI checkboxes.

### 2. Advanced Combat & Multi-Turret (Milestone 10: In Progress)
- **SubTurretManager:** C++ memory sidecar attached to `TechnoClass*`.
- **Dreadnought Missile Decoupling:** `ProcessSpawnedMissiles` intercepts `node1->Status == SpawnNodeStatus::TakeOff`, sets `pMissile->SpawnOwner = nullptr; node1->Status = SpawnNodeStatus::Dead;` and redirects destination via `Locomotor->Force_Immediate_Destination(targetCoords)`.
- **Safe Combat Execution:** SEH `__try / __except` around virtual methods, `DamageState::NowDead` bailing, and `InvalidateTargetGlobally()` to eliminate dangling pointers.
- **Lua Bindings:** `World.GetAllUnits()`, `unit:AddSubTurret()`, `unit:SetSplitTargets()`, `unit:FireSplitSalvo()`.

---

## 📁 Key Documentation Files
- `README.md` — Project overview, QuickStart, and v1.0 release info.
- `API.md` — Authoritative reference manual of all Lua functions and event signatures.
- `BENCHMARK.md` — Intel PresentMon hardware benchmark report (17,993 frames / 56.24 1% Low FPS).
- `CAPABILITIES.md` — Modder's cookbook with 4 verified Case Studies and comprehensive post-mortems of engine traps.
- `PROJECT/ROADMAP.md` — Complete 10-milestone architecture roadmap with `Why:` rationale.

---

## 🛠️ Build & Dev Commands
- **Compiler:** MSVC C++17 Win32 (x86) via CMake.
- **Build Command:** `cmake --build build --config Release`
- **Output Artifacts:** `build/Release/LuaAPI.dll` and `build/Release/injector.exe` (auto-deployed to game folder).
