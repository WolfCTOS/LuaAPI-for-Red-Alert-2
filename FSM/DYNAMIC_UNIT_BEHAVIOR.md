# 🔬 Dynamic Unit Behavior — Research Direction (FSM/DU)

> **Status:** READY FOR RESEARCH — hypothesis stage. Nothing implemented, nothing verified.
> **Created:** 2026-09-20 (feature-freeze reset).
> **Parent gates:** `PROJECT/GATES.md` (DU-1..DU-4 reconciliation).
> **Relationship to the M14 boundary research:** this is the continuation of the
> question in `PROJECT/RUNTIME_BOUNDARY.md` and `PROJECT/AI_CONTEXT.md`
> ("what can Lua make programmable at runtime that Ares/Phobos do not naturally
> model?"), focused on **unit behavior** rather than one target-selection decision.

## Hypothesis

> LuaAPI can make existing RA2/YR units behave differently at runtime — using
> only their existing capabilities — through runtime observation, persistent
> state, memory, decision-making, orders, coordination, adaptation, and feedback
> loops.

This is a design **boundary**, not a superiority claim: the value of a candidate
behavior is what Lua's *programming model* (live-frame predicates over arbitrary
runtime state, accumulated memory, coordination between units) adds on top of
unit *definitions* that Ares/Phobos extend.

## The boundary (design, not a claim)

```text
Ares / Phobos                          LuaAPI
────────────────                       ──────────────────────────────
unit definition                        runtime observation
weapons / warheads                     runtime state & memory
effects / AttachEffects                decision making
engine extensions                      orders & coordination
intrinsic mechanics                    adaptation & feedback loops
per-type INI configuration            multi-unit emergent behavior
```

A candidate behavior belongs to this direction only if its *core value* is on the
right-hand side. If removing Lua's runtime layer leaves the mechanic meaningful
(per-type config, stat change, or static trigger), it belongs to Ares/Phobos and
is out of scope.

## Explicit exclusions (NOT the direction)

The showcase must not be primarily about:

- new weapons or warheads;
- AttachEffects / new effect stacks;
- new intrinsic unit abilities (the concept is existing units, existing abilities);
- simple stat buffs or balance changes;
- mechanics whose main value already belongs to Ares/Phobos (per-type shields,
  per-type interceptors, radiation, spawn-on-fire, …).

These may appear as incidental implementation details; they may not be the point.

## Desired LuaAPI layer

- runtime **observation** (world, combat, ownership, threat)
- runtime **state & memory** (accumulated, decaying, timestamped belief)
- **decisions** (arbitrary predicates over runtime state, per unit and per group)
- **orders** (existing primitives: Attack/MoveTo/Stop/… with known persistence limits)
- **coordination** (multi-unit, multi-group, role assignment)
- **adaptation & feedback** (outcomes change future decisions)
- **emergent multi-unit behavior** (the showcase class)

## Candidate behavior matrix

Evidence grades: `LIVE VERIFIED` / `HARNESS VERIFIED` / `SOURCE VERIFIED` /
`PARTIAL` / `UNKNOWN`. Ares/Phobos overlap is a factual field, not a rejection:
overlap does not automatically disqualify — the test is whether the *runtime
state/decision/coordination* layer remains LuaAPI's contribution. No winner is
selected here; selection happens at DU-1 exit, from evidence.

Overlap legend — **V** vanilla, **A** Ares, **P** Phobos: `none` / `partial`
(approximable with workarounds) / `full` (natural mechanism exists). `UNKNOWN`
means not yet confirmed against authoritative docs — must be resolved in DU-1.

