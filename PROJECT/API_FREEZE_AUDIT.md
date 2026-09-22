# LuaAPI Beta API Freeze Audit (Phase 1, Step 3)

> **Date:** 2026-09-22
> **Scope:** audit only. No gameplay features added, no APIs added, no
> refactors, no Smart AI changes, no runtime behavior changes.
> **Gate 1.3:** runtime evidence still pending — status NOT changed by this audit.
> **Method:** every binding below was inventoried from source
> (`src/bindings_techno.cpp` method table + namespace registration,
> `src/bindings_house.cpp`, `src/bindings_production.cpp`,
> `src/lua_engine.cpp` `CreateEngine`, `src/barrel_pitch.cpp`
> `RegisterBindings`, `src/weapon_override.cpp`), then cross-checked
> against `API.md`, `FSM/VERIFICATION.md`, `PROJECT/GATES.md` Gate 2,
> and repo-wide consumer grep over `scripts/`.
> Evidence grades are never upgraded: source/harness ≠ live.

## Full classification table

Format: `API | Status | Evidence | Documentation | Known limitation | Beta recommendation`

### Callbacks / dispatch

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `mod.Update(frame)` | Stable | Runtime (every live session; `init.lua` pcall dispatch) | `API.md`, `README.md`, `TUTORIAL.md` match source | Logical frames only; no wall-clock | **Beta core** |
| `OnTick(frame)` global (loader) | Internal | Static (defined by `init.lua:80`, dispatched from C++) | Absent from `API.md` Callback Model | Mods must NOT define it (loader overwrites); use `Update` | Internal; document as loader-owned |
| `OnScenarioStart()` global | Blocked | Static (C++ fires at frame 1) BUT loader-shadowed (M1, below) | `API.md`/tutorial teach file-scope global that never fires | No fire on savegame load; mod handler wiped by `init.lua` | Do NOT promise; fix loader order first |
| `OnPreDamage()` global | Blocked | Static (ref collected, never invoked; no `ReceiveDamage` hook) | Honestly marked NOT WIRED in `API.md`/`README.md` | No damage-reactive mechanics possible | Do NOT promise |
| `OnUnitDestroyed()` global | Blocked | Static (unreachable dispatch branch, never invoked) | Honestly marked never-dispatched | Death detection = ID-diff polling | Do NOT promise |
| `OnDebugCommand(text)` global | Experimental | Static (wired via pcall in `ProcessDebugInput`); zero consumers | `API.md` documents form | Dev tool; last-write-wins across mods | Experimental dev tool |

### Techno reads (all on `LuaAPI.Techno` userdata)

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `GetTypeName` | Stable | Runtime (all showcases; ID-diff patterns) | Matches | Verify IDs vs live output, not art | **Beta core** |
| `GetHealth` / `GetMaxHealth` | Stable | Runtime | Matches | Snapshot per call | **Beta core** |
| `GetOwner` | Stable | Runtime (house-cache equality fix live) | Matches | Stale across matches pre-Gate-1.3-fix (fix runtime-pending) | **Beta core** |
| `GetPosition` | Stable | Runtime | Matches (256-lepton conversion) | Snapshot per call | **Beta core** |
| `IsAlive` | Stable | Runtime | Matches (+ pointer-safety warning) | Point-in-time check only | **Beta core** |
| `GetDistanceTo` | Stable | Runtime | Matches | — | **Beta core** |
| `GetId` (UniqueID) | Stable | Runtime (ID-diff death detection) | Matches | — | **Beta core** |
| `GetKind` | Stable | Runtime | Matches | `other` bucket exists | **Beta core** |
| `GetMission` | Stable | Runtime (target_reselect read-back) | Matches | Name-or-number form | **Beta core** |
| `GetTarget` | Stable | Runtime (victim-centric reselect) | Matches | Vanilla AI re-selects eventually (no lease) | **Beta core** |
| `IsIdle` | Stable | Runtime | Matches (Guard/Stop/Sleep) | — | **Beta core** |
| `IsAttacking` | Unverified | Static only; zero consumers in `scripts/` | Documented | Trivial mission read, never proven live | Promise only after one live consumer |
| `IsOnFloor` / `IsInAir` / `IsLanding` | Unverified | Static; consumer = inactive `heli_repair_test` on disk | Documented | No ledger evidence | Diagnostic; needs live proof |
| `GetVeterancy` / `GetCost` | Experimental | Static + active consumer (`bounty_hunter` v2); payout path not log-proven | Documented (Extras-2) | Reward math unverified live | Experimental |
| `GetAmmo` / `SetAmmo` | Unverified | Static; zero consumers | Documented (Extras-2) | — | Do not promise |
| `GetBaseSpeed` / `SetSpeedPercent` | Unverified | Static; zero consumers | Documented (Extras-2) | — | Do not promise |
| `GetTurretAnimFrame` / `SetTurretAnimFrame` / `GetTurretAnimFrameCount` | Experimental | Static + historic live exposure (M16 diag sessions) | Documented | Needs separate-barrel-voxel asset for visible effect | Experimental (asset-gated) |

