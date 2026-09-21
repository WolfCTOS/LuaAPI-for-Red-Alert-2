# Changelog

All notable changes to LuaAPI for Red Alert 2 are documented here.

The changelog follows the project's verified milestone history. Features are listed as released or implemented only when supported by project documentation and runtime validation.

---

## [Unreleased] — Milestone 10 removal (2026-09-21)

### Removed — multi-turret & advanced combat stack

- Deleted `src/sub_turret.h/.cpp` (`SubTurretManager`: sidecar state,
  split-targets, split-salvo, spawned-missile decoupling, tracer fire,
  primary-target management, draw-hook stub).
- Deleted `src/event_hook.h/.cpp` (disabled `ActiveClickWith`
  interception; override map always empty, `Update()` no-op).
- Deleted `src/bullet_hook.h/.cpp` (`Detonate` hook; impact events were
  TRACE-logged only, zero Lua consumers).
- Removed Lua bindings: `AddSubTurret`, `GetSubTurretCount`,
  `GetSubTurret`, `SetSubTurretTarget`, `FireSubTurret`,
  `ClearSubTurrets`, `SetSplitTargets`, `FireSplitSalvo`,
  `FireProjectile`, `game.GetEventHookOverrideCount`,
  `game.ClearEventHookOverrides`.
- Unwired per-frame work: `SubTurretManager::UpdateAll()` (full-array
  `SpawnManager` sweep every logical frame), `EventHook::Update()`,
  `BulletHook` impact drain, `BulletHook::Install()` /
  `EventHook::Install()`; `ResetSession()` entries dropped.
- Kept: `IronCurtain` (standalone, synchronous), `WeaponOverride`,
  `BarrelPitch`/bounty overlay, all traps in `docs/AGENTS.md` + §9 of
  `docs/ENGINEERING_LESSONS.md` (engine facts + do-not-rehook guard).
- Reason: zero consumers in `scripts/` (verified by repo-wide grep);
  per-frame sweep + per-detonation hook overhead on the game thread.
  History preserved in git; docs (`API.md`, `README.md`, `TUTORIAL.md`,
  `CAPABILITIES.md` Case Study 4, `ROADMAP.md` M10) carry REMOVED banners.
- Verification: STATIC (code inspection) + BUILD (below). No live-match
  run for a deletion; affected Lua surface had no consumers to regress.

## [Unreleased] — Full documentation synchronization pass (2026-09-20)

### Fixed — documentation now matches source; no runtime behavior changed

- `OnUnitDestroyed`: corrected everywhere from "dispatched every frame with
  `(nil, nil)`" to never-dispatched (contract without invocation —
  unreachable C++ dispatch branch, no native death hook). Touched: `API.md`
  (Callback Model), `scripts/init.lua` comment, `docs/TUTORIAL.md` (2×),
  `README.md` (Event Model), `PROJECT/ROADMAP.md` (M7 audit note, Gates
  7.1/7.2), `PROJECT/GATES.md` (items 1/6, Gate 2 gaps), `FSM/MODDB_ALPHA_RELEASE.md`
  (2×), `FSM/FEASIBILITY_TRIAGE.md`.
- `OnScenarioStart` accuracy: it DOES fire (global, logical frame 1) —
  corrected the lumped "not wired" wording in `docs/FRAMEWORK.md`.
- Absent-module honesty: `scripts/framework/init.lua`
  (`Framework.update`/`enableUnitEvents`/`unit_created`/`unit_destroyed`)
  does not exist; actual emitters are `combat_unit_invalidated` /
  `combat_state_changed`. Corrected in `docs/FRAMEWORK.md`,
  `PROJECT/ROADMAP.md` (M14 context, Gate 14.6), `PROJECT/CAPABILITIES.md`
  (Case Study 6 recipe now uses leaf requires; no UnitController module —
  `unit_controller.lua` returns the TacticalPatrol demo).
