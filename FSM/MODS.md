# FSM — Global Gameplay Mod Catalog

One entry per experiment. Status words are load-bearing:
ACTIVE EXPERIMENTAL / ARCHIVED / REMOVED / CONCEPT. A concept is never
described with implementation language (see Iron Curtain).

## War Reporter

- **Name:** War Reporter
- **Status:** ARCHIVED — no artifacts survive in the tree. Repo-wide search
  finds no source, no harness, no log or doc references. Everything below
  is the experiment record as handed down, not re-verifiable; no
  implementation claims are made.
- **Core mechanic:** runtime combat observation — kill feed, battle
  intensity metering.
- **Player interaction:** none (read-only HUD/telemetry).
- **AI interaction:** none.
- **Runtime state:** per-match observation snapshots (details unrecoverable).
- **Existing API used:** presumably world queries + HUD (unverifiable).
- **What makes it different from simple INI configuration:** nothing
  structural — and that was the finding. INI cannot do it either, but
  neither could this: observation is not a system.
- **Verification status:** none recoverable.
- **Known problems:** the concept itself — telemetry without decisions
  (`FSM/README.md`, `FSM/PRINCIPLES.md`). No code to fix; the failure is
  architectural and is kept as a negative result.

## War Clock

- **Name:** War Clock
- **Status:** REMOVED — mod source absent from `scripts/mods/`. Sole
  surviving artifact: `tools/tmp/war_clock_test.lua` (harness), from
  which the mechanic below is reconstructed. Harness asserts are quoted
  as harness facts, not live facts.
- **Core mechanic:** scheduled global events on fixed logical frames over
  ~21 minutes: SUPPLY DROP 5:00 (+credits both houses), IRON RESERVES
  8:00 (heal math), EMP STORM 10:00 (+warning; `Disable` on
  non-aircraft, aircraft immune), WARTIME ECONOMY 15:00 (+credits),
  SUDDEN DEATH from 20:00 (periodic symmetric 4-HP bleed). Symmetric
  application + HUD announcements.
- **Player interaction:** none — events fire regardless of play.
- **AI interaction:** none — both houses are passive recipients.
- **Runtime state:** schedule table + frame counter (reconstructed).
- **Existing API used (per harness stubs):** world queries, owner/kind/
  health accessors, `Disable`, `SetHealthRatio`, `TakeDamage`-equivalent
  HP math, house credits, HUD. Note: the harness stubbed
  `SetHealthRatio` with 0–1 range validation — against the CURRENT
  binding (percent scale, divides by 100) that stub would misbehave;
  the harness predates or ignores the quirk. Do not copy its HP math
  blindly.
- **What makes it different from simple INI configuration:** match-aware
  timed global effects with symmetric application and announcements —
  beyond static triggers in expressiveness, but without decisions.
- **Verification status:** harness only (schedule exactness, symmetry,
  HUD lines). No live evidence in the tree.
- **Known problems:** the loop (time → event → effect) gives the player
  nothing to do; symmetric effects cancel strategically; unsolicited
  global EMP punishes existence — the exact playtest complaint
  ("random EMPs are annoying", quoted in
  `scripts/mods/command_authority/main.lua` header) that forced
  iteration 3's retaliation doctrine. Kept as a negative result.

## Command Authority

- **Name:** Command Authority (Command Points Duel)
- **Status:** ACTIVE EXPERIMENTAL (`scripts/mods/command_authority/`,
  v0.3.0, iteration 3). Loaded in the live stack with `bounty_hunter`,
  `barrel_elevation_diag`, `target_reselect`.
- **Core mechanic:** persistent match-aware CP economy (kill +5 via
  nearest-hostile attribution with fallback split, damage +1/400
  fractional bank, survival streak, alternating HUNT/DEFEND directives
  ±8 CP) funding Z/X/C/V powers (Reinforce 10 / Repair 6 / Blitz 4 /
  Sabotage 8); an AI Director earns and spends by the SAME rules
  (repair/reinforce + announced retaliation only, never unprovoked EMP);
  frontline reinforcement at `lastKillPos` with eco/army validation.
- **Player interaction:** earn/spend decisions, timed directives with
  bounties, sabotage inviting announced retaliation, T-status readout.
- **AI interaction:** the Director is a full participant (income, repair
  triage, frontline/army-aware reinforcements, retaliation); MP gate
  locks powers with 2+ humans (deterministic systems continue).