### Techno orders / actions

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `MoveTo(x, y)` | Stable | Runtime (reselect redirects + read-back; CA; squads) | Matches | Vanilla AI competes (no order lease) | **Beta core** |
| `Attack(target)` | Stable | Runtime (8 live redirects w/ read-back) | Matches (native SetTarget path) | Buildings/MCV edge cases; no lease | **Beta core** |
| `Stop()` | Stable | Runtime (RCA probe Attack→damage→Stop) | Documented (Extras-2) | Vanilla AI reissues orders (e.g. enemy miners) | **Beta core** |
| `Hunt()` | Stable | Runtime (`SpawnUnit action="hunt"`; Smart AI live) | Matches | — | **Beta core** |
| `Scatter([x, y])` | Unverified | Static; zero consumers | Documented | Optional-arg form never exercised | Do not promise |
| `Unload()` | Unverified | Static; zero consumers | Documented (Extras-2) | — | Do not promise |
| `Deploy` / `TryToDeploy` / `Undeploy` / `CanDeployNow` / `IsDeployed` / `IsDeploying` / `IsUndeploying` | Unverified | Static; consumer = inactive `heli_repair_test` | Documented (Extras-2) | No ledger evidence | Diagnostic; needs live proof |
| `Return()` (aircraft) | Unverified | Static; zero consumers | Documented | Aircraft-only | Do not promise |
| `GetHarvestLocation` / `HarvestAt` | Unverified | Static; zero consumers | Documented (Extras-2) | — | Do not promise |
| `TakeDamage(amount, [warhead])` | Unverified | Static; zero consumers | Documented (+ real-damage warning) | Real engine damage op; warhead fallback chain unproven live | Do NOT promise as stable; Experimental at best |
| `Disable(frames)` | Experimental | Static + inactive-but-live-tested consumer (CA sabotage path) | Documented | Timed entries now reset per match (fix runtime-pending) | Experimental |
| `SetHealthRatio(percent)` | Experimental | Static + CA repair consumer; OPEN scale issue (CA passes `1.0` ≈ 1%) | Documented (0–100 + fractional warning) | Scale semantics disputed; needs own RCA before fix | Experimental; resolve scale RCA before Beta promise |
| `AttachParticleSystem(name)` | Unverified | Static; zero consumers | Documented | — | Do not promise |
| `IronCurtain(frames)` | Unverified | Static; zero consumers | Documented (kept standalone) | — | Do not promise |
| `MarkBounty` / `ClearBountyMark` | Experimental | Static + live user-observed (marks in crash session; fix verified by user, not protocol) | Documented | Draw-path crash history; vehicles/ships only | Experimental |
| `Engine.ClearBountyMarks` | Experimental | Static + Lua-side use (`bounty_hunter` restart guard) | Documented | — | Experimental |

### House

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `House.GetPlayer` | Stable | Runtime | Matches | Nil outside match | **Beta core** |
| `House.GetCount` / `GetByIndex` (0-based) | Stable | Runtime (Smart AI house scan) | Matches | — | **Beta core** |
| `house:GetName` / `IsHuman` / `IsAlliedWith` | Stable | Runtime | Matches | — | **Beta core** |
| `house:GetCredits` / `AddCredits` | Stable | Runtime (CA economy/director live) | Matches | — | **Beta core** |
| `house:SetCredits` | Unverified | Static; zero consumers | Documented | Delta-based impl | Do not promise |
| `house:GetPowerOutput` / `GetPowerDrain` | Unverified | Static; zero consumers | Documented | — | Do not promise |
| `house:SpawnUnit(...)` | Stable | Runtime (CA Director reinforcement pairs; RCA probes) | Matches (3-cell fallback radius) | Ownership/targeting semantics (Neutral); placement fallback | **Beta core** with documented semantics |

