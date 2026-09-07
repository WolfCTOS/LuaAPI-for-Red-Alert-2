# Adaptive AI Showcase

**Status:** RESEARCH / NOT VERIFIED

## Purpose

Test how far LuaAPI can take runtime AI decision-making without first adding new native bindings.

The showcase is an experiment in programmable runtime behaviour, not a claim that Ares or Phobos cannot implement the same result.

## Core Idea

The AI observes the player's live army and changes its plan during the match.

The important part is the decision loop:

```text
Observe game state
      ↓
Update runtime state
      ↓
Choose strategy
      ↓
Execute with controlled units
      ↓
Observe the result
      ↓
Re-evaluate
```

## Minimal Vertical Slice

The first implementation should prove only one adaptive loop.

Example:

```text
Player has many tanks
        ↓
AI detects armor-heavy composition
        ↓
AI chooses an anti-armor response
        ↓
Player adds strong AA
        ↓
AI detects the change
        ↓
AI changes its response
```

The decision must be based on the live game state. It must not be a fixed sequence that only looks adaptive.

## Runtime State

The prototype may keep Lua-side state for the observed opponent, such as:

- current unit composition;
- previous composition;
- detected trend;
- current strategic classification;
- time of the last observation;
- current response.

This is state owned by the mod. It must not be described as persistent engine state unless a native implementation is added and verified.

## Strategy Candidates

Initial strategic classifications should stay simple:

```text
ARMOR_HEAVY
AIR_HEAVY
AA_HEAVY
TURTLE
ECONOMY_EXPOSED
BALANCED
```

Possible responses can be represented as ordinary Lua rules or scores. No machine learning is required for the first experiment.

## Tactical Layer

After the strategic loop works, add limited tactical behaviour:

- choose targets based on the current strategy;
- avoid obviously bad targets when the available API supports the check;
- distribute targets between controlled units;
- retreat damaged controlled units;
- regroup after an unsuccessful attack.

These are secondary. The showcase should remain focused on adaptation rather than becoming a general AI rewrite.

## Smart AI Relationship

`smart_ai` is the existing experimental AI mod and the starting point for this work.

The current implementation is a useful baseline but is not yet the Adaptive AI showcase. It currently uses registry-provided unit types, periodic scans, target scoring, defense against nearby player units, and offensive strikes against dense player clusters.

The current implementation should be preserved as a baseline before the adaptive work begins.

## Ares / Phobos Boundary Test

For the exact behaviour chosen for the showcase, document how an experienced Ares/Phobos modder would implement it.

Classify the result as:

```text
Already natural
        ↓
Possible with workarounds
        ↓
No suitable existing model found
```

Do not use statements such as "Ares cannot do this" unless a specific technical limitation has been established.

## Verification Scenarios

The prototype is not complete until these scenarios can be reproduced in the game:

1. The AI detects an initial player composition.
2. The AI selects a response from that observation.
3. The player changes composition.
4. The AI detects the change after a bounded observation interval.
5. The AI changes its response.
6. The result is visible in gameplay and recorded in `LuaAPI.log` where applicable.
7. No new crashes or regressions are introduced.

## Non-Goals for the First Slice

- Machine learning.
- A complete replacement for vanilla AI.
- A new general-purpose AI framework.
- Large C++ architecture changes.
- New LuaAPI bindings before an experiment proves they are necessary.

## Success Condition

The first success condition is simple:

> A player can deliberately change their strategy during a match, and the Lua-controlled AI demonstrably changes its own strategy in response.

Only after this is verified should the project expand Smart AI into a larger runtime AI system.
