# Changelog

All notable changes to LuaAPI for Red Alert 2 are documented here.

The changelog follows the project's verified milestone history. Features are listed as released or implemented only when supported by project documentation and runtime validation.

---

## [1.1.0] — Milestone 10 Core / Milestone 11 Complete / Milestone 12 Development — 2026-08-31

### Added

- **Sub-turret state system** via the native `SubTurretManager` sidecar associated with `TechnoClass*` objects.
- **Independent turret state** including turret identity, facing, target references, ROF timers, weapon information, and spatial offsets.
- **Multi-turret Lua bindings** for adding turrets, querying turret state, assigning split targets, and explicitly firing split salvos.
- **Independent target allocation** for multi-turret showcase gameplay.
- **Passive native turret updates** for timer management and target-facing rotation.
- **Spawned missile interception and decoupling** for spawned projectiles, including native locomotor destination control.
- **CnCNet development tooling** including process attachment, debug spawning, and logical-frame callback gating.
- **Unit Control API vertical slice** with `GetMission`, `GetTarget`, `MoveTo`, `Attack`, `Stop`, and `IsIdle`.
- **`patrol_demo`** showcase for exercising the unit-control path.

### Fixed

- Prevented iterator invalidation during turret cleanup by using deferred removal.
- Prevented stale target references from surviving engine-object destruction.
- Corrected distance calculations that could overflow signed 32-bit integers when working in leptons.
- Corrected Lua binding usage to the verified `GetOwner()` and `GetTypeName()` interfaces.
- Prevented autonomous C++ multi-turret firing from producing unintended attacks while units were idle or moving.
- Added savegame-aware runtime reinitialization for systems whose state is not restored through `OnScenarioStart()`.
- Fixed ModLoader path resolution for the LuaAPI/DLL environment.

### Changed

- **Architecture boundary:** C++ manages native state, lifecycle, safety, and engine integration; Lua controls gameplay decisions and attack behavior.
- **Hook compatibility:** existing hooks at the main loop are treated as compatibility conditions rather than automatic injection failures.
- **Milestone 10 scope:** functional multi-turret combat is complete; voxel matrix rendering is deferred to Milestone 12.
- **Milestone 11 scope:** CnCNet compatibility and development tooling are complete. Full two-client online multiplayer validation remains separate.
- **Milestone 12 scope:** unit control and tactical AI work is now the active development line.

### Verification

- Milestone 10 core: verified through the `multi_turret_battleship` showcase and native combat infrastructure.
- Milestone 11 CnCNet/tooling work: verified through attach mode, hook compatibility, logical-frame gating, debug spawning/input, and ModLoader path resolution.
- Spawned missile decoupling: verified through the native interception path.
- Milestone 12 Gate 12.1: native implementation is complete; runtime showcase verification remains pending because the first `patrol_demo` run exposed Lua-side script errors.

---

## [1.0.0] — Milestone 9: Production Release — 2026-08

### Added

- Public production release of LuaAPI for the C&C modding community.
- Stable runtime package and injector/launcher components.
- Example and showcase mods.
- Production documentation and API reference.

### Verification

- Milestones 1–8 completed and verified before the production release.
- `v1.0.0` published as the stable production baseline.

---

## [0.6.0] — Milestone 8: Beta Hardening — 2026-08-24

### Added

- Tesla Overload interactive gameplay module (`scripts/tesla_overload.lua`) with pulsing EMP lock and electrical damage against enemy buildings.
- `DEBUG_MAP_WIDE` testing mode and proximity-based radius mode.
- Dynamic `package.path` resolution so `require()` works independently of the game's working directory.

### Fixed

- `ProcessDisabledObjects` dangling-pointer validation: disabled-object entries are verified against active engine arrays before dereferencing.
- `TakeDamage` zero-health clamping: already-dead objects no longer receive additional damage interactions.

---

## [0.5.0] — Milestone 7: Extended Gameplay API

### Added

- `house:IsAlliedWith(other_house)` via `HouseClass::IsAlliedWith`.
- `obj:GetDistanceTo(other_obj)` for Euclidean map-cell distance.
- `obj:TakeDamage(n)` for direct HP reduction with clamping at zero.
- `obj:Disable(frames)` for timed building/unit/infantry disabling with automatic re-enable tracking.

---

## [0.4.0] — Milestone 6: Lifecycle & Native Object Access

### Added

- Unified `LuaAPI.Techno` userdata handle.
- Techno methods including `GetTypeName`, `GetHealth`, `GetMaxHealth`, `GetOwner`, `GetPosition`, and `IsAlive`.
- Pointer-liveness validation for exposed Techno objects.
- Global `World` namespace with building and unit enumeration.
- Cross-module `PushHouse` export.
- Initial world scanner in `scripts/init.lua`.

---

## [0.3.0] — Milestone 5: House & Economy API

### Added

- Global `House` namespace with `GetPlayer`, `GetCount`, and `GetByIndex`.
- House methods including `GetCredits`, `SetCredits`, `AddCredits`, `GetPowerOutput`, `GetPowerDrain`, `GetName`, and `IsHuman`.
- Credit changes routed through the game's native `TransactMoney` path.

### Fixed

- Added the missing `byte` typedef required by YRpp headers in new translation units.

---

## [0.2.0] — Milestone 2: Engine Hook & HUD Integration

### Changed

- Corrected the MainLoop hook target to `Unsorted::MainLoop` at `0x55D360`.
- Corrected the detour calling convention to `void __fastcall()`.

### Added

- `Engine.PrintMessage(text)` through `MessageListClass::PrintMessage`.
- First-fire hook verification logging and MinHook status reporting.

---

## [0.1.0] — Milestone 1: Runtime Foundation

### Added

- CMake Win32 build with static CRT (`/MT`) for all targets.
- `injector.exe` with remote-thread DLL injection and dynamic DLL path resolution.
- Rotating `LuaAPI.log` logging with 5 MB × 3 retention.
- Lua engine bootstrap on the main game thread.
- Redirected Lua `print` output to the project log.
- Automatic deployment of built binaries to the game directory.

---

## Versioning Notes

- `v1.0.0` remains the current **production release baseline**.
- `1.1.0` represents the current API/development line associated with Milestone 10 core, completed Milestone 11 tooling, and Milestone 12 development.
- Milestone 11 is engineering-complete, but full two-client online multiplayer validation is not claimed.
- Milestone 12 is development work and must not be described as production-complete until separately verified.
- `PROJECT/ROADMAP.md`, `PROJECT/CAPABILITIES.md`, and `API.md` should be updated alongside significant API or milestone changes to keep documentation synchronized.