### World / game namespaces

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `World.GetUnits` / `GetBuildings` | Stable | Runtime | Matches | Snapshots; re-validate per use | **Beta core** |
| `World.GetAllUnits` | Stable | Runtime (target_reselect scans) | Matches | Whole-map cost; prefer throttled | **Beta core** |
| `World.GetUnitsInRadius` | Stable | Runtime (victim-centric SCAN) | Matches (64-bit + prefer-GetAllUnits notes) | Radius is cells; huge radii overflow-prone by design | **Beta core** |
| `World.GetAircraft` | Unverified | Static; zero consumers | Documented (Extras) | — | Do not promise |
| `World.GetSelectedUnits` | Experimental | Historic runtime (Gate 12.2) but showcasing mod ABSENT from tree | Documented; consumer pointer stale | Selection changes anytime; re-validate | Experimental; needs current-tree live proof |
| `World.GetSelectedTechnos` | Unverified | Static; zero consumers | Documented (Extras) | — | Do not promise |
| `World.GetWaypoint` / `game.GetWaypoint` | Blocked | Static: STUB — ignores id, always returns `{0,0}` (M2) | Documented as real map query — misleading | Returns origin for every id | Do NOT promise; fix or remove before Beta |
| `game.GetUnitsInRadius` | Stable-equivalent | Same C function as `World.` form | Colon-syntax headers misleading (M3) | Dot-call only | Fold into `World.`; deprecate `game.` twin |

### Engine / Game / Input / AI / WeaponOverride

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `Engine.PrintMessage(text)` | Stable | Runtime | Matches (no color arg — honest) | HUD mute flag global | **Beta core** |
| `Engine.SetBountyDrawMode` | Experimental | Static + temporary crash-diagnostic use (F6 cycling in-mod) | Documented (Extras-2) | Exists for crash isolation; remove mod-side cycling before Beta | Experimental; strip diagnostic use |
| `Engine` barrel-pitch family (Auto/Override/Persistent/Clear/GetAutoCount) | Experimental | Static + historic live (M16 sessions) | Documented (§Barrel Elevation + Extras-2) | Invisible on stock single-voxel turrets | Experimental (asset-gated) |
| `Engine.WeaponExists` | Unverified | Static; zero consumers | Documented (Extras) | — | Internal/dev |
| `Engine.SetHudMuted` / `IsHudMuted` | Internal | Static; zero consumers | Documented (Extras) | Global HUD flag | Internal |
| `Engine.version` (`"0.2.0"`) | Internal | Static | Documented (tracks code, not API version) | Value disagrees with API.md `1.1.0` by design | Internal |
| `Game.GetDebugHudText` | Internal | Static; zero consumers | Documented | Dev HUD helper | Internal |
| `Input.WasKeyPressed(vk)` | Stable | Runtime (CA hotkeys; bounty F6; tesla T historically) | Documented (edge-trigger + 256-state) | Global edge state (now reset per match — fix runtime-pending) | **Beta core** with documented edge semantics |
| `WeaponOverride.Set` / `Get` / `Clear` | Experimental | Static; zero consumers; hook degrades to vanilla w/ warning | Documented as dev/diagnostic | Per-session state; vet-key form must be read from source | Experimental dev tool |
| `AI.QueueUnit` | Blocked | Runtime: accepted-but-no-output, 2 headless runs (`FSM/QUEUEUNIT_GATE.md`) | Honestly marked BLOCKED in `API.md` | No production-director systems possible | Do NOT promise |
| `AI.CountUnit` | Unverified | Static; zero consumers; no harness | Documented (Extras-2, BLOCKED-adjacent) | Factory-queue counts only | Do NOT promise |

### Lua-side framework (`scripts/framework/`, zero native bindings)

