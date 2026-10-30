# SmartAI Roadmap

> Mod-owned roadmap (NOT part of the LuaAPI milestone namespace —
> see `AGENTS.md` Milestone Architecture). The operative milestone file
> is `milestones/M2_DECISION_SYSTEM.md`; this document is the direction.

## Vision

SmartAI is a runtime decision layer for the vanilla RA2/YR AI.

It does not replace the existing AI production, base construction, TaskForce, TeamType or wave systems.

Instead:

**Vanilla AI produces and deploys forces.
SmartAI observes the live battlefield and decides how existing forces should be used.**

The goal is not to make the AI stronger through economic cheats or larger armies.

The goal is to make the AI **reactive, coordinated, memorable and strategically adaptive**.

---

# M1 — Reactive AI

Status: Completed / established foundation

Goal:

Give the AI the ability to observe live battlefield conditions and react to them.

Core capabilities:

* live threat detection;
* point defense;
* target reselection;
* AA-aware targeting;
* tactical retreat;
* escort behavior;
* garrison screening;
* valuables protection;
* recall;
* surrender based on actual strategic defeat.

Core architecture:

`Observation → Tactical evaluation → Action → Readback`

Important principle:

SmartAI reacts to the current state instead of relying exclusively on pre-scripted behavior.

---

# M2 — Decision System

Status: In progress

Goal:

Move from isolated reactive behaviors to an actual decision-making system.

M2 introduces autonomous decisions, coordination between behaviors and persistent battlefield memory.

## C1 — Belief / Memory

Status: IMPLEMENTED + HARNESS VERIFIED 2026-09-25 (grudge slice only).
Live verification: NEEDED — not yet performed (a crate-elite rush that
destroys the base outright does not count: C1 needs repeated economy
attacks with `BELIEF_EVENT` → `BELIEF_EFFECT` in the log).
Harness: `tools/tmp/smartai_belief_test.lua`
13/13. Details: `milestones/M2_DECISION_SYSTEM.md` progress log.

Goal:

Give SmartAI memory of what happened earlier in the match.

Initial capabilities:

* grudge tracking;
* repeated attack detection;
* remembered hostile directions;
* remembered destroyed valuable units/buildings;
* historical threat weighting.

Core loop:

`Event → Memory → Updated belief → Future decision`

Example:

Player repeatedly attacks one side of the AI base.

SmartAI remembers this and increases the strategic importance of that direction.

Acceptance principle:

Memory must affect a later decision.

A table containing historical data without changing behavior does not count as completed.

---

## C2 — Decision Arbiter

Status: IMPLEMENTED + HARNESS VERIFIED 2026-09-25 (tick-claim registry,
priority = file order; live verification: NEEDED).
Harness: `tools/tmp/smartai_arbiter_test.lua` 6/6.
Details: `milestones/M2_DECISION_SYSTEM.md` progress log.

Goal:

Create a single authority for deciding what each force should currently be doing.

Potential competing behaviors:

* Defense
* Raid
* Escort
* Recall
* Garrison
* Rally
* Hold

The arbiter prevents different SmartAI modules from fighting over the same units.

Concept:

`Current State → Utility / Priority → Role Assignment → Force Action`

Example:

If the base is under heavy attack, available forces that would normally raid may instead be retained for defense.

When the threat disappears, the same forces may become eligible for a raid.

Acceptance principle:

Multiple SmartAI behaviors may exist simultaneously without producing persistent order conflicts.

---

## C3 — Autonomous Raider

Status: LIVE VERIFIED

Goal:

Create an autonomous offensive behavior using existing combat units.

Implemented behavior:

`Quiet Base → Select Idle/Marching Forces → Form Hunter Group → Select Valuable Target → Raid → Evaluate Threat → Retreat → Re-form → Re-target`

Target priorities:

`Refinery > Power > Tech > other valuable targets`

Harvester fallback is supported.

