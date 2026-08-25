\# Red Alert 2 LuaAPI — AI Project Context

\## 1. Project


Project: LuaAPI for Red Alert 2: Yuri's Revenge

Goal: Create an injectable DLL that adds Lua scripting capabilities on top of the game's existing INI-based configuration and engine functionality.

Long-term showcase goal: Tesla Overload — a gameplay mechanic where a Tesla-related unit can overload enemy buildings/power infrastructure, disable them, and progressively damage them.

Current project stage: rebuilding the project after loss of the previous D:\\ drive contents.

The human project owner does not directly write the C++ implementation or OpenCode prompts. The owner defines goals, desired gameplay behavior, constraints, and evaluates results. LLMs translate those goals into technical plans/prompts. OpenCode performs repository/code/build operations.

Important: do not assume the human owner is an experienced C++ or reverse-engineering developer. Explain important technical decisions and verify assumptions instead of relying on implicit expertise.

\---

\## 2. Platform

Game:

\- Red Alert 2: Yuri's Revenge

\- Windows x86 / Win32

Development environment:

\- Visual Studio 2026 Community

\- MSVC 14.51

\- CMake 4.3

\- Windows SDK 10.x

Project location:
D:\\Games\\Red Alert 2\\LuaAPI
Game location:
D:\\Games\\Red Alert 2

\---
\## 3. Repository Structure
Current intended structure:
LuaAPI/

├── CMakeLists.txt

├── .gitignore

├── include/

│   └── LuaAPI/

├── src/

├── third\_party/

│   ├── YRpp/

│   ├── lua/

│   ├── sol2/

│   └── spdlog/

├── scripts/

└── PROJECT/

&#x20;   └── AI\_CONTEXT.md
third\_party dependencies are Git submodules.

The repository is hosted on GitHub:
https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2

Main branch:
main
\---
\## 4. Dependencies
\### YRpp
Repository:
https://github.com/Phobos-developers/YRpp

Purpose:

\- Reverse-engineered structures and interfaces for Red Alert 2 / Yuri's Revenge.

\- Used as a reference for game classes, layouts, arrays, functions, and offsets.

\### Lua
Version:
5.4.7
\### sol2

Version:
3.3.0

Purpose:
\- C++ ↔ Lua bindings.


\### spdlog

Version:
1.12.0

Purpose:
\- Logging from the injected DLL.

\---

\## 5. Build
Expected Win32 build environment:

```powershell

"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86

## 6. Architecture Decisions & Upstream Context

- **Engine Model:** Red Alert 2 (Yuri's Revenge 1.001) has no native scripting runtime; legacy modding relies strictly on INI expansion (Ares/Phobos).
- **Community Validation (Kerbiter / Phobos Lead):** Confirmed that retrofitting legacy CnCNet `-SPAWN` handling is non-standard for external scripting. The platform therefore adopts a standalone MinHook-injected runtime with a modular `scripts/mods/<mod_name>/main.lua` system.
- **Modding Paradigm:** C++ `LuaAPI.dll` acts as a frozen host platform. All gameplay mechanics are authored as pure Lua modules inside `scripts/mods/`, loaded by the Universal ModLoader (`scripts/init.lua`) with per-mod error isolation (`pcall`).

### Hook & injection summary (validated)

- Hook target: `Unsorted::MainLoop` @ `0x55D360` via MinHook trampoline (YRpp-documented address). Syringe `.inj`/`.syhks00` hooking was evaluated and rolled back; Syringe remains only an optional dependency for CnCNet spawner hooks (cncnet5.dll) and is never required for the LuaAPI engine itself.
- Injection: `injector.exe` — attach mode (game already running) or 1-click spawn mode (`CREATE_SUSPENDED` → inject → resume).
- Lua state lives on the main game thread (lazy init inside first hook tick); logger is the only cross-thread component.

## 7. Explicit Boundaries & Triggers

- **Milestone 4 (Inbound Events):** COMPLETED. Sub-frame pre-damage mitigation via `OnPreDamage` events is now available. Mods can register callbacks through `game_RegisterEvent("OnPreDamage", callback)` to modify damage values before application. Entry criteria met and verified.

- **Milestone 5 (CnCNet Multiplayer):** COMPLETED. Singleplayer/Skirmish via standalone `injector.exe` is the supported baseline. CnCNet spawner mode (`--withcncnet flag`) has been verified for correct injection timing, RNG determinism, and absence of desync discrepancies when calling Lua API.

- **Conflict Detector:** VERIFIED. Code in `src/injector_gui.cpp` based on declared `conflicts` arrays in `mod.json` has been tested with mutual conflict scenarios (mod_a vs mod_b). GUI banner correctly displays conflict warnings when conflicting mods are simultaneously enabled.
