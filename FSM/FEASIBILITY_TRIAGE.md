# FSM Feasibility Triage

Research-only triage for three Idea Lab candidates. No code written, no
API extended, no mods or engine touched, no bugs fixed. Statuses used:
`READY FOR PROTOTYPE` / `NEEDS LIVE CHECK` / `BLOCKED` — nothing else,
no rankings, no "best" pick.

## Purpose

Decide, per idea, what is already proven, what exists only in source,
what is unknown, and what the smallest live experiment is that closes
the gap — so prototypes start from evidence instead of assumptions.

## Current Repository Evidence

### Bindings inventory (source-verified, `src/bindings_*.cpp`)

- House: `GetPlayer/GetCount/GetByIndex/GetCredits/SetCredits/AddCredits/
  GetPowerOutput/GetPowerDrain/GetName/IsHuman/IsAlliedWith/SpawnUnit`.
- Techno (50+): full query set (`GetOwner/GetTypeName/GetKind/GetId/
  GetPosition/GetHealth/GetMaxHealth/IsAlive/GetVeterancy/GetAmmo/
  GetCost/GetDistanceTo`), orders (`Attack/MoveTo/Hunt/Stop/Scatter/
  Unload`), state (`GetMission/IsIdle/IsAttacking/GetTarget`,
  aircraft + deploy families), `TakeDamage` (outgoing through native
  pipeline), `Disable`, `SetHealthRatio` (percent-scale quirk — open
  separate issue), `AttachParticleSystem`, sub-turret set,
  `FireProjectile`, `IronCurtain` (native full invuln), `SetAmmo`,
  `GetBaseSpeed/SetSpeedPercent`, turret-HVA accessors.
- Undocumented-but-implemented (doc gaps, not API gaps): `Attack`,
  `GetMission`, plus Engine extras beyond `PrintMessage` —
  `WeaponExists`, `SetHudMuted/IsHudMuted`, a `WeaponOverride`
  `Set/Get/Clear` global, and `Game.GetDebugHudText`.
- World: `GetBuildings/GetUnits/GetAircraft/GetAllUnits/GetWaypoint/
  GetUnitsInRadius` (+ legacy `game.*` mirrors); `Input.WasKeyPressed`;
  `AI.QueueUnit/CountUnit` (factory queue; zero mod usages found).
- Absent (verified by grep, hard limits): map dimensions/radar/fog
  control, alliance switching (read-only `IsAlliedWith`), damage
  interception (`OnPreDamage` collected-never-invoked; no
  `ReceiveDamage` hook), psychic/warhead identification, superweapon
  state, production-complete events, `game_RegisterEvent` (referenced
  only by stale archive mods).

### Factory production control (source-level, for idea A)

- Lua: `AI.QueueUnit(house, typeId)` → first accepting factory wins;
  `AI.CountUnit(house, typeId)` sums `CountTotal` (`src/
  bindings_production.cpp:33-124`).
- Native: `FactoryClass::DemandProduction(pType, pOwner, shouldQueue)`
  (`third_party/YRpp/FactoryClass.h:41`), `CountTotal(pType)` (:82).
- What source does NOT say: accept/reject semantics when busy, fund
  checks, queue depth, multi-factory arbitration, production timing.
  Those live in the binary — UNKNOWN until a live probe.

### VM / session lifecycle (runtime concerns, inspection findings)

- Lua VM is created ONCE per process (`std::call_once`,
  `src/lua_engine.cpp:1148-1158`); `init.lua` (ModLoader) runs once.
- `ResetSession()` (VM teardown, house-cache clear, callback clear)
  exists but has ZERO callers — dead code. Consequences:
  - House userdata cache (`g_houseCache`) persists across matches →
    stale `HouseClass*` reads/writes on 2nd+ match in one process
    (use-after-free class risk; no multi-match crash isolated — see
    blockers doc, not asserted as root cause of anything observed).
