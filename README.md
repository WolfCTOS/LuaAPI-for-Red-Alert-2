# LuaAPI for Red Alert 2 — Yuri's Revenge

A high-performance, native x86 runtime that injects a **Lua 5.4 scripting engine** into `gamemd.exe` (Yuri's Revenge 1.001). It enables complex, dynamic gameplay mechanics, reactive inbound events, and stateful mod logic to be written in pure Lua with **zero engine overhead** (< 0.01 FPS delta).

> **Status:** `v1.0.0 Production Release` (All Milestones 1–9 Verified & Benchmarked).  
> **Compatibility:** Singleplayer, Skirmish, and CnCNet Multiplayer (Deterministic RNG).

---

## ⚡ Key Features

- **Sub-Frame Inbound Events:** Intercept and modify damage before application (`OnPreDamage`) for custom shields, armor types, and damage reflection.
- **Zero-Overhead Runtime:** Lock-free QPC frame profiling confirmed **60.04 Avg FPS / 55.11 1% Low FPS** (identical to pure Vanilla). See [BENCHMARK.md](BENCHMARK.md).
- **Crash-Resistant Pointer Safety:** All engine object bindings validate pointers via RTTI (`WhatAmI()`) and lifecycle flags — preventing `0xC0000005` Access Violations on dead units.
- **Clean Session Lifecycle:** Automatic Lua state & callback cleanup on scenario load, restart, and exit (`ResetSession`).
- **CnCNet Determinism:** Synchronized frame-seeded RNG preventing Out-of-Sync (OOS) in multiplayer.
- **Modular Mod Ecosystem:** Isolated `scripts/mods/<name>/` directories with `mod.json` manifests and GUI conflict detection.

---

## 🚀 Quick Start

### 1. Launch via GUI Mod Manager
- Launch **`injector.exe`**.
- Toggle desired mods in the card list, view conflict warnings, and click **Launch Game**.
- The injector handles suspended launch, DLL injection, and DPI scaling automatically.

### 2. Launch under CnCNet Multiplayer
```batch
injector.exe --withcncnet

Runs in headless spawner mode compatible with CnCNet clients.
```

### 3. Manual Mod Activation

Edit `scripts/active_mods.txt` (one mod ID per line, `#` for comments):

```text
shield_overload
bounty_hunter
tesla_overload
```

---

## 📁 Mod Anatomy (Folder Structure)

Each mod lives in its own directory under `scripts/mods/<mod_id>/`:

```text
scripts/mods/bounty_hunter/
├── mod.json       # Mod manifest
└── main.lua       # Entry point
```

### `mod.json` — Manifest

```json
{
  "id": "bounty_hunter",
  "name": "Bounty Hunter",
  "version": "1.0.0",
  "author": "YourName",
  "description": "Awards credits and displays HUD alerts on combat hits",
  "conflicts": []
}
```

---

## 💻 Writing Mod Scripts (`main.lua`)

### 1. Inbound Events (Sub-Frame Pre-Damage Hooks)

Intercept incoming damage before it is applied to the unit:

```lua
local ShieldMod = {}

-- Callback: (attacker, target, damage, dmg_type, frame, subc)
function ShieldMod.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    -- Absorb 50% of energy and explosive damage
    if dmg_type == "energy" or dmg_type == "explosive" then
        return damage * 0.5 -- Return modified damage
    end
    return nil -- Keep original damage
end

function ShieldMod.OnRegister()
    game_RegisterEvent("OnPreDamage", ShieldMod.OnPreDamage)
end

return ShieldMod
```

### 2. Economy & HUD Messaging

Award credits and display notifications in the in-game message feed:

```lua
local BountyMod = {}

function BountyMod.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    -- Award $50 bounty to attacking player
    local attackerHouse = attacker:GetHouse()
    if attackerHouse then
        house_AddCredits(attackerHouse, 50)
        game_PrintMessage("[Bounty] +$50 awarded for combat hit!", 1)
    end
    return nil
end

function BountyMod.OnRegister()
    game_RegisterEvent("OnPreDamage", BountyMod.OnPreDamage)
end

return BountyMod
```

### 3. Unit State & Visual Effects (VFX)

Directly control health ratios and attach particle systems without map trigger hacks:

```lua
local FleetMod = {}

function FleetMod.Update(frame)
    -- Example: damaged starting unit setup on first frame
    if frame == 1 then
        local unit = ... -- acquired unit
        unit:SetHealthRatio(0.35)                     -- Set to 35% HP
        unit:AttachParticleSystem("DamageSmokeSys")    -- Attach real damage smoke
    end
end

return FleetMod
```

### 4. Multiplayer RNG Guidelines (CnCNet OOS Prevention)

> ⚠️ **Never use `os.time()` or `os.clock()` in gameplay logic.**

The Lua runtime initializes a synchronized deterministic seed (12345).

For frame-unique randomness, use:

```lua
math.randomseed(frame + 12345)
local roll = math.random(1, 100)
```

---

## 📊 Performance & Benchmarks

Benchmarked via Intel PresentMon (hardware ETW capture, 65s active Skirmish combat, D3D9 renderer):

| Configuration | Average FPS | 1% Low FPS | 95th Percentile | Max FrameTime | Overhead |
|---|---|---|---|---|---|
| **Vanilla RA2: YR** | 60.05 | 54.40 | 17.56 ms | 22.92 ms | — (Baseline) |
| **Clean LuaAPI (Hook 0x55D360)** | 60.05 | 55.13 | 17.51 ms | 18.84 ms | **0.00%** |
| **Modded LuaAPI (Active Mods + Events)** | 60.04 | 55.11 | 17.31 ms | 18.62 ms | **< 0.02%** |

Full methodology, frame time distribution, and reproduction instructions are available in [BENCHMARK.md](BENCHMARK.md).

---

## 🛠️ Repository Structure

```text
├── src/               # C++ Core: Injector GUI, Hook Profiler, Lua Engine & Bindings
├── include/LuaAPI/    # Public C++ Header definitions & game struct bindings
├── scripts/           # Lua Runtime: init.lua dispatcher & mods/
│   └── mods/          # Built-in sample mods (shield_overload, bounty_hunter, etc.)
├── tools/             # PresentMon automation (run_benchmark.ps1, benchmark_analyzer.py)
├── PROJECT/           # Architecture context, Roadmap & Milestone tracking
├── BENCHMARK.md       # Empirical performance report
└── README.md          # Project documentation
```

---

## 📜 License & Credits

Yuri's Revenge is a trademark of Electronic Arts.

Developed for the C&C modding community (Haven / CnCNet / PPM).

---

## 📖 API Reference

Full documentation of all Lua functions, signatures, and safety guarantees: [API.md](API.md).
