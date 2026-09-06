# LuaAPI Runtime Boundary Research

> **Status:** 🔬 RESEARCH / NOT VERIFIED
>
> **Purpose:** Identify the capability boundary between Ares/Phobos and LuaAPI.
>
> This document records a current research hypothesis. It does not describe a verified LuaAPI capability.

## The Question

The original question for LuaAPI was:

> What can LuaAPI do that is difficult or unnatural to implement with Ares and Phobos?

The current research direction is more specific:

> **Can LuaAPI provide programmable runtime decision-making that Ares/Phobos do not naturally model?**

The goal is not to replace Ares or Phobos, and not to reproduce their existing mechanics in Lua.

## Current Hypothesis

Ares and Phobos provide many powerful engine extensions and predefined gameplay mechanics.

LuaAPI may have a different role:

```text
Ares / Phobos
    ↓
Engine capabilities and predefined mechanics

LuaAPI
    ↓
Programmable runtime rules and decisions
```

The important distinction is not the language used to configure a mechanic. The distinction is whether a modder can define their own runtime state, rules, history, and decisions without needing a new engine feature for each case.

## Community Signals

Recent discussion with experienced RA2/YR modders produced several useful examples and ideas.

### Runtime information and memory

An AI should not only store:

> I saw 5 Apocs.

It should be able to reason about the observation over time:

> I saw 5 Apocs 10 seconds ago. I have no evidence that any were destroyed. I should currently assume there are at least 5.

This suggests a runtime information model containing observations, timestamps, and changing confidence or belief.

This is a research candidate, not a verified limitation of Ares/Phobos.

### Dynamic AI decisions

Vanilla AI can heavily focus on the human player even while another AI is attacking it.

A possible Lua-controlled system could evaluate the current situation and change its target based on factors such as:

- relative player strength;
- current attacks;
- recent observations;
- previous events;
- alliances;
- remaining players.

The research question is whether this kind of decision-making can be expressed naturally with existing Ares/Phobos AI systems.

### Multiple AI controllers for one side

One proposed scenario is a single base controlled by several independent AI players:

```text
One side
├── Soviet controller
├── Allied controller
└── Yuri controller
```

Each controller could have different production behavior while sharing technology and cooperating at runtime.

The interesting part is not the factions themselves. It is the coordination layer between independent controllers.

### Dynamic alliances

Another proposed direction is allowing AI alliances to change during a skirmish.

For example:

```text
Enemy
  ↓
Temporary ally
  ↓
Enemy again
```

The research question is whether Lua can make alliance decisions part of a general runtime rule system rather than a fixed scenario setup.

### AI use of existing mechanics

Taunts were discussed as another example. The taunt mechanic already exists. The possible LuaAPI capability would instead be the decision of **when and why an AI uses it** without requiring map scripting.

This illustrates an important distinction:

> LuaAPI does not need to add a mechanic if it can make existing mechanics programmable.

### Empowerment-style game mode

A small game mode called `Empowerment` from Dawn of Tiberium Age was suggested as a useful test case.

The basic idea is:

- killing an enemy gives additional firepower;
- destroying a structure has a higher value, such as 3x;
- the accumulated value changes during the game.

This can likely be approximated with Ares/Phobos using existing mechanics and AttachEffects. The research question is whether Lua can express the same system as a simple runtime state and rule set instead of a collection of workarounds.

This is a candidate comparison, not yet a verified LuaAPI showcase.

## First Research Target: AI Decision-Making

The current first target is intentionally small:

> **Replace one vanilla AI decision with Lua-controlled logic.**

Do not attempt to build a complete smart AI.

A first experiment could change target selection in a controlled 3-player scenario.

Example:

```text
Vanilla AI:

Player A is attacked by Player C
        ↓
AI continues focusing on Player B

Lua experiment:

Observe A, B and C
        ↓
Evaluate current threat
        ↓
Choose target dynamically
```

The experiment succeeds only if it demonstrates a real decision that can be controlled by Lua.

## Ares / Phobos Comparison

Every candidate must be classified using three levels:

### Already natural

Ares/Phobos provide a direct and appropriate mechanism.

→ Do not use this as a LuaAPI differentiator.

### Possible with workarounds

The behavior can be approximated through combinations of existing systems, but requires significant setup, conditions, AttachEffects, dummy objects, or other indirect mechanisms.

→ Potential LuaAPI showcase.

### No suitable model found

There is no reasonable existing Ares/Phobos abstraction for the required runtime behavior.

→ Strong LuaAPI candidate, but the claim must still be verified before publication.

## What LuaAPI Must Prove

A successful showcase should demonstrate more than shorter code.

The comparison should show a difference in the **programming model**:

```text
Ares / Phobos approach
Predefined mechanics
        +
Conditions / effects / workarounds
        ↓
Desired behavior

LuaAPI approach
Runtime state
        +
Runtime rules
        +
Game observations
        ↓
Desired behavior
```

Line count alone is not evidence that LuaAPI is better.

## Current Decision

Do not yet:

- redesign the entire API around AI;
- remove APIs that overlap with Ares/Phobos;
- claim that Ares/Phobos cannot implement runtime AI logic;
- build a complete behavior tree or autonomous AI framework;
- use a large showcase before the smallest decision is proven.

Do:

1. Pick one small AI decision.
2. Verify what vanilla YR actually does.
3. Determine how Ares/Phobos could implement the same behavior.
4. Implement the smallest Lua version.
5. Compare the two programming models.
6. Ask experienced modders to challenge the result.

## Architectural Direction Under Investigation

If the hypothesis is confirmed, the long-term architecture may become:

```text
INI / Ares / Phobos
        ↓
Existing game data and engine capabilities
        ↓
LuaAPI
        ↓
Runtime state + observations + rules
        ↓
Decision
        ↓
Game command
```

This would make LuaAPI a programmable gameplay layer rather than an alternative collection of Ares/Phobos features.

## Verification Rule

Nothing in this document should be described as a proven LuaAPI capability until it has been implemented and tested against the current build.

The purpose of this document is to record the research direction and prevent the project from returning to speculative feature-by-feature development.