Combat forces retreat when facing overwhelming opposition.

The raid does not spawn units and does not replace vanilla production.

Validated in live gameplay:

* marching units correctly counted;
* economic target selection;
* retreat against superior forces;
* repeated raid formation;
* re-targeting after target loss;
* no SmartAI unit spawning;
* no regression in Officer/Capture systems.

Status:

**IMPLEMENTED + HARNESS VERIFIED + LIVE VERIFIED**

---

## C4 — Adaptive Defense

Status: IMPLEMENTED + HARNESS VERIFIED 2026-09-25 (severity slice:
LOW/NORMAL/HIGH allocation; live verification: NEEDED).
Harness: `tools/tmp/smartai_defense_test.lua` 7/7.
Details: `milestones/M2_DECISION_SYSTEM.md` progress log.

Goal:

Make defensive behavior dependent on the current strategic situation rather than fixed defensive reactions.

Potential capabilities:

* dynamically allocate defenders;
* distinguish local attack from strategic threat;
* preserve reserve forces;
* prioritize critical structures;
* dynamically change defensive radius;
* reinforce weak sectors;
* avoid overcommitting the entire army to one attack.

Core loop:

`Threat → Estimate severity → Allocate response → Re-evaluate`

Acceptance principle:

The defensive response changes according to threat severity.

---

## C5 — Multi-Force Coordination

Status: IMPLEMENTED + HARNESS VERIFIED 2026-09-25 (ForceGroup+Tactical
execution for raid fight/flight; live verification: NEEDED).
Harness: `tools/tmp/smartai_group_test.lua` 8/8.
Details: `milestones/M2_DECISION_SYSTEM.md` progress log.

Goal:

Allow several independent forces to operate simultaneously while sharing a common strategic picture.

Examples:

* one group defends;
* one group escorts artillery;
* one group raids the economy;
* one group remains in reserve.

ForceGroup + Tactical become the execution layer.

SmartAI becomes the coordination layer.

Concept:

`Global battlefield state`
→ `Director`
→ `Role assignment`
→ `Independent ForceGroup decisions`
→ `Tactical execution`

Acceptance principle:

Independent groups can make different decisions in the same frame without collapsing into order churn.

---

## C6 — Emergent Gameplay Validation

Goal:

Validate SmartAI as a gameplay system rather than only as a technical system.

Two or more ordinary matches.

No manual activation of individual features.

Observe whether SmartAI produces moments that require the player to change their own decisions.

Examples:

* a raid forces the player to protect the economy;
* a retreat preserves an AI force for a later attack;
* repeated harassment changes AI priorities;
* defense and offense happen simultaneously;
* the AI responds differently after the player changes strategy.

The important metric is not:

`How many features fired?`

The important metric is:

**Did the AI create situations in which the player had to react to the AI?**

M2 completion requires both technical correctness and observable gameplay behavior.

---

# M3 — Strategy System

Goal:

Move from individual decisions to persistent strategic behavior.

SmartAI should no longer only ask:

> What should this unit/group do right now?

It should also ask:

> What kind of game should I be playing?

Potential strategic profiles:

### Rusher

* early pressure;
* aggressive raids;
* lower reserve threshold;
* higher acceptable risk.

### Turtle

* strong defensive reserve;
* lower raid frequency;
* stronger response to attacks;
* gradual counterattacks.

### Tech

* protect technology and economy;
* avoid unnecessary engagements;
* attack high-value infrastructure.

### Economic Pressure

* prioritize refineries;
* target harvesters;
* repeatedly disrupt income;
* avoid unnecessary fights.

### Counter Strategy

Detect dominant player behavior and change strategic priorities.

Core architecture:

`Battlefield State → Belief → Strategy → Directives → Forces`

---

# M4 — Adaptive AI

Goal:

Make strategy change during the match.

Instead of selecting one personality at game start:

`Strategy A`

SmartAI can transition:

`Rusher → Recovery → Defensive → Counterattack`

