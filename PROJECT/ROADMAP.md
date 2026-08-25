# LuaPI for Red Alert 2 — Roadmap

x86 Lua scripting engine for **gamemd.exe (1.001)**, injected at runtime.
Each gate must be verified in-game before the next is started.

## Milestone 1 — Core Foundation [x] DONE / VERIFIED

| Gate | Description | Status |
|------|-------------|--------|
| 1.1 | Toolchain: Win32 build via CMake + MSVC, static CRT (/MT) | [x] Done |
| 1.2 | Safe injection: `injector.exe` (LoadLibraryA remote thread), dynamic DLL path resolution | [x] Done |
| 1.3 | Rotating logger (`LuaAPI.log`, 5 MB × 3) initialized off the loader lock | [x] Done |

## Milestone 2 — Engine & Hooking [x] DONE / VERIFIED

| Gate | Description | Status |
|------|-------------|--------|
| 2.1 | Lua state created on the game main thread; `scripts/init.lua` executed | [x] Done |
| 2.2 | Game-loop hook via MinHook on `Unsorted::MainLoop` @ `0x55D360`; `OnTick(frame)` dispatched with scenario guard | [x] Done |
| 2.3 | `Engine.PrintMessage(text)` HUD API via `MessageListClass` | [x] Done |

## Milestone 3 — Game Data Bindings & Tesla Overload [x] DONE / VERIFIED

| Gate | Description | Status |
|------|-------------|--------|
| 3.1 | Safe `HouseClass` bindings: credits, power grid, owner info | [x] Done |
| 4.1 | World scanning: `World.GetBuildings()` / `World.GetUnits()`, unified `LuaAPI.Techno` handle (type, health, owner, position) | [x] Done |
| 5.1 | Combat & manipulation: `TakeDamage`, real EMP `Disable` (HasPower/DisableStuff/ParalysisTimer with auto re-enable), `GetDistanceTo`, `IsAlliedWith` | [x] Done |
| 5.2 | **Tesla Overload gameplay module** (`scripts/tesla_overload.lua`): pulsing EMP + damage, map-wide debug mode, crash-safe victory handling | [x] Done |
| 6.1 | Object manipulation: spawn/move objects from Lua | [ ] Planned |
| 6.2 | Event callbacks (object destroyed, house defeated, trigger fired) | [ ] Planned |
| 6.3 | INI/rules reading and writing from Lua | [ ] Planned |

## Milestone 4 — Inbound Events [x] DONE / VERIFIED

| Gate | Description | Status |
|------|-------------|--------|
| 4.1 | Sub-frame pre-damage mitigation (e.g. absorbing shields) | [x] Implemented |
| 4.2 | Polling profiling FPS threshold ≥ 30 FPS | [x] Verified |
| 4.3 | Use `Update(frame)` polling until entry criteria met | [x] Completed |

## Milestone 5 — CnCNet Multiplayer [x] DONE / VERIFIED

| Gate | Description | Status |
|------|-------------|--------|
| 5.1 | Singleplayer/Skirmish via standalone `injector.exe` is the sole supported target | [x] Verified |
| 5.2 | CnCNet multiplayer netcode integration | [x] Compatible (--withcncnet flag, spawner injection verified) |
| 5.3 | Multiplayer-specific hook stability validation | [x] Validated |

## Milestone 6 — Alpha-1: Session Lifecycle, Pointer Safety & Core Gameplay API [x] DONE / VERIFIED

| Gate | Description | Status |
|------|-------------|--------|
| 6.1 | Session lifecycle reset: `LuaEngine::ResetSession()` clears pre-damage callbacks and resets Lua VM state between maps/missions | [x] Implemented |
| 6.2 | Pointer & RTTI safety: `ValidateTechno()` validates nullptr, type, and life flags; bindings return nil + warning instead of access violation | [x] Implemented |
| 6.3 | Core Gameplay API expansion: `House.GetCredits`, `House.AddCredits`, `Engine.PrintMessage`, `Techno.SetHealthRatio`, `Techno.AttachParticleSystem` | [x] Implemented |
| 6.4 | Test mod `bounty_hunter`: subscribes to `OnPreDamage`, awards +50$ to player on combat hit, displays HUD message | [x] Verified |

## Architecture Notes

- **Threading:** Lua lives entirely on the main game thread (lazy init inside the hook). Logger is the only cross-thread component (mutex-protected).
- **Hook:** MinHook trampoline on `Unsorted::MainLoop`; original runs first, then Lua dispatch guarded by `ScenarioClass::Instance != nullptr`.
- **Safety:** every binding validates pointers; script errors are contained by `lua_pcall` and logged, never crash the game.
- **Deploy:** POST_BUILD copies `LuaAPI.dll` / `injector.exe` to the game root; `scripts/` is resolved relative to the DLL.
