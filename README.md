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

┌─────────────────────────────────────────────────────────────┐
│ gamemd.exe 1.001 │
│ (Main Simulation Loop) │
└──────────────────────────────┬──────────────────────────────┘
│ Inline MinHook (0x55D360)
▼
┌─────────────────────────────────────────────────────────────┐
│ LuaAPI.dll │
│ [ YRpp Class Bindings | Safe Handles | spdlog Logger ] │
└──────────────────────────────┬──────────────────────────────┘
│ sol2 Bridge
▼
┌─────────────────────────────────────────────────────────────┐
│ scripts/init.lua │
│ (Universal ModLoader Host) │
└──────────────┬───────────────────────────────┬──────────────┘
│ loads │ loads
▼ ▼
scripts/mods/tesla_overload/ scripts/mods/custom_mod/
└── main.lua └── main.lua
code Code

---

## 🚀 Quick Start

### 1. Installation
1. Download the latest release from the [Releases](https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2/releases) page.
2. Extract `LuaAPI.dll`, `injector.exe`, and the `scripts/` folder into your Yuri's Revenge game directory (where `gamemd.exe` is located).

### 2. Launching
1. Start **`gamemd.exe`** (or your preferred launcher/skirmish match).
2. Run **`injector.exe`**.
3. You will immediately see the welcome notification on your in-game HUD:
   ```text
   Commander: Russians | LuaAPI Platform: 1 Mod(s) Active

✍️ How to Create a Lua Mod

To create a new mod, you never need a C++ compiler. Simply create a new folder under scripts/mods/:
Example: scripts/mods/auto_healer/main.lua
code Lua

local AutoHealer = {}

local HEAL_INTERVAL = 60 -- Heal every 60 frames (~2 seconds)
local lastHealFrame = 0

function AutoHealer.Update(frame)
    if frame - lastHealFrame < HEAL_INTERVAL then return end
    lastHealFrame = frame

    local player = House.GetPlayer()
    if not player then return end

    -- Find damaged player buildings and repair them
    local buildings = World.GetBuildings()
    for _, bld in ipairs(buildings) do
        if bld:IsAlive() then
            local owner = bld:GetOwner()
            if owner and owner:GetName() == player:GetName() then
                local currentHp = bld:GetHealth()
                local maxHp = bld:GetMaxHealth()
                if currentHp < maxHp then
                    -- Repair 50 HP (using negative damage)
                    bld:TakeDamage(-50)
                    Engine.PrintMessage(string.format("🔧 Auto-Repaired %s (+50 HP)", bld:GetTypeName()))
                end
            end
        end
    end
end

return AutoHealer

Activate your mod in scripts/init.lua:
code Lua

local ACTIVE_MODS = {
    "tesla_overload",
    "auto_healer"      -- Added here!
}

📚 API Reference Overview
Engine

    Engine.PrintMessage(string text) — Displays a text message in the top-left in-game HUD.

    Engine.version — Returns the current platform version string.

House

    House.GetPlayer() — Returns the local human player House object (or nil).

    House.GetByIndex(int idx) — Returns House at index.

    House.GetCount() — Total houses in match.

    house:GetCredits() -> int — Returns current available money.

    house:SetCredits(int amount) / house:AddCredits(int delta) — Authoritative money transactions.

    house:GetPowerOutput() -> int / house:GetPowerDrain() -> int — Power grid statistics.

    house:GetName() -> string — Internal country/faction ID (e.g. "Russians", "Americans").

    house:IsAlliedWith(House other) -> bool — Diplomacy check.

World

    World.GetBuildings() -> table — Returns array of active Techno building handles.

    World.GetUnits() -> table — Returns array of active Techno unit handles.

Techno (Buildings & Units)

    obj:GetTypeName() -> string — INI type ID (e.g. "GAPOWR", "HTK", "TESLA").

    obj:GetHealth() -> int / obj:GetMaxHealth() -> int — Current and maximum HP.

    obj:GetOwner() -> House — Returns the owning player handle.

    obj:GetPosition() -> {x, y, z} — Cell grid coordinates.

    obj:GetDistanceTo(Techno other) -> number — Euclidean distance in cells.

    obj:TakeDamage(int damage) -> int — Applies damage and returns remaining HP.

    obj:Disable(int frames) — Applies genuine EMP blackout / weapon lockout for specified frame duration.

    obj:IsAlive() -> bool — Validates pointer existence and Health > 0.

🛠️ Building from Source
Prerequisites

    Windows 10/11 (x86 compilation target)

    Visual Studio 2022 (MSVC v143 toolset with C++20 support)

    CMake 3.20+

Build Commands
code Cmd

git clone --recursive https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2.git
cd LuaPI-for-Red-Alert-2
cmake -B build -A Win32
cmake --build build --config RelWithDebInfo

Output binaries (LuaAPI.dll and injector.exe) will be automatically placed in the project root.
🤝 Acknowledgments & Credits

    Westwood Studios & EA — Command & Conquer: Red Alert 2 & Yuri's Revenge.

    YRpp — The Yuri's Revenge reverse-engineered C++ platform maintained by the Phobos team.

    MinHook — Minimalistic x86 API hooking library.

    sol2 — Modern C++ binding library for Lua.

    spdlog — Fast C++ logging library.

    Special thanks to the RA2 modding community for technical insights.

⚖️ License

Distributed under the MIT License. See LICENSE for more information.
code Code

---

### КАК ОБНОВИТЬ README В РЕПОЗИТОРИИ

Отправь в **OpenCode** команду:

```markdown
### TASK: Update README.md with Official Documentation

**Instructions for OpenCode:**
1. Overwrite `README.md` with the official LuaAPI documentation.
2. Commit:
   `docs: update comprehensive README with architecture, quick start and API reference`
3. Push to `origin main`.
