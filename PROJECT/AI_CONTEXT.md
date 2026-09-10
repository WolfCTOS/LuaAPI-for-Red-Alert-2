# 🧠 LuaAPI for Red Alert 2: Yuri's Revenge — Engineering Context

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Current API Version:** `1.1.0`  
> **Current Research:** Milestone 14

## Executive Summary

LuaAPI is a native x86 Lua 5.4 runtime embedded into Yuri's Revenge. It exposes selected engine functionality to Lua while keeping unsafe engine interaction inside C++.

The project is now investigating a more specific question:

> What can LuaAPI make programmable at runtime that Ares and Phobos do not naturally model?

The current hypothesis is **programmable runtime decision-making**.

This is not yet a verified project claim.

## Core Principle

> **Ares and Phobos extend the engine. LuaAPI makes gameplay programmable.**

C++ provides safe engine access, lifecycle handling, native state, hooks, and engine primitives. Lua should define gameplay rules, runtime state, decisions, memory, coordination, and mod-specific behaviour.

## Current Research Direction

Recent community discussion identified several possible runtime areas:

- AI target selection based on changing game state.
- AI information and memory.
- Runtime state that persists and changes over time.
- Multiple AI controllers working as one team.
- AI alliances that can change during a match.
- AI using existing game mechanics based on runtime decisions.
- Systems such as Empowerment where a value accumulates from gameplay events.

These are research candidates. They must be compared against Ares and Phobos before being presented as LuaAPI advantages.

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

## First Candidate: AI Decision-Making

The first practical experiment is to replace one part of vanilla AI decision-making with Lua.

A candidate scenario is target selection in a multi-player game. Vanilla AI may continue focusing on one player even when another player becomes a larger threat.

The Lua experiment should not attempt to build a complete AI. It should prove whether one decision can be controlled by Lua using available game state.

## Runtime Information Model

One possible future system is AI memory:

```text
AI sees 5 Apocalypse tanks
        ↓
Stores observation + time
        ↓
No evidence of losses
        ↓
Assumes at least 5 remain
        ↓
New information arrives
        ↓
Updates belief
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

## Shared workflow context

The repository is the shared persistent context for the human developer and all assisting agents.

- `PROJECT/CURRENT_TASK.md` — single source of truth for the task currently being investigated.
- `PROJECT/DECISIONS.md` — lightweight architectural decision log (decisions actually made, not hypotheses).
- `PROJECT/REVIEWS/chatgpt/` — analysis/review artifacts from ChatGPT.
- `PROJECT/REVIEWS/harness/` — independent analysis/review artifacts from Harness.

Workflow:

```text
Human observation
        ↓
CURRENT_TASK.md
        ↓
independent analysis/review
        ↓
implementation task
        ↓
OpenCode implementation
        ↓
game test
        ↓
CURRENT_TASK.md updated
```

Review participants analyse; they are not automatic authorities. OpenCode implements. The human developer provides real in-game observations and final acceptance.

Runtime verification runs under the CnCNet spawner only (since 2026-09-11; Syringe/Ares/Phobos coexistence verified 2026-09-10: all signatures OK, all hooks MH_OK).

## Documentation Authority

When documentation conflicts, use this order:

1. Current native implementation and bindings.
2. Verified runtime tests and showcase mods.
3. `API.md` — public API contract.
4. `PROJECT/CAPABILITIES.md` — verified capabilities.
5. `PROJECT/ROADMAP.md` — current development direction.
6. This file — engineering context and research notes.

Research hypotheses must never override verified implementation behaviour.
