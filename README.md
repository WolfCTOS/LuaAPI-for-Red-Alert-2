# ⚡ Red Alert 2: Yuri's Revenge — LuaAPI

[![Platform](https://img.shields.io/badge/Platform-Windows%20x86%20%2832--bit%29-blue.svg)](https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2)
[![Engine Target](https://img.shields.io/badge/Game-Yuri's%20Revenge%201.001-red.svg)](https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2)
[![Lua Version](https://img.shields.io/badge/Lua-5.4.7-blue.svg)](https://www.lua.org/)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-orange.svg)](https://isocpp.org/)
[![Release](https://img.shields.io/badge/Release-v1.0.0--core-green.svg)](https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2/releases)

A modern, crash-safe **Lua 5.4 scripting framework** for *Command & Conquer: Red Alert 2 — Yuri's Revenge* (`gamemd.exe` 1.001). 

Instead of relying solely on static INI parameters or recompiling low-level C++ DLLs, **LuaAPI** embeds a high-performance Lua runtime directly into the game simulation loop. Modders can author real-time dynamic gameplay mechanics, custom abilities, unit auras, and event logic in pure Lua.

---

## 🌟 Key Features

* **Embedded Lua 5.4.7 Runtime:** Powered by `sol2` v3.3 for high-performance C++ ↔ Lua interoperability.
* **Synchronous Simulation Hook:** Hooks into `Unsorted::MainLoop` (`0x0055D360`) via MinHook, executing Lua scripts synchronously with game ticks without race conditions.
* **Modular Mod System (`scripts/mods/`):** Install and run multiple isolated Lua mods simultaneously without modifying the core C++ DLL.
* **Crash-Safe Memory Model:** Automatic pointer validation, `InLimbo` checks, and `pcall` isolation prevent memory access violations (`0xC0000005`) even if a target unit or building is destroyed mid-frame.
* **Rich Game APIs:**
  * **Economy & Faction:** Read and transact player credits, inspect power output/drain, check diplomacy alliances.
  * **World & Objects:** Query all buildings and units on the map with live HP, coordinates, and owner resolution.
  * **Combat Actions:** Apply progressive Damage-Over-Time (DoT), calculate Euclidean distances, and trigger genuine EMP power blackouts (`IsPowerOnline` lockouts).
  * **In-Game HUD:** Render custom color-coded text messages directly into the player's in-game screen HUD.
* **Showcase Feature Included:** **Tesla Overload** — an interactive combat aura where player units overload, blackout, and progressively destroy enemy power plants and defenses.

---

## 🏗️ Architecture
