# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Roadmap

> **Target Platform:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Current Release:** `v1.0.0`  
> **Development API Line:** `1.1.0`

LuaAPI started as a native Lua runtime and engine bridge. The next stage is to find and prove the real boundary between Ares/Phobos engine extensions and programmable runtime gameplay logic.

> **Core principle:** Ares and Phobos extend the engine. LuaAPI makes gameplay programmable.

> ⚠️ A planned capability is not a verified capability. Only tested behaviour is marked `VERIFIED`.

---

## Project Lifecycle

| Phase | Milestone | Status |
|---|---|---|
| Phase 1 | Milestones 1–5 — Runtime, hooks, core API | ✅ Complete |
| Phase 2 | Milestone 6 — Lifecycle & safety | ✅ Complete |
| Phase 3 | Milestone 7 — Spatial API & events | ✅ Core functionality established |
| Phase 4 | Milestone 8 — Beta hardening | ✅ Complete |
| Phase 5 | Milestone 9 — Production Release v1.0 | ✅ Complete |
| Phase 6 | Milestone 10 — Multi-Turret & Advanced Combat | ✅ Core verified |
| Phase 7 | Milestone 11 — CnCNet & Development Tools | ✅ Complete |
| Phase 8 | Milestone 12 — Unit Control & Tactical Gameplay | 🟡 Iterative |
| Phase 9 | Milestone 13 — Runtime Restoration / Hardening | 🟡 Planned |
| Phase 10 | **Milestone 14 — Runtime Gameplay & Decision-Making** | 🔬 Active |

---

## Milestone 14 — Runtime Gameplay & Decision-Making

### Goal

Find a concrete class of runtime gameplay logic that Lua can express in a useful way beyond the natural abstractions of Ares and Phobos.

The current hypothesis is that the strongest boundary may be **programmable runtime decision-making** rather than another predefined gameplay mechanic.

This is a research hypothesis, not a project claim.

### M14.1 — Define the Runtime Boundary

Identify one decision handled by vanilla AI and describe the information behind that decision.

First candidate:

> Replace one part of vanilla AI target selection with Lua so the AI can react to the changing balance of a multi-player game instead of relying only on the existing threat system.

### M14.2 — Ares/Phobos Comparison

Attempt to reproduce the same behaviour using Ares and Phobos.

Classify the result:

```text
Already natural
        ↓
Possible with workarounds
        ↓
No suitable model found
```

Do not use raw line count as the main comparison. The question is whether the behaviour has a natural model in the existing systems.

### M14.3 — Runtime State & Information

Investigate whether Lua can maintain information that changes during the match.

Example:

```text
AI sees 5 Apocalypse tanks
        ↓
Stores observation + time
        ↓
No evidence of losses
        ↓
Continues to assume at least 5
        ↓
New information arrives
        ↓
Updates its belief
```

This is an investigation target, not yet a verified feature.

### M14.4 — Lua Decision Prototype

Build the smallest possible prototype that:

- observes game state;
- maintains required Lua-side state;
- makes one runtime decision;
- uses existing LuaAPI primitives where possible;
- produces a visible and reproducible result.

New C++ bindings are justified only when the experiment proves an existing primitive is insufficient.

### M14.5 — Community Challenge

Show the exact behaviour to experienced Ares/Phobos modders and ask:

> How would you implement this exact behaviour with Ares/Phobos?

The goal is to test the boundary with real implementations.

### M14.6 — Larger Runtime Systems

Only after a smaller case confirms the direction, investigate:

- AI information and memory;
- dynamic target selection;
- production decisions;
- multi-AI coordination;
- dynamic alliances;
- event-driven AI behaviour;
- runtime systems such as Empowerment-style accumulation.

### M14.7 — API Audit

After the runtime boundary is established, review the current API.

Classify exposed functionality as:

```text
Foundation
   ↓
Engine bridge
   ↓
Ares/Phobos duplicate
   ↓
Genuine programmable capability
```

Do not remove existing API functions before this audit. Basic engine access may still be necessary infrastructure even when a similar engine feature exists elsewhere.

---

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

Do not build abstractions because they look useful in theory.

---

## Architectural Direction

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

The intended direction is:

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

The purpose of Milestone 14 is to determine whether this direction represents a real capability boundary or merely a different way to configure existing Ares/Phobos features.

---

## Historical Milestones

The earlier milestones established the foundation required for this research:

- Runtime embedding and Lua 5.4 execution.
- Safe engine-backed object access.
- Lifecycle handling.
- Spatial queries and gameplay callbacks.
- CnCNet process attachment and logical-frame execution.
- Multi-turret state and Lua-controlled combat.
- Unit control primitives and tactical gameplay showcases.

These remain part of the project's foundation. M14 is not a replacement for that work. It is the next architectural question.
