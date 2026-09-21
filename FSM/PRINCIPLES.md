# FSM — Technical Design Principles

Each principle earns its place by a concrete incident in this folder's
history (file/line refs where applicable). They are engineering rules
for runtime gameplay work on this API — not a manifesto.

## 1. Runtime state > static event list

War Clock's schedule (`tools/tmp/war_clock_test.lua`: five fixed-frame
events) executed perfectly and meant nothing, because nothing in the
match fed back into it. Command Authority's ledgers (`S.cp/dmgBank/seen/
losses/directive/retaliation`, `scripts/mods/command_authority/main.lua`)
persist, accumulate, and decay across the whole match — kills change
future options. A system that cannot name what it remembers between
frames is a script, not a system.

## 2. Gameplay loop > telemetry

War Reporter (kill feed, intensity meter, no surviving artifacts) is the
negative control: perfect observation, zero decisions. If a feature does
not change what any participant should do next, it is HUD decoration.
Budget it as such; do not balance it as gameplay.

## 3. Player decision > passive effect

War Clock applied symmetric effects on a timer (player as recipient).
Command Authority iteration 3 replaced them with priced, timed, opt-in
powers (Z/X/C/V + directives with bounties). Decisions need the full
contract — cost, timing, target, and a readable outcome (`CP -10
[reinforce] = 12`-style feedback, `main.lua` `earnCp`/`tryPower`).

## 4. Counterplay is part of the mechanic

Retaliation doctrine (`main.lua:513-519`, schedule + execute): sabotage
invites an announced, delayed, attributable answer — never a silent
unprovoked EMP (the exact War Clock failure: "random EMPs are annoying").
Iron Curtain keeps its psychic hole for the same reason
(`FSM/IRON_CURTAIN.md`). If the opponent's correct response is "wait",
the mechanic is unfinished.

## 5. AI plays the same system where possible

The Director earns/spends/retaliates under the same CP rules and costs
as the player (`directorAct`/`tryPower` shared path); remaining
asymmetry is labeling (`DIRECTOR:` prefix), not hidden mechanics.
Where symmetry breaks (MP powers lock with 2+ humans), the break is
explicit, announced, and deterministic — never a secret handicap.

## 6. Deterministic by design

Logical frames only (`Update(frame)`; never `os.time`/`os.clock` for
decisions — OOS risk, cf. `scripts/init.lua` seeding note). Same live
roster → same result: ID-ordered traversal, pure IEEE-double arithmetic
(`floor`/`sqrt`), no RNG in decisions (frontline validation,
`main.lua:433-600`). New logic is reviewed for these three properties
before it is reviewed for cleverness.

## 7. No API extension without proven necessity

The complete CA arc — economy, director, targeting RCA, Neutral
exclusion, placement validation — required zero native changes, and the
targeting RCA explicitly closed with "no extension proposed"
(`docs/research/RHINO_RCA_REVISION.md` §7). Convenience is not
necessity. The `OnPreDamage` gap is documented as a gap
(`FSM/CAPABILITIES.md`), not worked around with wishful bindings.

## 8. RCA/feasibility first, implementation second

Both CA fixes were diagnosed before being written (targeting RCA across
two research docs; placement static RCA), each with an explicit
hypothesis list adjudicated by evidence (H1–H7). Iron Curtain stays a
concept until its five feasibility checks resolve
(`FSM/IRON_CURTAIN.md` §5). Prototype to answer a question, not to
produce motion.

## 9. Harness verification ≠ live-game verification

Headless harnesses (`tools/tmp/command_authority_{test,neutral_test,
frontline_test}.lua`: 44 + 36 + 30) prove mod logic against mocks.
They do not prove engine behavior — stated in every fix report and in
`PROJECT/CHANGELOG.md` entries ("code verification only"). A harness
PASS must never be quoted as a live-game claim.

## 10. Live-game verification is recorded separately — or not at all

Runtime evidence gets its own record with session bounds
(`docs/research/rhino_rca_session_2026-09-20.log`), its own decision
tree, and per-node PASS/FAIL/INCONCLUSIVE. Silent-by-construction
effects (Neutral exclusion has no owner attribution in logs) are
recorded as code-verified/live-pending, never upgraded by enthusiasm.
User eyeball observations are evidence of the weakest grade for
ownership claims (no `GetOwner` at the eyeball) and are reconciled
against instrumented data, not averaged with it.