- Absent-mod honesty: `multi_turret_battleship`, `miner_safety`,
  `tesla_overload`, `patrol_demo`, `dynamic_objective_defense`,
  `debug_console` are not under `scripts/` (stale copies of three survive
  under `build/`). Annotated in `PROJECT/CAPABILITIES.md` (Case Studies
  4/5), `PROJECT/ROADMAP.md` (Gates 10.4/12.1/12.2/14.7/14.8),
  `FSM/FEASIBILITY_TRIAGE.md`, `FSM/MODDB_ALPHA_RELEASE.md` (Small 3),
  `FSM/MODS.md` (new catalog entries), showcase docs (current-status
  notes; history preserved, nothing rewritten).
- `smart_ai` accuracy: 232-line rally + capture-guard MVP (restart guard
  present), NOT squads/officers/escorts — rewrote
  `scripts/mods/smart_ai/HOW_TO_USE.txt`; synced
  `FSM/FEASIBILITY_TRIAGE.md`, `FSM/MODDB_ALPHA_RELEASE.md` exclusions.
- README: Quick Start no longer enables inert `bounty_hunter` /
  archived `shield_overload`; table-method `OnScenarioStart` examples
  fixed to dispatched forms; `PrintMessage` color-arg removed; added an
  explicit "What Should Users NOT Expect Yet?" section.
- `PROJECT/CAPABILITIES.md` Case Study 3: recipe fixed to global callback
  + percent-scale `SetHealthRatio(35)` + archive path.
- `PROJECT/GATES.md` Gate 2: the third live-verified system is the
  (archived) `multi_force` squads, not current SmartAI.
- `docs/AGENTS.md` status: Milestone-10 record replaced with the Gate 1
  PARTIAL / feature-freeze / DU-1-next state per `PROJECT/GATES.md`.
- `PROJECT/AI_CONTEXT.md`: current research → DU-1 audit.
- `IDEAS.md` created (did not exist): Events/Queries/Lua hypothesis as
  Status IDEA / COMMUNITY REVIEW REQUIRED / Implementation NONE, with a
  created-now provenance note; no invented history.
- Verification: docs-only. Gate statuses unchanged (no PASS invented).
  `git status` shows documentation/comment/config-note edits only —
  no C++, Lua runtime, EventBus, mod-logic, or hook changes.

---

## [Unreleased] — injector.exe launcher reskin (v1.3)

### Changed — `src/injector_gui.cpp` (+`CMakeLists.txt`: msimg32), launcher only

- Brand: drawn λ monogram + `LUA ENGINE` wordmark + `v1.3` chip in the
  sidebar (was: flat bar + plain text).
- Hero: flat surface card (cover art removed per feedback — it fought the
  headline for attention); status + icon buttons kept.
- Hero actions: icon+label buttons (play / inject-arrow / CnCNet diamond
  vector glyphs, hand-drawn — no icon font dependency).
- Stat tiles: 24pt bold numbers + uppercase micro-labels.
- Mod rows (64px): hash-colored initial avatar, two-line name +
  description, version text, ВКЛ/ВЫКЛ pill, amber conflict dot on the
  avatar; author moved to the inspector.
- Inspector: 40px avatar header, version·author line, status + problems
  chips, power-glyph toggle button.
- Empty states: circled `?` / `×` marks + centered copy (mods list,
  no-results, inspector).
- Disabled buttons explain themselves: hover tooltip with the reason
  (no DLL / launch first / already injected / no unsaved changes),
  600 ms dwell, auto-hide on leave/click/wheel.
- Perf: conflict pairs computed once per list paint and passed into rows
  (was: recomputed per row).
- Verification: clean Release build, GUI startup + Dashboard/Mods
  screenshots reviewed (art, glyphs, avatars, chips, inspector all render;
  no crashes, process killed cleanly). No gameplay code touched.

## [Unreleased] — injector.exe launcher bugfix pass (v1.3)

### Fixed — `src/injector_gui.cpp` (launcher only, no gameplay impact)

- Dashboard "Problems" tile ignored mod conflicts (`return n + 0`); now
  counts conflict pairs + missing `main.lua`. Added "+N more" overflow line.
