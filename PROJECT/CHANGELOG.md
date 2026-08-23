# Changelog

All notable changes to LuaPI for Red Alert 2.

## [0.4.0] — Gate 4.1 (in progress)

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
