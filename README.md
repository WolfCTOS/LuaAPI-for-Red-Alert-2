# LuaAPI for Red Alert 2 — Yuri's Revenge

A native x86 runtime that injects a **Lua 5.4 scripting engine** into `gamemd.exe` (Yuri's Revenge 1.001), enabling gameplay mechanics to be written in pure Lua instead of raw C++/ASM. The flagship showcase: **Tesla Overload** — a pulsing EMP + damage mechanic that disables and destroys enemy structures.

> **Status:** v1.0.0-Core — stable MinHook engine with modular ModLoader. Tagged [`v1.0.0-core`](https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2/releases/tag/v1.0.0-core).

---

## 🚀 Quick Start

### Launch via GUI Injector
- Double-click **`injector.exe`** — it spawns `gamemd.exe` suspended, injects `LuaAPI.dll`, resumes it, and exits.
- If `gamemd.exe` is already running, `injector.exe` attaches and injects into it instead.

### Launch under CnCNet
```cmd
injector.exe --withcncnet
```
Use this flag to run the game through the CnCNet spawner. Note: `os.time()` and `os.clock()` are **disabled** in the Lua runtime to prevent Out-of-Sync (OOS) desync between clients. See the **RNG Determinism** section below.

### Manage Mods
- **GUI Mod Manager**: open `injector.exe`, use the Mod Manager card list to enable/disable mods, view conflict warnings.
- **Manual**: edit `scripts/active_mods.txt` — one mod ID per line, `#` for comments. The Universal ModLoader reads this file on startup.

---

## 📁 Mod Anatomy (Mod Structure)

A mod is a folder inside `scripts/mods/<mod_name>/` containing:

### `mod.json` — Mod Manifest
```json
{
  "id": "shield_overload",
  "name": "Shield Overload",
  "version": "1.0.0",
  "author": "WolfCTOS",
  "description": "Absorbs incoming damage via pre-damage events",
  "conflicts": ["mod_a"]
}
```

**Fields:**
- `id` — unique mod identifier (used in `active_mods.txt` and conflict detection).
- `name` — display name.
- `version` — mod version string.
- `author` — author name or handle.
- `description` — short description shown in the GUI.
- `conflicts` — array of mod IDs that conflict with this mod. The injector's conflict detector will warn if two conflicting mods are enabled simultaneously.

### `main.lua` — Entry Point
A Lua module table returned at the end of the file. It may contain:
- `Update(frame)` — called every game frame (polling).
- `OnPreDamage(...)` — event callback (see below).
- Any other functions your mod needs.

### Example: `main.lua` for Shield Overload Mod
```lua
local ShieldOverload = {}

--[[
  Inbound Event: OnPreDamage
  Called by the C++ hook before damage is applied.
  Return a number to modify the damage, or nil to keep the original value.
]]
function ShieldOverload.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    -- Absorb 50% of energy and explosive damage
    local absorbed_types = {"energy", "explosive", "emp"}
    for _, dt in ipairs(absorbed_types) do
        if dmg_type == dt then
            return damage * 0.5
        end
    end
    -- Return nil = no change (original damage applied)
    return nil
end

-- Register the event with the C++ injector
function ShieldOverload.OnRegister()
    game_RegisterEvent("OnPreDamage", ShieldOverload.OnPreDamage)
end

-- Standard Update (optional — polling fallback)
function ShieldOverload.Update(frame)
    -- No per-frame logic needed for this mod
end

return ShieldOverload
```

---

## 💻 Writing Code (`main.lua`)

### 1. Update Polling (Traditional)
Every game frame, the Universal ModLoader calls `mod.Update(frame)`. This is the safe, always-available method.

```lua
function MyMod.Update(frame)
    local player = House.GetPlayer()
    if player then
        Engine.PrintMessage(string.format("Hello from MyMod at frame %d!", frame))
    end
end
```

### 2. Inbound Events (Sub-Frame Pre-Damage Hooks)
Mods can register callbacks for `OnPreDamage` events. These fire **before** damage is applied, allowing mods to absorb, redirect, or cancel damage. This is how absorbing shields and damage modifiers work.

**Registration:**
```lua
function MyMod.OnRegister()
    game_RegisterEvent("OnPreDamage", function(attacker, target, damage, dmg_type, frame, subc)
        -- Modify damage here
        return damage * 0.8  -- absorb 20%
    end)
end
```

**Event parameters:**
- `attacker` — the techno/unit dealing damage
- `target` — the techno/unit receiving damage
- `damage` — current damage value (number; return a new number to change it)
- `dmg_type` — string: `"energy"`, `"explosive"`, `"emp"`, etc.
- `frame` — current game frame number
- `subc` — sub-frame tick (0–255, granularity within the frame)

**⚠️ RNG Determinism — Critical for CnCNet Multiplayer**
- **Never use `os.time()` or `os.clock()`** in multiplayer gameplay logic. These functions produce different values on different clients, causing **Out-of-Sync (OOS)** desync.
- Use the **deterministic seed** `12345` (set at program start).
- If you need frame-unique randomness, use: `math.randomseed(frame + 12345)` inside `OnTick()` or `Update()`.
- The C++ layer enforces this by sandbox-blocking `os.time` and `os.clock` in CnCNet mode.

### 3. Multiplayer & RNG Determinism
- CnCNet mode (`--withcncnet`) enforces deterministic RNG.
- All mod Lua code is sandboxed: `os.time()` and `os.clock` calls are no-ops that return `0`.
- If your mod needs random values, initialize once at load:
```lua
math.randomseed(12345)  -- fixed seed for all clients
-- or per-frame:
math.randomseed(frame + 12345)
```
- Avoid `timer.os` or any wall-clock dependent timing in multiplayer.

---

## 📊 Profiling & Benchmarking (Performance & Profiling)

### In-Module Timing (Built-In)
The HookProfiler automatically measures per-frame hook cost. Every 5 seconds, the following is appended to `LuaAPI.log`:
```
[14:23:45] avg_ms=1.23 min_ms=0.50 max_ms=5.20 p95_ms=3.10 calls=1200
```
No code changes needed — it's always on.

### Manual Benchmark with PresentMon
1. Ensure `presentmon.exe` is available on PATH or place it alongside `injector.exe`.
2. Run the benchmark script:
```powershell
powershell -ExecutionPolicy Bypass -File tools/run_benchmark.ps1
```
3. The script:
   - Spawns `gamemd.exe` (or launches under CnCNet with `--withcncnet`).
   - Starts PresentMon to capture `presentmon_benchmark.csv`.
   - Waits 65 seconds of gameplay.
   - Stops PresentMon.
   - Invokes `tools/benchmark_analyzer.py` on the CSV.

### Analyze Results
```powershell
python tools/benchmark_analyzer.py presentmon_benchmark.csv
```
**Output:**
- **Avg FPS** — average frames per second over the analysis period (after 5s skip).
- **1% Low FPS** — the FPS value that is higher than 99% of all frames (protects against momentary hitches).
- **Frame Time stats** — average, min, max, median frame times in milliseconds.

---

## 📂 Repository Structure

```
LuaAPI/                                     Project root
├── CMakeLists.txt           Win32 build (MSVC, /MT, C++20)
├── injector.cpp           Dual-mode launcher/injector (spawn + attach)
├── include/LuaAPI/      Public headers (logger, lua_engine, bindings)
├── src/                     dllmain, lua_engine, bindings_house, bindings_techno
│   └── hook_profiler.h/cpp     QPC circular buffer profiler
├── scripts/
│   ├── init.lua           Universal ModLoader (reads active_mods.txt,
│                           registers events, seeds RNG)
│   ├── mods/              Pure-Lua gameplay modules
│   │   ├── tesla_overload/        tesla_overload/mod.json + main.lua
│   │   ├── shield_overload/     shield_overload/mod.json + main.lua
│   │   ├── mod_a/               mod_a/mod.json + main.lua  (conflict test)
│   │   └── mod_b/               mod_b/mod.json + main.lua  (conflict test)
│   └── active_mods.txt    One mod ID per line; '#' = comment
├── PROJECT/
│   ├── ROADMAP.md     Milestones and gate tracking
│   ├── AI_CONTEXT.md  Architecture decisions & upstream context
│   └── CHANGELOG.md   Version history
├── tools/
│   ├── benchmark_analyzer.py   PresentMon CSV analyzer
│   └── run_benchmark.ps1      65s benchmark automation
├── third_party/     YRpp, lua, sol2, spdlog, minhook (git submodules)
└── README.md          This file
```