- Hero layout: the third (CnCNet) button overlapped the headline by ~150px
  even at default width. Headline now spans full width; the three actions
  share one responsive row. Taller hero, default window 1100x740, min 840x600.
- `DoLaunchGame` injected synchronously on the UI thread when the game was
  already running (visible freeze + unused handle); now reuses the async
  attach path. Launch result (incl. process name) travels in a heap
  `LaunchResult`, replacing the cross-thread `g_pendingGameName` global.
- Headless modes were broken: the GUI async launch was fired and the process
  exited ~500ms later, killing the worker before injection; `--noinject`
  (`g_skipInjection`) was parsed but never read, and `--withcncnet` behaved
  like a vanilla launch. New synchronous `RunHeadlessLaunch` /
  `RunHeadlessCnCNet` block until game exit and honour both flags.
- `SetProcessDpiAwarenessContext` was resolved from shcore.dll (always
  failed, silent fallback to system-DPI); now resolved from user32.dll.
  `g_hwnd` is set in `WM_CREATE` (first layout used fallback DPI before)
  and layout is recomputed after font creation.
- `WM_PAINT` crashed/garbled when minimized (0x0 bitmap); now guarded.
- `InjectDllIntoProcess`: null `LoadLibraryW` check, access-denied hint
  (run as administrator), and no `VirtualFreeEx` on injection timeout
  (the remote thread may still read the path buffer — freeing it risked
  crashing the game).
- `LogLine` is now thread-safe (SRWLOCK); tick counters are `ULONGLONG`
  (32-bit truncation broke the 120s/15s waits at tick wrap).
- Inspector bottom/middle buttons ran the identical "open main.lua" action;
  now bottom toggles enable/disable, middle opens `main.lua`, top opens the
  folder. Context menu deduplicated to the same three distinct actions.
- Cursor: hand shown for the CnCNet button and language switch; search /
  apply / inspector hover gated to the Mods view; mouse wheel no longer
  scrolls the hidden Mods list from other views.
- Keyboard: Up/Down move mod selection, Space toggles enable (with
  scroll-into-view), Ctrl+V pastes into search (128-char cap also applies
  to typing). Drag-reorder keeps selection on the dragged mod.
- Stale failure banners are cleared when a new launch/inject starts; failed
  launches now show the injection error text.
- Verification: `cmake --build build --config Release --target injector`
  clean, `injector.exe` redeployed to game dir, GUI startup smoke-tested
  (process alive, killed cleanly). No gameplay code touched — no match
  verification required.

## [Unreleased] — Command Authority Neutral/Special exclusion (Fix A)

### Fixed — Command Authority no longer treats Neutral/Special as combat houses

- `scripts/mods/command_authority/main.lua`: new `NON_COMBATANT =
  { Neutral = true, Special = true }` set with `isCombatHouseName` /
  `isCombatHouse` helpers, enforced at CP seeding, kill-earner selection
  (`nearestHostileUnit`), fallback kill-split, the Director spend loop, and
  retaliation schedule + execute. Neutral/Special can no longer earn CA
  command points, receive Director reinforcements/repairs, or source
  retaliation. Human/AI combat-house behavior unchanged.
- Verification: existing harness `tools/tmp/command_authority_test.lua`
  44/44 PASS unchanged; new `tools/tmp/command_authority_neutral_test.lua`
  36/36 PASS. Status: code verification only — live-game verification
  pending a real match (harness PASS is not a live-game claim).

### Fixed — Command Authority frontline spawn validation (placement fix)

- `scripts/mods/command_authority/main.lua` (`powerReinforce` + helpers):
  `lastKillPos` stays the primary frontline source, but the victim cell is
  validated before use. Enemy eco contact (harvesters `SMIN/HARV/CMIN`,
  refineries `YAREFN/NAREFN/GAREFN` within 5 cells), any enemy building
  within 2 cells, or 3+ enemy combat mobiles within 5 cells displaces the
  point ~8 cells toward own forces (existing `force=true` + terrain spiral
  handle passability); if still contested, the own-force centroid is used.
  Normal contested frontline (1–2 enemies, no eco contact) still spawns
  exactly at `lastKillPos`. Deterministic (ID-ordered, pure arithmetic, no
  RNG/clock). No `Attack`/move/teleport after spawn; `SpawnUnit`,
  targeting, LuaAPI/C++ untouched.
