# Milestone 14 — Runtime Gameplay & Decision-Making

> **Goal:** Explore and prove where Lua can provide programmable runtime gameplay logic beyond the natural abstractions of Ares and Phobos.
>
> **Principle:** Ares and Phobos extend the engine. LuaAPI makes gameplay programmable.

## Why This Milestone Exists

LuaAPI has reached the point where adding another low-level engine binding is not always the best next step.

The original question is no longer simply:

> What mechanic can LuaAPI add?

The more useful question is:

> What kind of runtime gameplay logic can a modder define with Lua that is difficult to express naturally with Ares and Phobos?

Recent community discussion suggests that the most promising area may be runtime decision-making: systems that continuously observe the game, keep their own state, react to events, and change behaviour over time.

This is a hypothesis, not a verified capability claim.

## Scope

Milestone 14 is an investigation first and an implementation milestone second.

The first target is a small AI decision-making problem. The goal is to replace one part of vanilla AI handling with Lua while keeping existing INI data and engine functionality available.

Potential areas include:

```text
Runtime Decision-Making
│
├── Target selection
├── Information and memory
├── Production decisions
├── Multi-AI coordination
├── Dynamic alliances
└── Event-driven behaviour
```

Do not build a complete replacement AI during this milestone.

## M14.1 — Define the Runtime Boundary

Identify one decision currently handled by vanilla AI and describe what information it uses.

The first candidate is target selection in a multi-player situation where vanilla AI may continue focusing on one player despite another player becoming a larger threat.

The experiment should answer:

- What does vanilla AI currently consider?
- What information would a Lua decision system need?
- Which part of the decision can Lua replace?
- Can the result be observed clearly in-game?

## M14.2 — Ares/Phobos Comparison

Before implementing the Lua solution, attempt to model the same behaviour using existing Ares and Phobos functionality.

Classify the result as:

```text
Already natural
        ↓
Possible with workarounds
        ↓
No suitable model found
```

The comparison must focus on the conceptual model and required systems, not raw line count.

A feature should not be presented as a LuaAPI advantage merely because the Ares/Phobos implementation is longer.

## M14.3 — Runtime State & Information

Investigate whether Lua can naturally maintain state that changes during the game.

Example concept:

```text
AI observes 5 Apocalypse tanks
        ↓
Stores the observation and its time
        ↓
No evidence of losses appears
        ↓
AI continues to assume at least 5 exist
        ↓
New information arrives
        ↓
Belief is updated
```

The purpose is to test whether Lua can define its own runtime information model rather than relying only on predefined engine states.

This is an investigation target, not a requirement to build a complete AI memory system in M14.

## M14.4 — Lua Decision Prototype

Implement the smallest useful Lua prototype for the selected decision.

The prototype should:

- observe relevant game state;
- maintain any required Lua-side state;
- make a decision during runtime;
- issue an existing engine command or use an existing LuaAPI primitive;
- produce a visible and reproducible result.

The prototype should avoid adding native functionality unless the experiment proves that an existing LuaAPI primitive is insufficient.

## M14.5 — Community Challenge

Show the concrete result to experienced Ares/Phobos modders and ask how they would implement the same behaviour without Lua.

The question should be concrete:

> How would you implement this exact runtime behaviour with Ares/Phobos?

Do not ask whether Lua is "better" in general.

The purpose is to test the boundary with real implementations and find cases where Lua provides a different programming model.

## M14.6 — Candidate Runtime Systems

After the first decision prototype, investigate larger systems only if the first case confirms the direction.

Potential candidates:

### Multi-AI Coordination

Several independent AI controllers operate as one team while keeping separate roles and production behaviour.

### Dynamic Alliances

AI relationships can change during a match based on runtime conditions. Human players may also interact with these relationships.

### Event-Driven AI Behaviour

AI reacts to events instead of relying only on predefined TeamTypes, AITriggers, and static priorities.

### Runtime Information / Memory

AI keeps observations, timestamps, assumptions, and updates its internal state when new evidence appears.

### Empowerment-Style Systems

A runtime value changes as the player destroys units or structures and continuously affects gameplay. This is a candidate for comparison, not yet a verified LuaAPI-exclusive capability.

## M14.7 — What This Milestone Is Not

M14 is not intended to:

- reimplement Ares or Phobos in Lua;
- replace INI as the configuration layer;
- build a complete general-purpose AI immediately;
- add mechanics only because they are easier to write in Lua;
- claim that Ares or Phobos cannot do something without testing the alternative;
- optimize or remove existing API functions before the runtime boundary is established.

## Completion Criteria

Milestone 14 is complete when:

- one concrete vanilla AI decision has been identified;
- its Ares/Phobos implementation path has been investigated;
- the result has been classified as natural, workaround-heavy, or unsupported by a suitable model;
- a minimal Lua prototype exists for a promising case;
- the runtime behaviour is reproducible in-game;
- the result has been reviewed by experienced modders where possible;
- the project can state a more precise answer to what LuaAPI adds beyond Ares/Phobos.

If the investigation shows that a candidate is already natural in Ares/Phobos, discard it and test another candidate.

If no meaningful boundary is found, record that result instead of forcing a justification for LuaAPI.

## Development Rule

Use a vertical slice:

```text
Real gameplay problem
        ↓
Understand vanilla behaviour
        ↓
Test Ares/Phobos model
        ↓
Classify the boundary
        ↓
Lua prototype
        ↓
In-game verification
        ↓
Community challenge
        ↓
Document the result
```

Do not build abstractions before a real gameplay problem requires them.

## Architecture Direction

The intended long-term boundary is:

```text
                 C++
┌──────────────────────────────────────┐
│ Engine integration                   │
│ Hooks                                │
│ Pointer safety                       │
│ Lifecycle                            │
│ Native state                         │
│ Engine primitives                    │
└──────────────────┬───────────────────┘
                   │
                   ▼
                 LuaAPI
┌──────────────────────────────────────┐
│ Safe engine access                   │
│ Events / observations                │
│ Object and world queries             │
│ Runtime state                        │
└──────────────────┬───────────────────┘
                   │
                   ▼
                  Lua
┌──────────────────────────────────────┐
│ Decisions                            │
│ Memory                               │
│ Rules                                │
│ Coordination                         │
│ Gameplay behaviour                  │
└──────────────────┬───────────────────┘
                   │
                   ▼
                  Mods
```

The long-term direction remains:

```text
Engine Access
      ↓
Safe Native API
      ↓
Runtime State & Events
      ↓
Programmable Gameplay Logic
      ↓
Decision-Making / Tactical Systems
      ↓
Reusable Mod Systems
```

M14 is the point where LuaAPI tests whether this direction represents a real capability boundary rather than simply a different syntax for existing Ares/Phobos features.