- **Runtime state:** `S.*` ledger (cp, dmgBank, seen snapshots, losses,
  blitz, kill sites, directives, retaliation), reset on match restart;
  readable (never writable) via `AUTH._S` in harnesses.
- **Existing API used:** world queries, owner/alliance/identity,
  position, health, post-hoc damage observation, `SpawnUnit`,
  `Disable`, `SetHealthRatio` (see known problems), HUD, hotkeys,
  logical-frame timers. Zero native changes across the whole history.
- **What makes it different from simple INI configuration:**
  persistent cross-match-tick economy + attribution + an AI opponent
  inside the same rules + player decisions in the loop — the standing
  proof quoted in the mod header (Ares/Phobos are design-time
  configuration; this is a match-aware meta-layer).
- **Verification status:** 44 + 36 + 30 harness checks green; two live
  sessions analyzed frame-by-frame (RCA + placement + log review);
  MP-gate and several edges harness-only. See `FSM/VERIFICATION.md`.
- **Known problems:**
  1. `powerRepair → SetHealthRatio(1.0)` vs percent-scale binding
     (~1% HP) — open separate issue, deliberately unfixed.
  2. Tower-vs-TRUE-Arabs sample never occurred (towers unsampled in the
     RCA run); the ownership-gating model explains all 60+ mobile/
     building-threat samples with zero exceptions, but a dedicated
     tower sample remains the single most valuable follow-up IF a
     contradictory observation is ever produced.
  3. `with your forces` message covers two outcomes (no `lastKillPos`
     vs failed revalidation) — log ambiguity, no behavior impact.
  4. Total `SpawnUnit` failure (packed/OOB point) is silent in HUD
     (no charge either — safe but unannounced).
  5. First-spawn Neutral funding detail (MCV-deploy false-death
     hypothesis) unconfirmed at the log level — irrelevant post-Fix-A.

## Bounty Hunter

- **Name:** Bounty Hunter
- **Status:** INERT / HISTORICAL REFERENCE (`scripts/mods/bounty_hunter/`,
  NOT in `scripts/active_mods.txt` since 2026-09-20).
- **Core mechanic (intended):** +50 credits on combat hit via `OnPreDamage`.
- **Why inert (three independent reasons, source-verified):** table-method
  callback (global lookup only) + `OnPreDamage` never invoked + nonexistent
  `house_AddCredits` call. `Update` no-ops.
- **Decision:** `PROJECT/DECISIONS.md` (Gate 1 item 2) — removed from
  defaults, kept as reference; repair (global callback + aftermath polling)
  is a candidate future cleanup, not done.

## Smart AI

- **Name:** Smart AI Commander (`scripts/mods/smart_ai/`, v1.0.0,
  "Dynamic base flank defense and threat response"). Inactive by default.
- **Status:** ACTIVE EXPERIMENTAL (code present, 232 lines). NOT the squad /
  officer / escort version its `HOW_TO_USE.txt` still describes (that file
  is stale — see doc-sync).
- **Core mechanic:** flank-breach rally (idle reserves `MoveTo`+`Hunt`,
  every-30-frame scan) + capture-aware valuables guard MVP (transition-only
  pullback, owner-flip observation, frame-backwards restart guard).
- **Verification status:** capture guard harness 15/15
  (`tools/tmp/smartai_capture_test.lua`); live: observer fired, pullback
  decision never triggered in-window (`FSM/VERIFICATION.md`).
- **Known problems:** rally order churn unthrottled per unit while breached.

## target_reselect

- **Name:** Target Reselect (M14 experiment, `scripts/mods/target_reselect/`).
  In the default active stack.
- **Status:** ACTIVE EXPERIMENTAL, live-observed 2026-09-10 (victim-centric
  v2: 8 redirects with read-back, zero errors).
- **Core mechanic:** victim-side AA-threat signal → `unit:Attack(alternative)`
  → `GetTarget()` read-back. Cannot hold against vanilla AI re-selection
  (open boundary: `PROJECT/RUNTIME_BOUNDARY.md`).

## barrel_elevation_diag

- **Name:** Barrel Elevation Diagnostic (`scripts/mods/barrel_elevation_diag/`).
  In the default active stack.
- **Status:** ACTIVE EXPERIMENTAL — M16 live heartbeat; draw-only AUTO,
  simulation-neutral (`PROJECT/MILESTONE_16.md`). Visible effect awaits a
  separate-barrel-voxel asset.