- Verification: new `tools/tmp/command_authority_frontline_test.lua`
  30/30 PASS (safe point, eco displacement, army displacement, normal
  frontline preserved, centroid fallback, symmetric Human case); existing
  44/44 and 36/36 suites still green. Status: code verification only —
  live-game verification pending a real match.

## [Unreleased] — SmartAI capture-aware valuables guard (MVP)

### Implemented (Lua-only, `scripts/mods/smart_ai/main.lua`)
- New guard section: high-value own units (`VALUABLE_TYPES`, default
  `APOC` + `HTNK` — the latter added after a live-observed capture)
  avoid lone exposure to observable vanilla mind-control threats
  (`CAPTURE_THREATS`: `MIND` live-observed earlier, `YURIPR` confirmed
  live this session in census, `YURI` rules/wiki ID; Hijacker ID
  unconfirmed and Chaos Drone (frenzy, not capture) excluded).
- Rule shape `valuable + hostile capturer-type within 9 cells +
  exposed (>4 from own centroid)` → single `MoveTo` toward own group
  (transition-only + 600f refresh, never yanks engaged units); threat
  clearance releases silently. Owner flips by tracked ID are logged as
  observations only (possible vanilla capture/mind-control) and drive
  no decisions. Frame-backwards restart guard included (mod-local;
  C++ `ResetSession` untouched).
- Vanilla mechanics untouched and unimplemented: no capture logic,
  no new bindings, no Ares/Phobos changes. Formulation: LuaAPI lets
  SmartAI reason about and react to existing vanilla mechanics.
- `Nuclear Truck` / `Livya` IDs do not exist anywhere in this repo
  (verified by repo-wide search) — treated as external/custom context;
  the guard is type-parameterized so those IDs slot into
  `VALUABLE_TYPES` when supplied by their defining mod.

### Verification
- Harness `tools/tmp/smartai_capture_test.lua`: 15/15 PASS (no-threat
  preservation, type-specificity incl. close-Rhino negative,
  with-group exemption, churn-free repetition, flip observation w/o
  decisions, restart reset, flank-rally regression, engaged-unit and
  out-of-envelope exemptions).
- Live (11-min headless Dannath match, SmartAI enabled, stack
  otherwise default): Scenario A PASS — zero pullback lines, match
  normal, rally intact; flip observer fired ×4 live
  (`HTNK`/`HTK`/`HTNK`/`E2` Africans→YuriCountry); `YURIPR` confirmed
  live in census. Scenario B (pullback decision) NOT triggered —
  no lone idle valuable co-visible with a capturer in-window.
- Status: implemented + source verified + harness verified; live
  PARTIAL (detection/observation live, decision live-pending). NOT
  claimed verified/live-ready. No ForceGroup exists in SmartAI
  (stability question moot); zero guard orders issued live (zero
  churn by construction); mod stack restored after the run.

## [Unreleased] — Milestone 16: Dynamic Barrel Elevation (logic complete)

### Added 2026-09-20 — automatic barrel elevation runtime (M16 logic closed)

- `src/barrel_pitch.h/.cpp`: `UnitClass::DrawAsVXL` (0x73B470) MinHook detour
  with draw-time `TechnoTypeClass::FireAngle` swap-and-restore (single-draw,
  unconditional restore); distance-based AUTO pitch (8–55° over 4–14 cells)
  computed natively per draw for player AND AI units; global switch
  `Engine.SetBarrelPitchAutoAll`, per-unit `SetBarrelPitchAuto`, manual
  `SetBarrelPitchOverride`, diagnostic `GetBarrelPitchAutoCount`;
  persistent type-field write path used for the decisive static experiment.
- `scripts/mods/barrel_elevation_diag`: fully automatic mode (user request,
  zero hotkeys) — global AUTO enabled at match start and re-asserted per
  second, per-second heartbeat with live angle samples, plus a keyless HVA
  frame-cycler walking every multi-frame-turret unit through its pose frames
  every 2 s (TS elevation mechanism via `SetTurretAnimFrame`).
