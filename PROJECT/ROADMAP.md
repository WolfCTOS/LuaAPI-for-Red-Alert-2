# LuaPI for Red Alert 2 — Roadmap

x86 Lua scripting engine for **gamemd.exe (1.001)**, injected at runtime.
Each gate must be verified in-game before the next is started.

## Milestone 1 — Core Foundation ✅

| Gate | Description | Status |
|------|-------------|--------|
| 1.1 | Toolchain: Win32 build via CMake + MSVC, static CRT (/MT) | ✅ Done |
| 1.2 | Safe injection: `injector.exe` (LoadLibraryA remote thread), dynamic DLL path resolution | ✅ Done |
| 1.3 | Rotating logger (`LuaAPI.log`, 5 MB × 3) initialized off the loader lock | ✅ Done |

## Milestone 2 — Engine & Hooking ✅

| Gate | Description | Status |
|------|-------------|--------|
| 2.1 | Lua state created on the game main thread; `scripts/init.lua` executed | ✅ Done |
| 2.2 | Game-loop hook via MinHook on `Unsorted::MainLoop` @ `0x55D360`; `OnTick(frame)` dispatched with scenario guard | ✅ Done |
| 2.3 | `Engine.PrintMessage(text)` HUD API via `MessageListClass` | ✅ Done |

## Milestone 3 — Game Data Bindings 🚧

| Gate | Description | Status |
|------|-------------|--------|
| 3.1 | Safe `HouseClass` bindings: credits, power grid, owner info | ✅ Done |
| 4.1 | World scanning: `World.GetBuildings()` / `World.GetUnits()`, unified `LuaAPI.Techno` handle (type, health, owner, position) | 🚧 In Progress |
| 4.2 | Object manipulation: spawn/move/damage objects from Lua | ⬜ Planned |
| 5.1 | Event callbacks (object destroyed, house defeated, trigger fired) | ⬜ Planned |
| 5.2 | INI/rules reading and writing from Lua | ⬜ Planned |

## Architecture Notes

- **Threading:** Lua lives entirely on the main game thread (lazy init inside the hook). Logger is the only cross-thread component (mutex-protected).
- **Hook:** MinHook trampoline on `Unsorted::MainLoop`; original runs first, then Lua dispatch guarded by `ScenarioClass::Instance != nullptr`.
- **Safety:** every binding validates pointers; script errors are contained by `lua_pcall` and logged, never crash the game.
- **Deploy:** POST_BUILD copies `LuaAPI.dll` / `injector.exe` to the game root; `scripts/` is resolved relative to the DLL.
