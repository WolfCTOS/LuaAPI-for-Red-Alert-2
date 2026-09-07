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

Current candidate:

> An AI observes the changing balance of a match and changes its response during the same match.

The first practical experiment is documented in [`SHOWCASES/adaptive_ai.md`](SHOWCASES/adaptive_ai.md).

### M14.2 — Preserve Smart AI as Baseline

`smart_ai` is the existing experimental AI implementation and the starting point for the Adaptive AI work.

Before extending it, record its current behaviour and limitations as a baseline. Do not mix baseline fixes with the new research result unless the change is required for the experiment.

### M14.3 — Ares/Phobos Comparison

For the exact behaviour selected for the prototype, determine how it would be implemented using Ares and Phobos.

Classify the result:

```text
Already natural
        ↓
Possible with workarounds
        ↓
No suitable model found
```

Do not use raw line count as the main comparison. The question is whether the behaviour has a natural model in the existing systems.

### M14.4 — Runtime State & Information

Investigate whether Lua can maintain information that changes during the match.

Example:

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

This is an investigation target, not yet a verified feature.

### M14.5 — Adaptive AI Vertical Slice

Build the smallest useful adaptive loop:

```text
Observe player army
        ↓
Classify current strategy
        ↓
Choose response
        ↓
Control AI units
        ↓
Observe changed game state
        ↓
Re-evaluate
```

The first slice should prove one visible change of strategy. It should not attempt to replace the complete vanilla AI.

Initial strategy categories may include armor-heavy, air-heavy, AA-heavy, turtle, economy-exposed, and balanced.

Do not add machine learning. Ordinary Lua state and decision rules are sufficient for the first experiment.

### M14.6 — Tactical Expansion

Only after the adaptive strategic loop is verified, investigate:

- target selection;
- target deconfliction;
- retreat and regroup behaviour;
- engagement evaluation;
- multi-unit coordination;
- event-driven reactions.

These are secondary to proving adaptation.

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