- Techno bindings (Gate 1): `unit:GetTurretAnimFrame`,
  `unit:SetTurretAnimFrame`, `unit:GetTurretAnimFrameCount`; one-shot
  turret-HVA FrameCount scan logged per session (`[M16] HVA scan`).

### Verified 2026-09-19/20 — M16 evidence trail (5 sessions)

- Gate 1: HVA scan measured all 85 stock unit types — 15 voxel turrets,
  exactly ONE multi-frame (YTNK, 2 frames); key-driven frame control accepted
  and held against engine rewrites.
- Gate 2B: per-draw FireAngle swap executed 86+ times with correct values and
  zero SEH; persistent 45° type-field hold across 16+ s — **no visible effect
  on stock HTNK in either case**.
- **Root cause identified (2026-09-20):** stock YR vehicles fuse turret and
  barrel into one voxel; `FireAngle` only rotates a separate barrel voxel
  (ModEnc; Cranium [CCO]). Negative results are asset-explained, not an
  engine limit — M16 logic is complete and re-activates automatically on an
  asset with a separate barrel voxel / multi-frame turret HVA.

---

## [Unreleased] — Milestone 14: Lua Gameplay Framework + Runtime Research

### Verified 2026-09-10 — M14.1 live proof (victim-centric v2)

- `target_reselect` rewritten attacker-centric to victim-centric: HP drop on
  a player harvester/refinery triggers evaluation; AA threat scanned around
  the victim; each attacker holding it is redirected via native
  `unit:Attack` with `GetTarget` read-back; one evaluation per siege phase
  (150-frame cooldown).
- Live trail: VICTIM (hp drop) → SCAN (threat 2.00, HTK/+1.0 x2) → AA →
  8x RESELECT (HARV→SCHP, then HARV→HTNK tank), all `accepted=true` with
  matching read-back, zero errors. Cooldown spacing (160 frames) respected.
- AA table corrected against community docs + live logs: Flak Track = HTK,
  Flak Trooper = FLAKT, Flak Cannon = NAFLAK (art/UI names are not TypeIDs).
- M14.1 research half closed; M14.2 (Ares/Phobos comparison) and M14.5
  (community challenge) still open. Milestone 14 stays open.

### Fixed 2026-09-10 — match-restart freeze

- Root cause: `seekSquads` re-issued `MoveTo` to every squad member every 5
  frames with per-order INFO logging (46K+ lines/session), starving the game
  thread. Fix: destination-memory throttle + per-order `[Nav]` logs demoted
  to DEBUG + `g_lastFrame` resync on frame-counter restart. Verified: log
  stays small, no freeze across matches.
- Drive-by: 64-bit distance math in `GetUnitsInRadius` (lepton overflow).

### Added 2026-09-11 — launcher CnCNet support

- Multi-name game detection (`gamemd.exe`, `gamemd-spawn.exe`) across GUI
  detection/inject paths; vanilla launch flow unchanged.
- New hero button **CnCNet**: launches the CnCNet client/package entry
  (`CnCNetYRLauncher.exe`, `Resources/clientdx|clientxna|clientogl.exe`),
  waits for the spawned game plus hook-host modules (Ares/Phobos/
  CnCNet-Spawner, bounded, headless policy), then auto-injects.
- Verified live under Syringe + Ares + Phobos + spawner: all signatures OK,
  all hooks `MH_OK`, both mods loaded.

### Changed — runtime verification environment

- From 2026-09-11, live verification runs under the CnCNet spawner only
  (Syringe/Ares/Phobos coexistence verified 2026-09-10).

### Changed — runtime research direction (upstream, 2026-09-06)

