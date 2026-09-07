# 🧠 LuaAPI for Red Alert 2: Yuri's Revenge — Engineering Context

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Current API Version:** `1.1.0`  
> **Current Research:** Milestone 14

## Executive Summary

LuaAPI is a native x86 Lua 5.4 runtime embedded into Yuri's Revenge. It exposes selected engine functionality to Lua while keeping unsafe engine interaction inside C++.

The project is investigating a specific question:

> What can LuaAPI make programmable at runtime that Ares and Phobos do not naturally model?

The current hypothesis is **programmable runtime decision-making**.

This is not yet a verified project claim.

## Core Principle

> **Ares and Phobos extend the engine. LuaAPI makes gameplay programmable.**

C++ provides safe engine access, lifecycle handling, native state, hooks, and engine primitives. Lua should define gameplay rules, runtime state, decisions, memory, coordination, and mod-specific behaviour.

## Current Research Direction

The current primary experiment is an **Adaptive AI** showcase built from the existing `smart_ai` mod.

The goal is to test whether Lua can run a decision loop that:

- observes the player's live army;
- maintains Lua-side runtime state;
- classifies the current situation;
- chooses a response;
- controls AI units;
- observes the changed game state;
- changes the next decision.

The first slice is intentionally small. It is not a complete replacement for vanilla AI and does not require machine learning.

Other runtime areas remain research candidates:

- AI target selection based on changing game state.
- AI information and memory.
- Multiple AI controllers working as one team.
- AI alliances that can change during a match.
- AI using existing game mechanics based on runtime decisions.
- Systems such as Empowerment where a value accumulates from gameplay events.

These are research candidates. They must be compared against Ares and Phobos before being presented as LuaAPI advantages.

## Smart AI Baseline

`smart_ai` is an existing experimental AI mod and the baseline for the Adaptive AI experiment.

Its current implementation uses periodic scans, registry-provided unit types, target scoring, nearby defensive interception, and offensive strikes against dense player clusters. It is useful as a starting point but should not be described as an adaptive AI until the new runtime decision loop is implemented and verified.

The baseline should be preserved while the new experiment is developed so that improvements can be attributed correctly.

## M14 Working Model

The current investigation uses this process:

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

### Boundary classification

```text
Already natural
        ↓
Possible with workarounds
        ↓
No suitable model found
```

The comparison is about the programming model and required systems. Raw line count is not sufficient evidence.

## Adaptive AI Showcase

The detailed experiment is documented in [`SHOWCASES/adaptive_ai.md`](SHOWCASES/adaptive_ai.md).

The first success condition is:

> A player can deliberately change their strategy during a match, and the Lua-controlled AI demonstrably changes its own strategy in response.

The first prototype should avoid broad API expansion. New C++ bindings are justified only when the experiment proves an existing primitive is insufficient.

## Runtime Information Model

One possible future system is AI memory:

```text
AI sees 5 Apocalypse tanks
        ↓
Stores observation + time
        ↓
New information arrives
        ↓
Updates its belief
        ↓
Changes the next decision
```

This is an investigation target. Do not describe it as an existing LuaAPI feature until implemented and verified.

## Existing Foundation

The current API already provides primitives that can support runtime gameplay experiments, including:

```lua
World.GetAllUnits()
World.GetUnitsInRadius(x, y, radius)
World.GetBuildings()

House.GetPlayer()
house:GetCredits()
house:AddCredits(...)
house:GetName()
house:SpawnUnit(...)

unit:GetOwner()
unit:GetTypeName()
unit:GetPosition()
unit:IsAlive()
unit:GetHealth()
unit:GetTarget()
unit:MoveTo(...)
unit:Attack(...)
unit:Stop()

Engine.PrintMessage(...)
```

The exact public contract remains `API.md`.

## Architecture

```text
                 C++
┌──────────────────────────────────────┐
│ Engine integration                   │
│ Hooks / lifecycle / pointer safety   │
│ Native state / engine primitives     │
└──────────────────┬───────────────────┘
                   │
                   ▼
                LuaAPI
┌──────────────────────────────────────┐
│ Safe engine access                   │
│ Events / observations / queries      │
└──────────────────┬───────────────────┘
                   │
                   ▼
                  Lua
┌──────────────────────────────────────┐
│ Runtime state                        │
│ Decisions / memory / rules           │
│ Coordination / gameplay behaviour   │
└──────────────────┬───────────────────┘
                   │
                   ▼
                 Mods
```

## Engineering Rules

1. Verify before documenting.
2. Do not invent API names.
3. Separate verified functionality from research and plans.
4. Validate engine-backed objects before use.
5. Invalidate stored references when objects are destroyed.
6. Prefer passive C++ infrastructure when Lua can make the gameplay decision.
7. Use logical game frames for gameplay timing.
8. Add native bindings only when a real experiment proves they are needed.
9. Do not reimplement Ares or Phobos without a clear reason.
10. Do not remove existing API functions until the runtime boundary has been established.

## Historical Engineering Lessons

### 32-bit Lepton Overflow

RA2 uses 256 leptons per cell. Large squared-distance calculations can exceed signed 32-bit limits. Use sufficiently wide arithmetic.

### Destroyed Engine Objects

Engine-backed pointers can become invalid after destruction. Native code must validate objects before dereferencing them.

### Deferred Cleanup

Do not erase entries from containers while iterating over them. Queue removals and process them after iteration.

### Savegame Lifecycle

`OnScenarioStart()` is not universal initialization for loaded saves. Runtime systems must detect and restore missing state when required.

### Passive Native State

The multi-turret system demonstrated that C++ can maintain timers, target references, rotation, and pointer safety while Lua controls targeting and firing decisions. This remains the preferred boundary for future runtime systems.

## Documentation Authority

When documentation conflicts, use this order:

1. Current native implementation and bindings.
2. Verified runtime tests and showcase mods.
3. `API.md` — public API contract.
4. `PROJECT/CAPABILITIES.md` — verified capabilities.
5. `PROJECT/ROADMAP.md` — current development direction.
6. This file — engineering context and research notes.

Research hypotheses must never override verified implementation behaviour.