- `smart_ai` (`lastScanFrame` module-level; frame-backwards restart guard
  present since the capture-guard MVP): after a long match it still scans on
  the next match's opening (guard resets state) — cross-match staleness
  largely addressed; order-churn discipline (rally re-issues `MoveTo`+`Hunt`
  to idle reserves while breached) still unthrottled per unit. Mod currently
  inactive by default.
- `target_reselect` (victim tables, no restart guard): stale-ID
  collision class if engine UniqueIDs recycle per match.
  - Command Authority is immune by construction (frame-backwards
    restart detection); `bounty_hunter`/`barrel_elevation_diag` are
    stateless/re-asserting.
- `OnScenarioStart`: really dispatched (global lookup at
  `CurrentFrame==1`); does NOT fire on savegame load (lifecycle caveat).
- `OnUnitDestroyed`: never dispatched in the current build — the C++ dispatch
  branch is unreachable (no queued refs; `lua_engine.cpp:1086-1146`), and no
  native death hook exists. Documented payload does not exist. Death
  detection must be ID-diff polling.
- `OnPreDamage`: global reference collected, never invoked with damage
  args (see `FSM/CAPABILITIES.md` conflict note).
- ModLoader (`scripts/init.lua`): per-mod `pcall(require)` with
  `[+]/[-]` log lines, but `Update` errors are swallowed silently
  (`local ok, err = pcall(...)`, `err` unused) — mod bugs are invisible
  in logs. Diagnostic gap, not gameplay bug.
- CnCNet attach: `injector --attach` finds `gamemd-spawn.exe`, bounded
  wait, clears state on process exit (injector_log evidence). Known-good
  path; direct Syringe launches crashed intermittently (`0xC0000005`,
  root UNKNOWN, one crash without LuaAPI — see blockers doc).

### Mods / docs honesty baseline

- Active: `command_authority` (flagship, live-tested), `target_reselect`
  (M14 experiment, live census), `barrel_elevation_diag` (M16, live).
  `bounty_hunter` REMOVED from defaults 2026-09-20 (Gate 1 item 2,
  `PROJECT/DECISIONS.md`): inert (table-method callback never dispatched +
  `OnPreDamage` not wired + nonexistent `house_AddCredits` call); kept in
  tree as a historical reference mod with a header disclaimer.
- Dormant/unverified-live: `smart_ai` (232-line rally + capture-guard MVP,
  inactive by default; NOT the "64-line reserve rally" of older notes, and
  NOT the squad/officer version its HOW_TO_USE.txt still describes),
  `damaged_fleet`/`god_mode`/`shield_overload`/`spawn_test`/`tactical_patrol`
  (archive; `god_mode` also present-but-stale in `scripts/mods/`).
  Absent from the source tree entirely (referenced by older records; stale
  copies of some survive under `build/Release/scripts/mods/`):
  `multi_turret_battleship`, `miner_safety`, `tesla_overload`,
  `patrol_demo`, `dynamic_objective_defense`, `debug_console`.
- `API.md` gaps vs source: CLOSED 2026-09-20 — `Attack`/`GetMission`
  documented; `OnPreDamage` marked NOT WIRED; `SetHealthRatio` scale
  corrected; undocumented extras (`WeaponExists`, HUD-mute, `WeaponOverride`,
  `World.GetAircraft`, `GetSelectedUnits/Technos`) now in an
  "Implemented Extras" section graded UNVERIFIED LIVE
  (`PROJECT/DECISIONS.md`). `TUTORIAL.md` stale claims fixed same day.
- `PROJECT/CAPABILITIES.md` recipes partly stale (same three issues +
  `PrintMessage` color arg).
- `PROJECT/ROADMAP.md` marked v1.0 "Production Release" done while the
  project operates as Alpha — a currency note was added to the ROADMAP
  header 2026-09-20 (historical tag; operative checklist is
  `FSM/MODDB_ALPHA_RELEASE.md`); Milestone 4/7 boxes remain checked as
  historical records with audit notes appended, not as current claims.
- Working tree is dirty (modified + untracked incl. game assets) —
  release needs file/commit hygiene (see ModDB doc).

## Counter-Composition Director