- **Milestone 14 direction:** shifted from building a general Lua-side gameplay framework toward finding the real runtime boundary between LuaAPI and Ares/Phobos.
- **Research focus:** programmable runtime decision-making is now the primary hypothesis.
- **AI research:** target selection, information and memory, multi-AI coordination, dynamic alliances, and event-driven behaviour are candidate areas.
- **Comparison rule:** candidate behaviour must first be tested against Ares/Phobos and classified as naturally supported, workaround-heavy, or lacking a suitable existing model.
- **API strategy:** defer broad API cleanup or removal until a real runtime capability boundary is established.
- **Community validation:** concrete runtime examples should be challenged against experienced Ares/Phobos modders instead of relying on theoretical comparisons.

### Research Candidates (not verified capabilities)

- AI target selection that reacts to changing game state.
- Runtime observations with timestamps and changing confidence.
- Several AI controllers operating as one coordinated side.
- AI alliance changes during a match.
- Runtime systems such as Empowerment-style accumulation.

### Verified 2026-09-09 — Gate 14.8 SHOWCASE VERIFIED

- **Multi-force squads live:** 4 sessions, 245 recruits, 166 CONTINUE,
  125 CHANGETARGET, 138 RETREAT, 26 DISENGAGE; independent per-squad decisions,
  movement tracks, wipe-and-refill. Zero `FRAMEWORK-ERR` after the integer-cell
  centroid fix; zero neutral/civilian targets; zero MCV drafts. Consumer:
  Smart AI squads. Accepted caveat: base defense off (balance decision).

### Added — framework + first experiment (2026-09-04)

- **Runtime AI target-reselection experiment** under
  `scripts/mods/target_reselect/`: tests the architecture
  `Game state → Lua observes → Lua decides → engine executes` by reselecting an
  AI unit's target based on a live anti-air-threat signal near its current
  target, then reading back `unit:GetTarget()` to confirm the engine accepted
  the new target. Signal computed only from existing primitives (no new native
  binding); decision is generic (no unit-name branch).
  - **Verified (deterministic harness 8/8):** observe → decide → act → read-back.
  - **Remains a hypothesis (live YR pending):** whether the vanilla AI house does
    not immediately re-select its own target afterward. See
    `PROJECT/RUNTIME_BOUNDARY.md`.

- **Lua Gameplay Framework** under `scripts/framework/` — a composable,
  Lua-side abstraction layer on top of the existing native bindings (no new C++).
  - `event_bus.lua` — callback-safe pub/sub (M14.1): multi-listener,
    insertion-ordered dispatch, error isolation, safe mid-dispatch mutation,
    no retained engine references.
  - `timer.lua` — logical-frame scheduler (M14.2): `after`/`every`/`at`,
    cancellation, idempotent isolation, no native hooks.
  - `query.lua` — gameplay query helpers (M14.3): `enemies_in_range`,
    `friendlies_in_range`, `nearest_enemy`, `nearest_friendly`,
    `units_by_house`, `units_by_type`, `units_matching`, `is_enemy`, `is_ally`.
  - `task.lua` — minimal multi-step primitive (M14.4): `MoveTo`/`Attack`/`Wait`/
    `Fn` nodes, `Sequence`/`Loop` composites, task lifecycle states.
  - `unit_controller.lua` — one-unit control (M14.5): `move_to`/`attack`/
    `patrol`/`stop`/`task`/`update`, tracks the unit by id and re-resolves it.
  - `init.lua` — integration entry point (M14.6): `Framework.update(frame)`
    driver + opt-in unit event tracker emitting `unit_created` (valid userdata)
    and `unit_destroyed` (id + value snapshot).
  - `util.lua` — shared safe predicates/logging.
- **`tactical_patrol`** showcase (M14.7) demonstrating framework composition:
  patrol → detect (Query) → attack (UnitController) → resume patrol.

### Notes

- Framework logic verified with a deterministic Lua 5.4 harness (event ordering,
  timer cadence, query filtering, task lifecycle, controller engage/resume).
  In-game runtime verification against Yuri's Revenge 1.001 is pending.
- No native bindings were added, removed, or renamed; the framework is opt-in
  and additive, so existing mods are unaffected.
- M13 (native event restoration) remains open; the framework drives itself from
  `Update()` and does not rebuild the native event system.

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