| API | Status | Evidence | Documentation | Known limitation | Beta recommendation |
|---|---|---|---|---|---|
| `event_bus` / `timer` / `query` / `task` / `combat_state` / `tactical` / `force_group` / `unit_controller` / `util` / `persist` | Unverified (harness-only) | Harness PASS (deterministic mocks); in-game runtime OPEN per gate (14.1–14.7) | `docs/FRAMEWORK.md`, `ROADMAP.md` honest (logic-verified, runtime pending) | No live consumer for most; `unit_controller.lua` returns demo, not module | Ship as harness-verified helpers, NOT Beta-promised systems |
| `Framework.update` / `unit_created` / `unit_destroyed` emitter | Blocked (absent) | Static: `framework/init.lua` does not exist | Honestly annotated absent | Polled emitters are `combat_unit_invalidated` / `combat_state_changed` | Do not reference as API |

---

## 1. Stable Beta API list (promise-able)

`mod.Update(frame)`; Techno reads (`GetTypeName/GetHealth/GetMaxHealth/GetOwner/GetPosition/IsAlive/GetDistanceTo/GetId/GetKind/GetMission/GetTarget/IsIdle`); orders (`MoveTo/Attack/Stop/Hunt`); House (`GetPlayer/GetCount/GetByIndex/GetName/IsHuman/IsAlliedWith/GetCredits/AddCredits/SpawnUnit`); World (`GetUnits/GetBuildings/GetAllUnits/GetUnitsInRadius`); `Engine.PrintMessage`; `Input.WasKeyPressed`.

## 2. Experimental API list (usable with warnings, not promised)

`OnDebugCommand`; `GetVeterancy/GetCost`; `Disable`; `SetHealthRatio` (pending scale RCA); bounty mark family + draw mode; barrel-pitch family + turret HVA frames; `World.GetSelectedUnits`; `WeaponOverride` family.

## 3. Internal API list (exposed but not Beta surface)

`OnTick` (loader-owned); `Engine.SetHudMuted/IsHudMuted/version`; `Game.GetDebugHudText`; `Engine.WeaponExists` (dev); `game.` legacy twins (deprecate toward `World.`).

## 4. Unverified API list (implemented, never proven live — do not promise)

`IsAttacking`; `IsOnFloor/IsInAir/IsLanding/Return`; deploy family (7); `Scatter/Unload`; `GetAmmo/SetAmmo/GetBaseSpeed/SetSpeedPercent`; harvest pair; `TakeDamage/IronCurtain/AttachParticleSystem`; `house:SetCredits/GetPowerOutput/GetPowerDrain`; `World.GetAircraft/GetSelectedTechnos`; `AI.CountUnit`; all framework leaf modules (harness-only); `unit_controller` module (absent — demo file).

## 5. Blocked API list (mechanism absent or fake — must not be promised)

`OnPreDamage` (not wired); `OnUnitDestroyed` (never dispatched); `OnScenarioStart` for modders (loader-shadowed, M1); `World/game.GetWaypoint` (stub returns origin, M2); `AI.QueueUnit` (accepted, no output); `Framework.update` emitter (file absent).

## 6. Documentation mismatches (new + carried)

- **M1 (NEW, critical): loader shadows mod `OnScenarioStart`.**
  `scripts/init.lua` requires mods (line 51) and THEN defines empty
  global `OnScenarioStart`/`OnUnitDestroyed` defaults (lines 71–78),
  wiping any file-scope handler a mod set. C++ DOES fire
  `OnScenarioStart` at frame 1 — into the loader's empty default.
  `TUTORIAL.md:201–212,486` teaches exactly the broken pattern.
  Prior records ("OnScenarioStart DOES fire") are true at C++ level
  but false for the documented mod usage. Fix direction (not done in
  this audit): define defaults BEFORE the require loop or only-if-nil.
- **M2 (NEW): `GetWaypoint` is a stub documented as a query.**
  `game_GetWaypoint` ignores its id and returns `{x=0,y=0,cell=0}`
  (`src/bindings_techno.cpp:1161–1174`, "Placeholder … For now").
  `API.md` (§World + §game) presents it as a real map query.
- **M3 (NEW, minor): colon-syntax headers for dot-only functions.**
  `game:`/`World.` waypoint/radius entries are plain C functions
  (`luaL_checkinteger(L,1…)`); a colon call passes the table as arg 1
  and errors. `API.md` headers show `game:GetWaypoint(id)` while
  examples (correctly) use dots.