### Required Capabilities
Observe enemy composition by type; choose counters (Lua table);
request production for the AI house; verify units appear, exit the
factory, and affect gameplay; order the wave (`Attack`/`MoveTo`);
track results by ID.

### Verified
Composition counting (`GetTypeName` tallies), wave orders
(`Attack/MoveTo/Stop`), ID tracking + re-resolution, frame timers,
nearest-hostile-style attribution patterns — all FSM-VERIFIED via
CA/RCA/harness. Decision math itself is harness-provable.

### Unknown
Everything about `DemandProduction` past its signature: accept/reject
when busy or broke, queue depth and ordering, multi-factory
arbitration, production timing, and whether `CountTotal` reflects
queued vs active units. These are semantic unknowns, not missing
bindings.

### Live Micro-Gate (2026-09-20 session, details: `FSM/QUEUEUNIT_GATE.md`)
First-ever live calls, temp probe mod (removed after the run),
headless Africans-vs-YuriCountry match on Dannath Revisited:
- Factory found: Soviet `NAHAND` barracks (Africans) at frame 900.
- `AI.QueueUnit` infantry + vehicle + immediate repeat: all accepted
  (`true` × 3). `AI.CountUnit` read 1 once at request frame, then 0
  for all 201 polls over ~3.4 min. Zero requested units appeared
  (ID-scan appearance detector, type+owner matched); house credits
  stayed ample (98k → 88k drift = AI's own spending).
- Faction-mismatch caveat (limits the verdict): the accepted types
  were Yuri IDs (`INIT`, `LTNK`) requested from a Soviet house — the
  probe took the first acceptance instead of faction-authentic types.
  So this run proves **acceptance performs no producibility
  validation**, but cannot separate "accept-then-sink" from
  "mismatched-type drop".
- Sharper retest specified (not run): derive faction-authentic types
  from the AI's live fielded army, request late-match from a mature
  base, census AI production in-window. That test — not this one —
  decides between sink and mismatch-drop.

### Live Micro-Gate v2 (same day, details: `FSM/QUEUEUNIT_GATE.md`)
Faction-authentic retest, temp probe v2 (removed after the run):
`NAHAND` + `NAWEAP` mature base; requested the AI's own fielded types
(`E2` infantry, `HTNK` vehicles); AI-production census proved factory
output working in-window (`HTNK` churn 26–30). Result: accepted, yet
zero `E2` appearances (clean channel — AI builds no E2 itself),
`CountUnit` steady 0/0, ample funds. `HTNK` channel self-polluted by
AI mass production (excluded from evidence either way). Sink vs
AI-scrubbing is unobservable live, but both equal pipeline failure
for any consumer — hence `BLOCKED` for the production dependency
(see Status). Optional reopen: single attributed `APOC`-class
appearance (not run).

### Risks
- Engine-side blockers: none structural (this IS the native factory
  path); risk is semantic (a `true` return that never materializes, or
  silent queue drops).
- CPU: periodic composition scans — same cost class as CA scans.
- Determinism: decisions ID-ordered (proven pattern); production
  timing is engine sim-state, synced given synced inputs.
- Multiplayer: production requests are sim writes — same gating
  discipline as CA powers until proven otherwise.

### Minimal Live Test
One AI house, one known player composition (e.g. 5×LTNK): observe →
choose documented counter → `QueueUnit` returns true → `CountUnit`
rises and/or the unit appears → exits factory into `Guard` → scores
a kill or damage. Six links, each log-assertable; any broken link
names the exact unknown that failed.

### Status
`BLOCKED` — for the production dependency. The sharper retest ran
(`FSM/QUEUEUNIT_GATE.md`, v2): faction-authentic `E2`/`HTNK` requested
from a mature Soviet base with working factory output in-window —
accepted, zero attributable output (`E2` clean-negative; `HTNK`
self-polluted by AI mass production). Whether accept-then-sink or
AI scrubbing, the Director-usable contract (request X → X appears)
does not hold. Decision layer stays harness-ready; reopen only on a
positive attributed appearance (specified APOC tiebreak, not run).

## Aegis Detail

### Required Capabilities
`MoveTo/Attack/Stop`, `GetTarget/GetHealth/GetPosition/
GetUnitsInRadius`, unit IDs + lifecycle discipline, order throttling,
`UnitController`-style follow/engage/reassign/replace.

### Verified
Every primitive is FSM-VERIFIED (probe `Attack`→`Stop` release with
exact-frame asserts) or REPO-VERIFIED (`UnitController` tracks by ID
with re-resolution). Change-detection getters (`GetMission/
IsAttacking/GetTarget`) allow order-only-on-change.

### Unknown
No structural unknowns — only tuning: follow distance, re-task
hysteresis, scan cadence. (Tuning is not a capability gap.)

### Risks
- Command churn: NO built-in throttle primitive exists; callers must
  implement cadence + change-detection (SmartAI's rally re-issues
  `MoveTo`+`Hunt` to idle reserves every scan while breached; the capture
  guard is transition-gated — see `scripts/mods/smart_ai/main.lua`).
- Scan cost: per-detail radius scans; bounded by cadence, same class
  as CA.
- Target-acquisition interference: the engine re-selects targets
  under ordered units (target_reselect lesson) — assignments need the
  RCA-probe release discipline, not fire-and-forget.
- Dead/recreated handling: ID re-resolution pattern covers it;
  cross-match staleness needs a CA-style restart guard (SmartAI
  precedent).
- Determinism/MP: standard (ordered traversal, frame timers, gated
  writes).

### Minimal Live Test
Assign a 2-tank detail to a harvester → drive a threat at it →
intercept observed (`Attack` + target lock in log) → kill one guard →
replacement assigned, order rate counted from log lines (churn
metric). Pass = protection works AND orders/frame stays bounded.

### Status
`READY FOR PROTOTYPE` — harness-first (relationship state machine),
then live for feel and churn numbers.

## Sector Command

### Required Capabilities
Position APIs, play-area extents, `GetUnits`/`GetUnitsInRadius`,
ownership, kill attribution, credits, HUD, AI objective weighting.

### Verified
All except extents are FSM-VERIFIED (queries, attribution, credits,
HUD, Director-style AI weighting patterns).

### Unknown
No structural unknowns. Map dimensions have no binding — sectors must
be relative (min/max of live positions, or waypoint-anchored), which
is deterministic given synced engine state. Sector granularity and
bonus curves are tuning, not gaps.

### Risks
- No radar/fog API (verified absent) — sector bonuses MUST be
  credits/forces/access, never vision. Fake vision is explicitly
  out (do not propose equivalents).
- HUD spam and bonus snowball (decay + recalc cadence required).
- Determinism/MP/CPU: standard patterns apply.
- Engine-side blockers: none (pure Lua aggregation over verified
  queries).

### Minimal Live Test
3–5 sectors → control computed from presence/kills → displayed →
forced transfer (scripted raid) → reward paid to new owner →
Director re-weights objectives toward a high-value sector. Each link
log-assertable.

### Status
`READY FOR PROTOTYPE` — harness-first (control math), then live.

## Cross-Idea Findings

- All three share the proven substrate: ID tracking, cadence timers,
  order release discipline, restart guards, deterministic traversal.
  None requires API extension as specified.
- The only structural unknown in the set is production acceptance
  (idea A); B and C carry tuning unknowns only.
- Common MP rule until proven otherwise: sim writes stay
  single-player-gated like CA powers.
- Common hygiene both B and C must include from day one: CA-style
  frame-backwards restart guard (SmartAI precedent) and order-rate
  logging (churn is the known failure mode of follow/assign systems).

## Recommended Next Experiments

Dependency order, not a ranking — all three stay candidates:

1. `QueueUnit` acceptance micro-probe (one AI house, one requested
   type, log `QueueUnit` return + `CountUnit` trajectory + first
   factory exit). Unblocks idea A; tiny, decisive, no full mod needed.
2. Aegis prototype (harness relationship machine → live detail with
   order-rate metrics).
3. Sector prototype (harness control math → live 3–5 sector map with
   transfer + AI re-weighting).
