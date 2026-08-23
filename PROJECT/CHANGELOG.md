# Changelog

All notable changes to LuaPI for Red Alert 2.

## [0.6.0] - 2026-08-24

### Added
- Tesla Overload interactive gameplay module (`scripts/tesla_overload.lua`):
  pulsing EMP lock + electrical damage against enemy buildings, with a
  `DEBUG_MAP_WIDE` flag for instant map-wide testing and an 8-cell radius mode
  driven by player-unit proximity.
- Dynamic `package.path` resolution: the engine prepends `<DLL dir>/scripts/?.lua`
  so `require()` works regardless of the game's working directory.

### Fixed
- `ProcessDisabledObjects` dangling pointer validation: disabled-object entries
  are now verified by address against all active engine arrays
  (Building/Unit/Infantry/Aircraft) before any dereference; destroyed objects
  are dropped silently instead of crashing at timer expiry (post-victory crash).
- `TakeDamage` zero-health clamping: damage on already-dead objects is ignored,
  preventing further interaction with dying structures.

## [0.5.0] — Gate 5.1

### Added
- `house:IsAlliedWith(other_house)` via `HouseClass::IsAlliedWith`
- `obj:GetDistanceTo(other_obj)` — Euclidean distance in map cells
- `obj:TakeDamage(n)` — direct HP reduction (clamped at 0), logged as `[Combat]`
- `obj:Disable(frames)` — timed disable:
  - buildings via `BuildingClass::DisableStuff()` / `EnableStuff()`
  - units/infantry via `TechnoClass::Deactivated` flag
  - auto re-enable tracked per-frame from the game loop; dead objects are skipped

## [0.4.0] — Gate 4.1

### Added
- `src/bindings_techno.cpp`: unified `LuaAPI.Techno` userdata handle
  - Methods: `GetTypeName`, `GetHealth`, `GetMaxHealth`, `GetOwner`, `GetPosition` (cell coords), `IsAlive`
  - All methods validate pointer liveness (`ptr != nullptr && Health > 0`)
- Global `World` namespace: `World.GetBuildings()`, `World.GetUnits()` iterating YRpp arrays
- `PushHouse` exported from house bindings for cross-module wrapping
- `scripts/init.lua`: one-shot world scanner logging building/unit names, HP and positions

## [0.3.0] — Gate 3.1

### Added
- `src/bindings_house.cpp`: global `House` namespace (`GetPlayer`, `GetCount`, `GetByIndex`)
- House instance methods: `GetCredits`, `SetCredits`, `AddCredits`, `GetPowerOutput`, `GetPowerDrain`, `GetName`, `IsHuman`
- Credits changes routed through the game's own `TransactMoney`

### Fixed
- Missing global `byte` typedef required by YRpp headers in new TUs

## [0.2.0] — Gate 2.2

### Changed
- Hook target corrected to `Unsorted::MainLoop` @ `0x55D360` (documented in YRpp);
  previous guessed address `0x685650` never fired
- Detour calling convention matched to target (`void __fastcall()`)

### Added
- `Engine.PrintMessage(text)` → `MessageListClass::PrintMessage`
- First-fire hook verification logging; MH status codes logged

## [0.1.0] — Milestone 1

### Added
- CMake Win32 build, static CRT (`/MT`) for all targets
- `injector.exe`: remote-thread injection with dynamic DLL path resolution
- Rotating file logger (`LuaAPI.log`, 5 MB × 3), initialized off the loader lock
- Lua engine bootstrap on the main game thread; `print` redirected to log
- Auto-deploy of binaries to the game directory after build
