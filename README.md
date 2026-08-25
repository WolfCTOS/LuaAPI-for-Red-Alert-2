# LuaAPI for Red Alert 2 — Yuri's Revenge

A native x86 runtime that injects a **Lua 5.4 scripting engine** into `gamemd.exe` (Yuri's Revenge 1.001), enabling gameplay mechanics to be written in pure Lua instead of raw C++/ASM. The flagship showcase: **Tesla Overload** — a pulsing EMP + damage mechanic that disables and destroys enemy structures.

> **Status:** v1.0.0-Core — stable MinHook engine with modular ModLoader. Tagged [`v1.0.0-core`](https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2/releases/tag/v1.0.0-core).

---

## Architecture

```
injector.exe ──► gamemd.exe (suspended) ──► inject LuaAPI.dll ──► resume
                                                    │
                                    MinHook trampoline on Unsorted::MainLoop
                                              @ 0x55D360 (per-frame)
                                                    │
                              Lazy Lua-state init on the main game thread
                                                    │
                                   scripts/init.lua (Universal ModLoader)
                                                    │
                          scripts/mods/<mod_name>/main.lua (pure Lua mods)
```

**Key design decisions**

| Aspect | Decision |
|---|---|
| Hook target | `Unsorted::MainLoop` @ `0x55D360` (documented in YRpp), via MinHook trampoline |
| Threading | The entire Lua state lives on the main game thread; lazy-initialized on the first hook tick. Only the rotating logger is cross-thread (mutex-protected). |
| Safety | All bindings validate pointer liveness against engine arrays; script errors are contained by `pcall` and logged — never crash the game. |
| CRT | Statically linked (`/MT`) — no VC++ redistributable needed inside the game process. |
| Modding | `LuaAPI.dll` is a frozen host platform; gameplay is authored as pure Lua modules in `scripts/mods/<name>/main.lua`. |

Community context and upstream validation notes: see [`PROJECT/AI_CONTEXT.md`](PROJECT/AI_CONTEXT.md).

---

## Quick Start

1. **Build** (or use the deployed binaries):

   ```cmd
   cmake -B build -A Win32
   cmake --build build --config RelWithDebInfo
   ```

   Binaries are auto-deployed next to `CMakeLists.txt` (your game directory) after every successful build.

2. **Launch the game:**
   - Double-click **`injector.exe`** — it spawns `gamemd.exe` suspended, injects `LuaAPI.dll`, resumes it, and exits.
   - If `gamemd.exe` is already running, `injector.exe` attaches and injects into it instead.

3. **Verify:** check `LuaAPI.log` next to the DLL:

   ```
   [..] MainLoop hook fired! (first execution)
   [..] [script] [LuaAPI] Universal ModLoader Online!
   [..] [script] [LuaAPI] [+] Mod active: 'tesla_overload'
   ```

### Manual attach (advanced)

```cmd
injector.exe                 :: 1-click launch or attach
injector.exe D:\path\LuaAPI.dll   :: explicit DLL path (attach mode)
injector.exe "gamemd.exe" -SPAWN -LOG -CD   :: spawner mode (creates process, injects, waits)
```

---

## Writing Mods

A mod is a folder inside `scripts/mods/<mod_name>/` containing:

- **`mod.json`** — mod manifest with `id`, `name`, `version`, `author`, `description`, and `conflicts` arrays.
- **`main.lua`** — module table with an optional `Update(frame)` function, called every game frame.

### Mod manifest (`mod.json`)

```json
{
  "id": "tesla_overload",
  "name": "Tesla Overload",
  "version": "1.0.0",
  "author": "WolfCTOS",
  "description": "Base electrical overload & EMP blackout aura",
  "conflicts": []
}
```

### Mod code (`main.lua`)

```lua
local MyMod = {}

function MyMod.Update(frame)
    local player = House.GetPlayer()
    if player then
        Engine.PrintMessage(string.format("Hello from MyMod at frame %d!", frame))
    end
end

return MyMod
```

### Activation

Add the mod ID to `scripts/active_mods.txt` (one per line, `'#'` for comments), or enable it via the **GUI Mod Manager** in `injector.exe`. The Universal ModLoader (`scripts/init.lua`) reads active mod IDs from `scripts/active_mods.txt` on startup.

- Mods load via `require` — the engine prepends `<DLL dir>/scripts/?.lua` to `package.path`.
- Each mod's `Update` runs inside `pcall`: one broken mod logs an error and keeps running; it never crashes the game.
- Conflicts declared in `mod.json`'s `conflicts` array are checked by the injector's conflict detector, but are not actively verified under real conflicting mods.

---

## API Reference

### `Engine`

| Function | Description |
|---|---|
| `Engine.PrintMessage(text)` | Shows `text` in the in-game HUD message list (UTF-8 input). |

### `House`

| Function | Description |
|---|---|
| `House.GetPlayer()` | Returns the local player's house handle, or `nil`. |
| `House.GetCount()` | Number of houses in the scenario. |
| `House.GetByIndex(idx)` | House handle at `idx` (bounds-checked), or `nil`. |

**House handles** support:

| Method | Returns |
|---|---|
| `house:GetCredits()` | Available money (via `Available_Money()`). |
| `house:SetCredits(amount)` | Sets credits through the game's own money transaction. |
| `house:AddCredits(delta)` | Adds/subtracts credits. |
| `house:GetPowerOutput()` / `house:GetPowerDrain()` | Power grid values. |
| `house:GetName()` | Internal house ID string (e.g. `"Americans"`). |
| `house:IsHuman()` | Whether a human controls this house. |
| `house:IsAlliedWith(other)` | Alliance test between two houses. |

### `World`

| Function | Description |
|---|---|
| `World.GetBuildings()` | Array of building handles (`BuildingClass::Array`). |
| `World.GetUnits()` | Array of unit handles (`UnitClass::Array`). |

### Techno handles (buildings & units)

All methods validate that the underlying object is still alive.

| Method | Returns |
|---|---|
| `obj:GetTypeName()` | Type ID string (e.g. `"GAPOWR"`). |
| `obj:GetHealth()` / `obj:GetMaxHealth()` | Current / maximum health. |
| `obj:GetOwner()` | Owning house handle. |
| `obj:GetPosition()` | `{x = cellX, y = cellY, z = ...}` in map cells. |
| `obj:IsAlive()` | Liveness (health > 0, not in limbo). |
| `obj:GetDistanceTo(other)` | Euclidean distance in cells. |
| `obj:TakeDamage(n)` | Applies damage (clamped at 0); returns remaining HP. |
| `obj:Disable(frames)` | Timed EMP-style disable — buildings lose power (`HasPower` + `DisableStuff()`), units get paralyzed (`ParalysisTimer`). Auto-restores on expiry. |

### Global helpers

| Function | Description |
|---|---|
| `print(...)` | Redirected to `LuaAPI.log` (tagged `[script]`). |
| `OnTick(frame)` | Define this in your mod; called every game frame with the current frame number. |

---

## Logging

`LuaAPI.log` is written next to `LuaAPI.dll` using a rotating sink (**5 MB × 3 files**). Contents include engine lifecycle, hook status, `[script]` output, `[HUD]` messages, `[Combat]` events, and per-line source locations. It initializes off the loader lock and is safe across threads.

---

## Project Layout

```
├── CMakeLists.txt          Win32 build (MSVC, /MT, C++20)
├── injector.cpp            Dual-mode launcher/injector (spawn + attach)
├── include/LuaAPI/         Public headers (logger, lua_engine, bindings)
├── src/                    dllmain, lua_engine, bindings_house, bindings_techno
├── scripts/
│   ├── init.lua            Universal ModLoader
│   └── mods/               Pure-Lua gameplay modules
│       └── tesla_overload/main.lua
├── PROJECT/                Roadmap, changelog, AI project context
└── third_party/            YRpp, lua, sol2, spdlog, minhook (git submodules)
```

## Build Requirements

- Visual Studio 2026/2022 with **Desktop development with C++** (MSVC v145+, Win32 toolchain)
- CMake 3.16+
- Windows SDK 10.x
- Git (submodules): `git submodule update --init --recursive`

## Documentation

- [`docs/TUTORIAL.md`](docs/TUTORIAL.md) — beginner-friendly modding tutorial: write a complete gameplay mod in ~30 lines of Lua
- [`docs/ENGINEERING_LESSONS.md`](docs/ENGINEERING_LESSONS.md) — deep technical retrospective: YR engine internals, warhead mechanics, safe handle validation, MinHook nuances
- [`PROJECT/ROADMAP.md`](PROJECT/ROADMAP.md) — milestones and gate tracking
- [`PROJECT/CHANGELOG.md`](PROJECT/CHANGELOG.md) — version history
- [`PROJECT/AI_CONTEXT.md`](PROJECT/AI_CONTEXT.md) — architecture decisions & upstream context

## Credits

- **[YRpp](https://github.com/Phobos-developers/YRpp)** — reverse-engineered game structures (Phobos developers & community)
- **[MinHook](https://github.com/TsudaKageyu/minhook)** — x86 trampoline hooking (TsudaKageyu)
- **[Lua 5.4](https://www.lua.org/)** / **[spdlog](https://github.com/gabime/spdlog)** / **[sol2](https://github.com/ThePhD/sol2)**
- Community validation: Kerbiter (Phobos lead) regarding CnCNet `-SPAWN` retrofitting constraints
