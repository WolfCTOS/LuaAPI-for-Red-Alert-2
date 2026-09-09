# AGENTS.md

LuaAPI: a native x86 Lua 5.4 runtime injected into Yuri's Revenge 1.001 (`gamemd.exe`). C++ handles engine integration/safety; Lua controls gameplay. Read `docs/AGENTS.md`, `docs/ENGINEERING_LESSONS.md`, and `API.md` before touching hooks or combat.

## Repo layout gotcha

- **The repo root IS a full Yuri's Revenge 1.001 install** (`gamemd.exe`, `.mix`, `.mmx`, etc.). Those game assets are gitignored; don't confuse them with source. Only `src/`, `include/`, `scripts/`, `docs/`, `PROJECT/`, `tools/`, `third_party/` are code.
- The CMake build **auto-deploys `LuaAPI.dll` + `injector.exe` to the repo root** (the game dir). A fresh build overwrites the DLL/injector already in the game folder.

## Build

- **MSVC only** (works out-of-the-box; no GCC/Clang). Configured `build/` uses VS generator, `Win32` (32-bit x86), C++20, static `/MT`. Build: `cmake --build build --config Release`.
- **Init git submodules first** — `git submodule update --init --recursive` (YRpp, lua, sol2, spdlog, minhook).
- **MinHook IS used and linked** (`lua_engine.cpp`, `event_hook.cpp`, `sub_turret.cpp` call `MH_*`). Ignore the stale "no longer linked, retained for reference" comment in `CMakeLists.txt`.
- **No unit test framework.** Verification is in-game: run `injector.exe`, launch Yuri's Revenge, read `LuaAPI.log` (written next to the DLL = repo root; 5 MB x 3 rotating, deleted fresh each run). Trace logs: set `LUAAPI_TRACE=1` env (or define `LUA_TRACE_FRAMES`); default level is `info`.
- Do not commit the generated binaries/DLL/injector — they're gitignored.

## Run / iterate

- **Lua changes are free** — scripts reload on match start (no rebuild). **C++ changes need rebuild + relaunch game.**
- `injector.exe` must attach to **`gamemd.exe`**. `RA2MD.exe` is a launcher stub — targeting it yields `VirtualProtect` error 487. CnCNet launches `gamemd-spawn.exe` instead. Injection is async with a 5 s timeout.

## Hard MSVC constraint (SEH / C2712)

- A function containing `__try/__except` cannot also have C++ objects with destructors → compile error **C2712**. Keep `__try` in tiny helper functions and never let a C++ exception cross an SEH frame (see `logger.hpp` `FlushImpl`, `event_hook.cpp`).
- **Validate before deref**: `nullptr`, `WhatAmI()` RTTI (Building/Unit/Infantry/Aircraft), `IsAlive`, `Health > 0`, `InLimbo`, plus SEH wrapping. The engine frees objects aggressively — cached pointers die within a few frames.

## Hooks & combat traps

- `FootClass::Active_Click_With` @ `0x004D74E0` is **hard-disabled** (`kDisableActiveClickHook = true` in `src/event_hook.cpp`). The detour crashes on ANY player attack order against a TechnoClass. Do not re-enable without a new interception point. Full story: `docs/ENGINEERING_LESSONS.md` §9.
- Verified addresses (gamemd 1.001): MainLoop `0x0055D360`, `StringTable::LoadString` `0x00734E60` (__fastcall). Byte-signature checks are **non-blocking** (Gate 11.1): a mismatch is only a WARN; the hook still installs unless `MH_CreateHook` itself fails.
- SpawnManager rewrites spawned-missile targets each frame → `SpawnOwner=nullptr` before redirect. RocketLocomotor precomputes the ballistic spline at spawn → use `Force_Immediate_Destination` for in-flight redirect.

## Multiplayer determinism

- MainLoop can run several times per logical frame. Gameplay logic (`EventHook::Update`, Lua `OnTick`/`Update`) is gated to run **once per logical frame** via `Unsorted::CurrentFrame`. In Lua, don't use `os.time()`/`os.clock()` for gameplay decisions (causes OOS).
- Coordinates are leptons (1 cell = 256 leptons); squared distances can overflow signed 32-bit → use 64-bit arithmetic.

## Mods

- `scripts/mods/<id>/main.lua` returns a table of callbacks (`Update`, `OnScenarioStart`, `OnPreDamage`, `OnUnitDestroyed`). Active IDs go one-per-line in `scripts/active_mods.txt` (`#` = comment); the launcher writes it, `init.lua` reads it.
- **Load order matters**: for same-frame writes, the mod loaded later (lower in the list) wins. `init.lua` resolves paths relative to the DLL/module dir, not the CWD (CnCNet changes CWD). See `docs/MOD_MANAGER.md` for order/conflict semantics.

## Truth sources

- **Current milestone: MILESTONE 12**, API line `1.1.0`, release `v1.0.0` — see `PROJECT/ROADMAP.md` + `PROJECT/CHANGELOG.md`. The `docs/AGENTS.md` "Status" block is **stale** (still says Milestone 10 / v1.1); it follows its own "update status at end of session" protocol — trust ROADMAP/CHANGELOG over it.
- API reference: `API.md`. Beginner tutorial: `docs/TUTORIAL.md`. Verified capabilities/recipes: `PROJECT/CAPABILITIES.md`.