## heli_repair_test

- **Name:** Heli Repair Test (`scripts/mods/heli_repair_test/`). Not in the
  default stack.
- **Status:** DORMANT DIAGNOSTIC — SCHP deploy-cycle probe (M16 Gate 2B).
  No live claim attached.

## damaged_fleet

- **Name:** Battle-Damaged Starting Fleet (`scripts/mods_archive/damaged_fleet/`).
- **Status:** ARCHIVED — recipe uses table-method `OnScenarioStart` (never
  dispatched); health/visual edits proven in the initial release. Convert to
  global callback + percent-scale `SetHealthRatio` before reuse.

## shield_overload

- **Name:** Shield Overload (`scripts/mods_archive/shield_overload/`).
- **Status:** ARCHIVED — uses `game_RegisterEvent` (no such binding) + a
  table-method callback the engine never dispatches. Reference only.

## spawn_test

- **Name:** Spawn Test (`scripts/mods_archive/spawn_test/`).
- **Status:** ARCHIVED diagnostic for `house:SpawnUnit` development.

## tactical_patrol / tactical_reassess

- **Name:** Tactical Patrol, Tactical Reassess
  (`scripts/mods_archive/tactical_patrol/`, `tactical_reassess/`).
- **Status:** ARCHIVED — M14 framework showcases; superseded by the Gate 14.8
  squads work. `tactical_patrol` requires the nonexistent
  `framework.init` module as written.

## god_mode

- **Name:** God Mode (was `scripts/mods/god_mode/` + archive copy). Not in the
  default stack.
- **Status:** DELETED 2026-09-21 — was DORMANT, STALE (`game_GetLocalPlayer` /
  `house_AddCredits` (no such bindings) + table-method `OnPreDamage`).
  Directory removed on request; entry kept as history.

## multi_force

- **Name:** Multi-Force (`scripts/mods_archive/multi_force/`).
- **Status:** ARCHIVED SHOWCASE — Gate 14.8 vehicle (4 live sessions
  2026-09-09: 245 recruits, per-squad decisions). Sessions stand as records;
  mod not in the active tree.

## combat_state_tracker / diagnostic_test / vet_diag / weapon_override_demo

- **Status:** ARCHIVED DIAGNOSTICS — single-purpose probes (tracker demo,
  veterancy, weapon-override). No live claims attached.

## Mods referenced by records but absent from the source tree

- `multi_turret_battleship`, `miner_safety`, `tesla_overload`,
  `patrol_demo`, `dynamic_objective_defense`, `debug_console` — no record
  under `scripts/` (stale copies of the first three survive under
  `build/Release/scripts/mods/` only). Audit/showcase documents that cite
  them stand as historical records; do not treat the citations as proof the
  mods exist today.

## Iron Curtain experiment

- **Name:** selective Iron Curtain (working title; see
  `FSM/IRON_CURTAIN.md`)
- **Status:** CONCEPT / NOT IMPLEMENTED. No code, no harness, no
  bindings touched. Feasibility checks against current API are
  explicitly unresolved (several UNKNOWNs).
- **Core mechanic (proposed):** on activation, incoming conventional
  damage reduced by 80% (unit takes 20%); psychic attacks exempt —
  the curtain shields the hull, not the minds inside.
- **Player interaction (proposed):** activation decision with
  cost/duration/cooldown (Lua-defined trigger, not the vanilla
  superweapon button — no SW API exists).
- **AI interaction (proposed):** symmetric access if feasible (same
  trigger logic for AI houses).
- **Runtime state (proposed):** ID-set of shielded units + expiry frames.
- **Existing API used:** TBD — candidates: `TakeDamage` (outgoing only),
  `SetHealthRatio` compensation (lossy), `unit:IronCurtain` (native
  FULL invulnerability — different mechanic), HP polling.
- **What makes it different from simple INI configuration:** per-unit
  runtime damage discrimination with an explicit counterplay hole
  (psychic) — not expressible in static armor/warhead config.
- **Verification status:** none (concept).
- **Known problems:** live damage interception is not wired in current
  source (see `FSM/CAPABILITIES.md` conflict note); psychic
  identification has no payload; lethal blows can't be compensated
  post-hoc. Implementation must wait for the feasibility checks in
  `FSM/IRON_CURTAIN.md`.