- **M4 (NEW, minor): `OnTick` undocumented.** The actual C++-dispatched
  global is absent from the `API.md` Callback Model; only `mod.Update`
  is shown (correct usage, but the loader layer is invisible).
- **M5 (carried): stale consumer pointers.** `API.md` cites
  `dynamic_objective_defense` (Gate 12.2) for `GetSelectedUnits` and
  `heli_repair_test` for deploy — both absent/inactive in-tree.
  Mechanism claims stand; "used live by" pointers are historical.
- **M6 (carried): `SetHealthRatio` scale dispute.** Code divides by 100
  (0–100); `API.md` documents that; CA passes `1.0`. Open RCA, no fix
  in this audit.

> **Resolution 2026-09-22 (M1: PASS, user-observed — history
> preserved):** author ran `scenario_start_probe` across three
> in-process matches: `OnScenarioStart` fired in Match 1, again in
> Match 2 after menu return, Match 3 completed; no crash, no stale
> state. No log file on disk — graded user-observed, NOT log-verified.
> `M1 Implementation: DONE`, `M1 Runtime: PASS (user-observed)`.

> **Addendum 2026-09-22 (M2 FIX IMPLEMENTED, RUNTIME PENDING — history
> preserved):** stub replaced with `ScenarioClass::IsDefinedWaypoint` /
> `GetWaypointCoords` lookup (`src/bindings_techno.cpp`); contract
> `{x, y}` or `nil`, 0-based `[0..701]`; `API.md` updated to match.
> Probe mod `scripts/mods/waypoint_probe/` added (not in default stack).
> `GetWaypoint` stays out of Beta promises until the runtime protocol
> (2+ distinct positions matching the map + `nil` for invalid ids +
> second-match re-probe) produces a fresh log.

> **Resolution 2026-09-22 (M2 GetWaypoint: PASS, user-observed — history
> preserved):** author ran `waypoint_probe` in two in-process matches:
> valid ids returned distinct correct positions, `-1`/`702` → `nil`,
> identical behavior after menu return, no crash or stale state. No log
> file on disk — graded user-observed, NOT log-verified. `GetWaypoint`
> enters the Beta-promisable surface under its documented contract.

## 7. Remaining Beta blockers from this audit

1. Gate 1.3 runtime evidence pending (unchanged by this audit).
2. M1 loader shadowing — any Beta modder following the tutorial gets a
   silently dead `OnScenarioStart`. Fix + re-verify before Beta.
3. M2 waypoint stub — remove, wire, or clearly mark non-functional
   before Beta (it is currently a documented fake query).
4. ~~`SetBountyDrawMode` F6 cycling + `VISUAL_ENABLED` diagnostics are
   temporary crash-isolation code in the DEFAULT active mod —
   strip before Beta packaging.~~ **RESOLVED 2026-09-22:** F6
   draw-mode block removed from `bounty_hunter` (isolation verdict
   was already in); native `SetBountyDrawMode` binding kept as a dev
   tool. `VISUAL_ENABLED` flag intentionally kept (default-true =
   normal behavior, documented tuning, harnessed).
5. `target_reselect` (default stack) has no restart guard (unlike the
   other four stack mods) — add or document before Beta.
6. Clean-machine install test + external modder test not performed
   (Steps 6–7) — no evidence either way.

---

## What LuaAPI HAS vs what it CAN PROMISE to Beta modders

**HAS:** ~90 Lua-exposed functions across 8 namespaces + 5 lifecycle
callbacks + a harness-verified Lua framework, on a natively hooked
YR 1.001 runtime with SEH-guarded bindings and per-logical-frame
dispatch. The observe → decide → order → read-back loop is
log-verified live in multiple systems.

**CAN PROMISE:** the §1 Stable list (≈30 functions + `Update`) —
observed, commanded, and read back in live matches with log evidence
— plus an honest Experimental shelf (§2) and an explicit Blocked list
(§5: no damage interception, no death payloads, no scenario-start for
modders until M1 is fixed, no waypoint queries, no production queue).
Everything in §4 stays out of Beta promises until one live consumer
proves it. No grade in this file was upgraded to reach that shape.