or:

`Tech → Economic Pressure → Full Offensive`

Possible triggers:

* army losses;
* economic damage;
* repeated failed attacks;
* player composition;
* air dominance;
* tank dominance;
* defensive turtle;
* loss of important structures;
* successful harassment.

Core principle:

**Strategy is not permanent. It is a hypothesis that can be changed when battlefield evidence changes.**

---

# M5 — Strategic Director

> Architecture diagram (Archify, self-contained HTML with theme/export):
> `docs/smartai-director.html` (repo root `docs/`).

Goal:

Create a higher-level SmartAI Director coordinating the complete system.

Architecture:

```text
                    SMARTAI DIRECTOR
                           │
             ┌─────────────┼─────────────┐
             │             │             │
           BELIEF        MEMORY       STRATEGY
             │             │             │
             └─────────────┼─────────────┘
                           │
                      DECISION ARBITER
                           │
          ┌────────────────┼────────────────┐
          │                │                │
       DEFENSE            RAID            ESCORT
          │                │                │
          └────────────────┼────────────────┘
                           │
                       FORCEGROUP
                           │
                        TACTICAL
                           │
                          UNITS
                           │
                       BATTLEFIELD
                           │
                           └──── FEEDBACK ────→
```

This becomes the complete runtime decision architecture.

---

# M6 — Advanced Adaptive Behaviors

Only after the previous layers are stable.

Potential experiments:

* feints;
* split attacks;
* simultaneous economic + military pressure;
* artillery/spotter coordination;
* air route selection;
* counter-air behavior;
* reserve deployment;
* strategic retreats;
* baiting;
* fake retreats;
* targeted harassment;
* player habit recognition.

These should be added only when they fit the Director architecture.

M6 is not a feature dump.

Each new behavior must have a clear place in:

`Belief → Strategy → Decision → Force → Tactical Action`

---

# M7 — SmartAI Showcase

Goal:

Demonstrate what LuaAPI enables that traditional RA2 AI scripting cannot easily express.

Showcase scenarios:

1. Adaptive Raider
2. Tactical Retreat
3. Dynamic Target Reselection
4. Coordinated Defense
5. Persistent Grudge
6. Strategy Switching
7. Counter-Composition
8. Multi-Force Coordination
9. Economic Warfare
10. Adaptive Personality

The showcase should demonstrate behavior, not implementation complexity.

The viewer should be able to understand:

> "The AI noticed what I was doing and changed its behavior."

without needing to understand Lua, Ares or Phobos.

---

# Smart AI Principle: Constraint Inversion

SmartAI does not compete with Ares/Phobos by adding more static AI
configuration. It uses the limitations of engine-level AI customization as
architectural opportunities.

Ares/Phobos extend what the engine AI can do. SmartAI determines what the AI
should do now, based on the current battlefield, memory, priorities, conflicts,
outcomes and adaptation.

The goal is not to create a more powerful AI through larger armies, stronger
economy or more aggressive presets. The goal is to create a smarter AI through
observation, memory, decision-making, coordination, feedback and adaptation.

Rationale: the engine AI's entire reaction vocabulary is a fixed set of
pre-authored trigger conditions (unit/tech ownership, enemy power, credits,
superweapon charge) evaluated once per frame. It has no primitive that carries
a fact across frames. A weight can be retuned; a memory cannot be expressed as
a weight. Constraint inversion means: because the engine cannot remember, the
runtime layer must; because the engine cannot arbitrate, the runtime layer
must; because the engine cannot observe a result, the runtime layer must.

How to apply this principle: a feature that is fully expressible through
existing static AI mechanisms is not, by itself, a SmartAI differentiator.
SmartAI may use such mechanisms — INI weights, TeamTypes entries and
AITriggerTypes conditions remain available inputs, and tuning them can be
worthwhile. What distinguishes SmartAI is where its own value accrues: in the
parts that require runtime observation, memory, decision-making, coordination,
feedback or adaptation.

