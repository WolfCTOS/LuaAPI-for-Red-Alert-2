# Changelog — 27.11.2026

Scope: the LuaAPI repository (`main`) and the deployed game build. This entry
records the retirement of research/diagnostic mods and their custom bindings,
plus the repair of the one mod that remains.

## Summary

- **Removed:** 10 research/diagnostic mods; 30 Lua bindings; 2 C++ diagnostic
  modules; stale diagnostic includes.
- **Kept:** `smart_ai` — the only shipped mod.
- **Added:** no new API bindings. This is a deliberate API reduction.
- **Goal:** make LuaAPI look and behave like Ares/Phobos — engine configuration
  and native verbs, not a second gameplay engine written in Lua. All removed
  mods were experiments probing things Ares/Phobos either already provide via
  INI or do not need at all.

## Removed mods

| Mod | What it was | Why removed |
|---|---|---|
| `barrel_elevation_diag` | M16 draw-only barrel-pitch / HVA frame probe | Visual experiment; no gameplay value, required 13 custom bindings |
| `bounty_hunter` | Bounty-target overlay with `MarkBounty` | Research mod; bounty mechanics belong in Ares/Phobos rules, not Lua draw hooks |
| `command_authority` | CP economy, directives, Director powers | Meta-system with its own scoreboard binding; outside the intended API role |
| `dynamic_fow` | Fog-of-war PoC with shroud-cell bindings | Engine visual state should be owned by the engine/draw layer |
| `heli_repair_test` | SCHP deploy-cycle diagnostic | One-off test, no lasting consumer |
| `radiation` | Weather hazard / RadSite orchestration | 11 radiation bindings plus `DetonateAt`; Phobos already owns radiation |
| `scenario_start_probe` | Loader event probe | Beta scaffolding, no longer needed |
| `target_reselect` | AI target-reselection experiment | Research-only; uses generic verbs that remain |
| `tesla_mcv` | Tesla MCV spawn verifier | One-off Gate 1-2 tooling |
| `waypoint_probe` | Waypoint lookup verifier | One-off probe; `GetWaypoint` removed with it |

`scripts/active_mods.txt` now contains only `smart_ai`.

## Removed API bindings

All 30 removals are from the public Lua surface. No replacement bindings were
added.

### Barrel / overlay (from `barrel_elevation_diag`, `bounty_hunter`)

- `Engine.SetBarrelPitchAutoAll`, `Engine.GetBarrelPitchAutoCount`,
  `Engine.GetBarrelPitchAuto` — global AUTO pitch switch and counters. Draw-only
  experiment driven from Lua; the engine owns its voxel draw path.
- `unit:GetTurretAnimFrame`, `unit:SetTurretAnimFrame`,
  `unit:GetTurretAnimFrameCount` — HVA pose-frame access used by the frame
  cycler. Rewriting turret animation state from Lua is not an engine feature.
- `unit:MarkBounty`, `unit:ClearBountyMark` — bounty overlay drawing. The
  overlay lived in `src/barrel_pitch.cpp`, so removing the module requires
  removing its Lua surface as well.

### Command Authority

- `Game.SetModHudText`, `Game.GetModHudText` — persistent green scoreboard line
  plus its `DrawModHudText` renderer. Only the CP economy used it.

### EMP / radiation / deploy diagnostics

- `unit:Disable` — timed EMP lock (`DisableEntry`, per-frame expiry and session
  reset). Used by Command Authority and Radiation; no remaining consumer.
- `World.DetonateAt`, `World.DetonateAtFromUnit` — radiation-bullet detonation
  used to make Phobos build a `RadSiteClass`. Removed with the radiation mod.
- `World.GetCellRadLevel`, `World.SetCellRadLevel`, `World.IsCellRadiated`,
  `World.RadSiteCreate`, `World.RadSiteSetLevel`, `World.RadSiteSetEnabled`,
  `World.RadSiteSetLight`, `World.RadSiteHasZone`, `World.RadSiteList`,
  `World.RadSiteProbe`, `World.RadSiteNativeEnabled` — all RadSite / cell
  radiation probes and writers. Radiation is an engine/Phobos concern.
- `unit:CanDeployNow`, `unit:TryToDeploy`, `unit:Undeploy`,
  `unit:IsDeploying`, `unit:IsUndeploying`, `unit:IsOnFloor`, `unit:IsInAir`,
  `unit:IsLanding` — deploy-state diagnostics used by heli/radiation/tesla
  probes. `unit:Deploy` and `unit:IsDeployed` were kept: `IsDeployed` is used by
  Smart AI to protect deployed MCVs.

### Dynamic fog / probes

- `DynamicFow` namespace bindings and its hooks — removed with
  `src/dynamic_fow_poc.cpp` (the whole PoC module). Generic shroud/fog read
  helpers in `bindings_techno.cpp` (`GetFogState`, `SetCellShrouded`,
  `FlushShroudRedraw`, `NativeReshroud`, `GetSight`) were **kept**: they are
  read-only engine diagnostics with no remaining mod-specific writer.
- `game:GetWaypoint` — used only by `waypoint_probe`.
- Stale `#include "fog_gap_probe.h"` and its `FogGapProbe::Install()` call — the
  header was not present in the repository, so this was also required for a
  clean build.

## Removed C++ files

- `src/barrel_pitch.cpp`, `src/barrel_pitch.h` — barrel-pitch detour and bounty
  overlay renderer (1071 + 85 lines).
- `src/dynamic_fow_poc.cpp` — fog override PoC (2254 lines).

## Added / changed

- English `README.md` and `scripts/mods/README.md`: they now state that **Smart
  AI is the only mod shipped today**, and that **NiTeMind** will add more mods in
  the future and expand the description as they arrive.
- Installation tree in `README.md` reduced to `mods/smart_ai/` only.
- `scripts/active_mods.txt` reduced to `smart_ai`.
- `.gitignore` now excludes local build directories (`build/`, `build-win32/`).
- No new Lua bindings and no behavior changes to the remaining API.

### Deployed game build (outside the repository)

The game installation's `mods/Smart AI/` was repaired and compacted:

- Fixed the load failure `main.lua:1275: too many local variables (limit is
  200)`: 69 top-level locals were grouped into one table `STX`; the main chunk
  now has 168 locals. Module loads and `SmartAI.Update()` runs cleanly.
- Reduced `main.lua` from 6346 to 3313 lines by moving long comment blocks into
  `NOTES.md` (185 sections), collapsing blank runs and folding continuation
  lines. The token stream is identical to the original, so behavior is
  unchanged.
- `mod.json` entry now points to `main.lua` instead of the stale `smartai.lua`.
- Mods and bindings removed from the repository were also purged from legacy
  source copies (old builds, worktrees, local checkouts).

## Verification

- Win32 Release build: **EXIT 0** (`cmake --build ... --config Release`).
- Removed binding names in the resulting DLL: `SetModHudText`, `MarkBounty`,
  `Disable`, `DetonateAt`, `RadSiteCreate`, `GetTurretAnimFrame`, `DynamicFow`,
  `GetWaypoint`, `IsOnFloor`, `CanDeployNow`, `SetBarrelPitchAutoAll` — **0 matches**.
- `require('main')` for `smart_ai` with the framework: **OK**.
- Diff for the cleanup: 33 files, **−13 563 lines**, +12.

## Git history

- PR #31 `d0d0b29` — remove research mods and their custom bindings.
- PR #32 `de1a82e` — English README / Smart AI-only statement.
- Merged topic branches `cleanup/keep-smart-ai`, `docs/english-readme-smart-ai`,
  `WolfCTOS-patch-1..4` were deleted after merging.
