# FSM — First Serious Mod

Research archive for the first serious attempt to use LuaAPI as a **runtime
gameplay layer** (persistent, match-aware global systems with player
decisions in the loop) — as opposed to small mechanic demos or INI-style
static configuration.

## Why this folder exists

Small LuaAPI experiments (shields, bounties, turrets, patrols) each prove
one binding. None of them answers the real question:

> Can LuaAPI carry a whole match-long gameplay system — economy, AI
> opponent logic, player abilities, objectives — without native changes?

`FSM/` records that attempt honestly: what was tried, what failed and why,
what was verified at which evidence level, and which principles survived.
It is an archive, not a showcase. Marketing-style success claims do not
belong here; retracted conclusions stay visible with pointers to what
replaced them.

Related records: `FSM/VERIFICATION.md` (evidence ledger),
`FSM/CAPABILITIES.md` (capability map), `FSM/MODS.md` (mod catalog),
`docs/research/RHINO_RCA_RUNTIME.md` and `RHINO_RCA_REVISION.md`
(the targeting RCA that reshaped the project).

Related research direction: **Dynamic Unit Behavior**
(`FSM/DYNAMIC_UNIT_BEHAVIOR.md`) — the continuation of the runtime-boundary
question onto existing-unit behavior (observation/state/decisions/coordination),
with a stress-tested candidate matrix and DU-1..DU-4 research gates. Hypothesis
stage; nothing implemented.

Project-level gates: `PROJECT/GATES.md` (Gate 1 Foundation / Gate 2 Runtime
Capability / Gate 3 Alpha Showcase — the lifecycle every experiment, including
the DU track, reconciles against).

## Project status

- **Command Authority** (`scripts/mods/command_authority/`, v0.3.0) —
  ACTIVE EXPERIMENTAL. The current FSM vehicle. Harness-verified
  (44 + 36 + 30 checks); live sessions observed; two bugs found and fixed
  in Lua (Neutral-as-client, literal lastKillPos stacking). No C++/API
  changes required at any point.
- **War Reporter** — ARCHIVED. No artifacts survive in the tree (no source,
  no harness, no log references found repo-wide). Documented here from the
  experiment record only; implementation details are not recoverable.
- **War Clock** — REMOVED. Mod source is gone from `scripts/mods/`; only
  the harness `tools/tmp/war_clock_test.lua` survives, from which the
  mechanic is reconstructible (see below).
- **Iron Curtain experiment** — CONCEPT / NOT IMPLEMENTED
  (see `FSM/IRON_CURTAIN.md`). Feasibility against current API is
  explicitly unresolved in several points.

## Global gameplay mechanics already experimented

1. **Runtime combat observation** (War Reporter): kill feed, battle
   intensity tracking.
2. **Scheduled global events** (War Clock): fixed-frame symmetric events
   over ~21 minutes (supply, heal, EMP, economy, sudden-death bleed).
3. **Runtime CP economy + AI Director + player abilities + directives**
   (Command Authority): kill/damage/streak/directive income, Z/X/C/V
   powers, a Director playing by the same rules, retaliation doctrine,
   frontline reinforcement.

## Approaches that turned out bad, and why

### War Reporter — telemetry is not gameplay

- What it was (per experiment record): runtime combat observation —
  kill feed, battle intensity metering.
- Why it failed as a gameplay mod: observation without decisions. A feed
  and a meter change nothing about what the player should do next; there
  is no loop (no resource, no choice, no counterplay). It is a HUD
  feature, not a system.
- Surviving lesson: `gameplay loop > telemetry` (see `FSM/PRINCIPLES.md`).
  No code survives to re-verify; the failure mode is architectural, not
  a bug.

### War Clock — time → event → effect is a weak loop

- What it was (reconstructed from `tools/tmp/war_clock_test.lua`, the
  only surviving artifact; mod source absent): five global one-shot
  events on fixed logical frames — SUPPLY DROP 5:00 (credits), IRON
  RESERVES 8:00 (heal math), EMP STORM 10:00 (+warning, `Disable` on
  non-aircraft), WARTIME ECONOMY 15:00 (credits), SUDDEN DEATH from
  20:00 (periodic symmetric HP bleed) — applied symmetrically to both
  houses with HUD announcements. Harness-verified schedule exactness.
- Why it failed as a gameplay mod:
  - Events fire regardless of what anyone does — the player has no
    decisions (no opt-in, no cost, no timing play).
  - Symmetric effects cancel out strategically (both sides +credits,
    both sides bleed equally).
  - Unsolicited global EMP punished players for merely existing —
    playtest feedback quoted in `command_authority/main.lua` header
    ("random EMPs are annoying") directly killed this pattern and
    produced iteration 3's retaliation doctrine (EMP only as an
    announced answer to the player's own sabotage).