| # | Unit | Behavior hypothesis | V | A | P | LuaAPI primitives required | Current verification | Unknowns | Potential test | Showcase value |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Rhino/HTNK | formation & regrouping; attack/retreat decisions as a group | none* | partial | partial | queries, position, orders, group state, IsAttacking | orders/orders-lifecycle LIVE VERIFIED (CA/RCA); formation itself untested | order persistence vs AI re-selection; churn control | 4-tank wedge vs enemy probe; regroup on casualty threshold | high — familiar unit, visible formation |
| 2 | V3 | positioning & target selection (stand-off firing) | partial (vanilla AI keeps range) | partial | UNKNOWN | position, target, GetMission, orders | position/target observation LIVE VERIFIED; V3-specific untested | vanilla range-keeping interference | force V3 to reposition at defined standoff vs vanilla | medium — behavior partially vanilla |
| 3 | Terror Drone | pursue / disengage / retarget state machine | none (vanilla: fixed Locomotor/Emerge logic) | partial | partial | queries, health, target, orders, state machine (Lua) | orders + state machines HARNESS/LIVE VERIFIED (CA patterns); TD-specific untested | infestation-state observability from outside | scripted pursue→disengage-at-HP→retarget cycle | high — dramatic state machine |
| 4 | Apocalypse | battlefield positioning & coordination (frontline holding) | none | partial | partial | queries, position, cost, orders, group roles | same as #1 | same as #1 | 2-Apoc hold-line vs waves; withdrawal rule | high — heavyweight visible |
| 5 | Kirov | route → strike → retreat → reassess lifecycle | none (vanilla Kirov never retreats) | partial | UNKNOWN | position, target, health, orders (locomotion limits?) | position/health/orders LIVE VERIFIED; Kirov locomotion behavior UNKNOWN | can Kirov be re-ordered at all (vanilla mission logic may resist) | scripted lifecycle over vanilla Kirov | high — subverts known behavior |
| 6 | Prism Tank | coordinated targeting (focus fire / spacing) | none | partial | partial | queries, radius, target, orders, cooldown state | focus-fire = same primitives as #1 | firing-line geometry churn | 3 Prisms focus-fire priority target in range order | medium-high — visually legible |
| 7 | Mirage Tank | adaptive positioning (ambush selection) | none | partial | UNKNOWN | position, queries, threat scan, orders, memory | all primitives LIVE VERIFIED; Mirage-specific untested | decoy-state observability | pick ambush cell by threat memory, hold until trigger | high — emergent & thematic |
| 8 | IFV | runtime behavior based on battlefield context (passenger/role logic) | partial (combo weapon = type config) | full** | partial | queries, passenger/role observation, orders | passenger composition observation: UNKNOWN (no passenger API in bindings) | how to read loaded infantry w/o new binding | IFV posture change by observed context | low-medium — likely Ares territory |
| 9 | (any) Harvester | threat-avoidance memory (route around kill sites) | none | partial | UNKNOWN | position, kill-site memory, orders | memory + orders LIVE VERIFIED (CA lastKillPos is the same ledger pattern) | vanilla harvest order interference (miner_safety lesson) | harvester avoids 3-cell radius of last kill site | medium — subtle |
| 10 | (any) squad | shared targeting memory (don't re-attack what just killed 2 allies) | none | partial | partial | death observation (disappearance diff), per-type loss ledger, target orders | loss-ledger + disappearance diffing LIVE VERIFIED (CA); decision on top untested | attribution noise | squad refuses target type after N losses for T frames | high — pure decision layer, zero overlap risk |

\* Vanilla has loose group-move behavior; not formation logic.
\** Phobos IFV combos are per-type config (the exact boundary we exclude).

### Reading the matrix

- Rows 1, 3, 4, 7, 10 currently look strongest on the **boundary test**: their
  core is a runtime decision/state layer with no natural per-type mechanism.
- Row 8 is the weakest (most likely Ares/Phobos territory) and row 2 is
  partially vanilla already.
- Nothing here is selected. DU-1 (below) resolves the UNKNOWNs first.

## Stress test against Ares/Phobos (per-candidate questions)

For every candidate, before it may enter DU-2:

1. Can vanilla RA2/YR already do it? (mission logic, TeamTypes, ScriptTypes)
2. Can Ares do it? (per-type flags, AI extensions, MindControl-related logic)
3. Can Phobos do it? (AttachEffect conditions, behaviors, shields, etc.)
4. Can Ares/Phobos *scripting* approximate it? (map triggers, TeamType scripts)
5. What exactly would LuaAPI contribute? (must be: runtime state / decision /
   coordination — not configuration)
6. Would the behavior still demonstrate LuaAPI if Ares/Phobos were present in
   the game? (it must — they are always present in the live stack)
7. If the answer to #5/#6 is weak → **reject** the idea, do not soften it.

## Proposed research gates (reconciled with PROJECT/GATES.md)

### Gate DU-1 — Capability audit (research-only; may start during feature freeze)

- Resolve `UNKNOWN` cells in the matrix against authoritative sources
  (ModEnc/Ares docs/Phobos docs/YRpp), not memory.
- Confirm primitive coverage for the shortlisted candidates against
  `FSM/CAPABILITIES.md` (all listed primitives are already LIVE VERIFIED —
  DU-1 verifies the *mapping*, not the bindings).
- Apply the 7-question stress test to each shortlisted row; reject weak ideas
  explicitly (with reasons) in this file.
- **Output:** one selected unit + one behavior + falsifiable success criteria;
  this file updated; no code.

### Gate DU-2 — One unit + one runtime behavior

- Standard pipeline: hypothesis → gate → source audit → implementation →
  harness → live verification → evidence grade → doc update.
- Entry condition: Gate 1 documentation items closed (`PROJECT/GATES.md`) +
  DU-1 exit artifact exists.
- Exit: the behavior is LIVE VERIFIED or the negative result is recorded here.

### Gate DU-3 — Multi-unit coordination / feedback

- Same pipeline; adds coordination/memory/feedback across units (the CA
  Director and Gate 14.8 squads are prior art, not obstacles).
- Exit: multi-unit behavior LIVE VERIFIED with decision-loop evidence.

### Gate DU-4 — Showcase

- The DU-2/DU-3 success is packaged per the Gate 3 checklist
  (`FSM/MODDB_ALPHA_RELEASE.md`): reproduction, install, media, limitations,
  Ares/Phobos boundary statement, evidence-graded claims.

## Anti-patterns (from project history — binding)

- Do not present observation as gameplay (War Reporter lesson).
- Do not add unsolicited, unannounced negative effects to the player (War Clock
  EMP lesson; player decision > passive effect).
- Do not write unit-name special cases into decisions — rules must be generic
  predicates over runtime state (target_reselect lesson).
- Do not assume an order persists — verify against vanilla AI re-selection
  (`PROJECT/RUNTIME_BOUNDARY.md` second-half boundary; miner_safety lesson).
- Do not attribute observed behavior to SmartAI (or anything) without evidence —
  the 20–30-Rhino case was Command Authority Director reinforcements, not SmartAI
  or factory production (`FSM/VERIFICATION.md`, attribution note 2026-09-20).
- Harness PASS ≠ live claim (`FSM/PRINCIPLES.md` §9).

## Status

| Item | Status |
|---|---|
| Direction defined | ✅ this file |
| Candidate matrix | ✅ hypotheses recorded, no winner |
| DU-1 capability audit | ⬜ not started (may start now — research only) |
| DU-2 implementation | ⬜ blocked on Gate 1 doc reset + DU-1 exit |
| Ares/Phobos boundary statement | ⬜ per-candidate at DU-1; draft rule above |