Concretely: adding a `ThreatValues` table is not Constraint Inversion. That is
the same lever Ares/Phobos and configuration-driven mods already pull, and
doing it inside SmartAI would not make the AI any smarter. Feeding a
`ThreatValues` lookup *into* a runtime decision system — where the result is
combined with live battlefield state, memory of what already happened,
arbitration against other roles, and adaptation to observed outcomes — is
Constraint Inversion. Static configuration is a legitimate input; it is not
the differentiator, and it is not what this principle is asking to be built on.

## What the static kings cannot do (audit 2026-09-30)

Mental Omega's Mental AI and F[AI]r are the reference points: powerful,
respected, and structurally incapable of five things. Each is a SmartAI
work item, not a complaint.

| # | Their limit (measured, not guessed) | Our answer (shipped or queued) |
|---|---|---|
| L1 | Difficulty = cheats. Mental AI multiplies income (AIVirtualPurifiers x12), starting credits and base defenses (Hard: 40 vs 25). Stronger wallet, same brain. | Difficulty = character + tempo. Zero economy writes anywhere in SmartAI (source-verified); Mastermind wins by decisions. M3 stance: hard always rushes. |
| L2 | Same strike forces every time. Wiki, verbatim: "still predictable, as they usually build the same strike forces and use their support powers the same way." | Escalation from evidence: raid size tracks wins/losses (M3 fortunes), composition varies by stance. Never the same raid twice in a row. |
| L3 | Support powers on rails (scripted timing, same usage). | We cannot fire superweapons (no binding, honest limit) — so we time OUR pushes around the battlefield instead: counterpunch when the enemy is bled dry, retreats when outmatched, feints while hunters work. Timing as behavior, not as script. |
| L4 | No memory between waves. Triggers are stateless by engine design; a grudge cannot be expressed as a weight. | Grudge + escalation levels + queue-marks persist across waves and visibly change decisions (Avenging HUD, bigger reforms). |
| L5 | No cross-force coordination. Teams execute scripts independently; TK3600-style escort compositions are pre-authored, not decided. | Arbiter (one authority per tick) + ForceGroup independent same-frame decisions + two-axis simultaneity (feint + raid). |
| L6 | Fairness is a feature players beg for (F[AI]r: "without creating unfair conditions"). Static AI cannot be both fair and threatening — fairness removes its only lever (cheats). | Fairness as architecture (no spawns, no credits, no order immunity) PLUS visible smarts on top. Threat without cheating is the whole bet. |

Rule of combination (senior ruling): take their mechanics as INPUTS
(INI weights, TeamTypes, compositions remain legitimate), never as the
differentiator; never depend on a custom engine DLL (their crashes
become ours without our sources). LuaAPI chains through stock
Ares/Phobos via MinHook (M11 verified) — combine at the mod level,
not at the binary level.

---

# Design Boundaries

SmartAI does not attempt to replace Ares or Phobos.

Ares/Phobos remain valuable for:

* engine extensions;
* AI primitives;
* production capabilities;
* TaskForce/TeamType systems;
* static AI configuration;
* dehardcoding;
* engine fixes.

SmartAI occupies a different layer:

**Ares/Phobos: What the AI can be configured to do.**

**SmartAI: What the AI decides to do right now.**

This distinction is fundamental to the architecture.

---

# Core Evolution

The complete SmartAI evolution is:

`M1`
Reactive

↓

`M2`
Decision

↓

`M3`
Strategy

↓

`M4`
Adaptation

↓

`M5`
Strategic Director

↓

`M6`
Advanced Emergent Behavior

↓

`M7`
Showcase

The long-term loop is:

**Observe → Remember → Evaluate → Decide → Coordinate → Act → Observe the result → Adapt.**

The objective is not to create an AI that simply has more units or more resources.

The objective is to create an AI that **changes what it does because the player changed what they did.**