- Surviving lesson: `player decision > passive effect`; symmetric
  global effects have zero strategic depth without cost asymmetry.

### Command Authority, pre-fix — two real bugs found by RCA, not by review

- **Neutral/Special as combat houses (bug #1, fixed — Fix A).**
  The Director economy (`S.cp` seeded from all houses, spend loop over
  all non-humans) served the civilian `Neutral`/`Special` houses
  (`MultiplayPassive`). Neutral qualified via 11 preplaced civilian
  vehicles (`spawnmap.ini [Units]`). 7/10 observed Director reinforcement
  pairs were Neutral-owned. Their passive status made them nearly
  untargetable for player-side auto-acquire — the "Rhino in my base
  nobody shoots" symptom. Fix: `NON_COMBATANT` gate at seeding, earner
  selection, fallback split, Director loop, retaliation (Lua-only,
  `scripts/mods/command_authority/main.lua:176-188` + 5 sites).
- **Literal lastKillPos stacking (bug #2, fixed — placement fix).**
  Frontline reinforcement used the exact victim cell with `force=true`,
  so armor could materialize on top of a Harvester and instantly engage
  it. Fix: eco/army proximity validation with ~8-cell displacement
  toward own forces, centroid fallback
  (`main.lua:433-600`, `FRONTLINE_*` block). Normal contested frontline
  still spawns exactly at `lastKillPos`.
- **NOT fixed (separate open issue):** `powerRepair` calls
  `SetHealthRatio(1.0)` while the binding computes `ratio/100`
  (`src/bindings_techno.cpp:1127-1131`) — i.e. ~1% HP instead of full
  repair, contradicting `API.md`. Static finding only, deliberately
  left alone.

## Principles that emerged

Condensed here, stated technically in `FSM/PRINCIPLES.md`:

- Runtime state beats static event lists; loops beat telemetry; decisions
  beat passive effects; counterplay is part of the mechanic, not a patch.
- The AI should play the same system as the player where possible
  (Director earns/spends by the same rules — the asymmetry that remains
  is announcement labeling, not hidden mechanics).
- Deterministic by design (logical frames, ID-ordered traversal, no
  RNG/clock in decisions).
- No API extension without proven necessity — the whole CA history
  (economy, director, targeting RCA, both fixes) required zero native
  changes.
- RCA/feasibility before implementation; harness ≠ live game; live
  verification is recorded separately and never inferred.

## Command Authority — what was actually verified

(Evidence levels per claim; see `FSM/VERIFICATION.md` for the ledger.)

- **Runtime CP economy** (kill +5 nearest-hostile attribution, damage
  1-per-400 fractional bank, streak, directive +8): harness 44 checks +
  live sessions (CP lines, kill/damage awards in `LuaAPI.log`).
- **Combat attribution**: nearest-hostile + fallback split; live-confirmed
  (probe kills credited to the player at exact frames).
- **AI Director**: same-rules repair/reinforce, retaliation-only EMP —
  harness + live (`DIRECTOR:` lines, think cadence).
- **Observed unit floods are attributable**: the "20–30 Rhino" match
  observation (2026-09-20) was traced to CA Director reinforcement pairs
  (SmartAI has zero spawn capability — verified by source grep; ledger:
  `FSM/VERIFICATION.md`). Rule: attribute observed behavior only after a
  capability check of the active stack.
- **Player abilities** (Z/X/C/V, costs, blitz discount, refusal rules):
  harness; live Z-use observed via spawn correlation.
- **Directives** (alternating HUNT/DEFEND, 75 s, ±8 CP): harness full
  matrix (success/fail/expire both kinds).
- **Retaliation**: announced 5 s warning, on-time execution — harness +
  live.
- **Frontline reinforcement**: `lastKillPos` primary + centroid fallback —
  live-correlated 1:1 by pos/time (8+ pairs); placement validation —
  harness 30 checks + live `adjusted` tags (12 observed).
- **Neutral/Special exclusion**: harness 36 checks; live effect is
  silent by construction (no owner attribution in logs) — recorded as
  code-verified, live-pending.
- **Targeting RCA**: `SpawnUnit` lifecycle-complete (behavioral proof);
  player-side acquisition gated on owner-house belligerence
  (`MultiplayPassive`); manual `Attack` always works. Full record:
  `docs/research/RHINO_RCA_{RUNTIME,REVISION}.md`.
- **Multiplayer gate** (2+ humans → powers locked, deterministic
  systems only): harness; live MP unverified.
