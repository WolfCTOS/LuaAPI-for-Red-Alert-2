# 🧱 LuaAPI Runtime Boundary — Research Direction & First Evidence

> **Status:** 🔬 **RESEARCH — hypothesis partially verified; live YR verification pending**
> **Target:** `gamemd.exe` — Yuri's Revenge 1.001  
> **Frame of reference:** INI / Ares / Phobos
>
> This document records a research hypothesis AND the first experiment result.
> Nothing here is a proven capability until implemented and tested against the
> current build.

## Question

> What can LuaAPI provide as a programmable runtime layer that is **not** naturally
> modeled by INI, Ares, or Phobos?

The principle: **Ares and Phobos extend the engine. LuaAPI makes gameplay
programmable.** A feature is only valuable if it crosses a real runtime boundary
that INI/Ares/Phobos do not already model naturally.

The goal is not to replace Ares or Phobos, and not to reproduce their existing
mechanics in Lua.

## Research candidates (upstream)

- AI target selection reacting to changing game state.
- Runtime observations with timestamps and changing confidence/belief.
- Several AI controllers operating as one coordinated side.
- Dynamic alliances during a match.
- AI deciding **when/why** to use existing mechanics (e.g. taunts) without map scripting.
- Empowerment-style accumulation (kill/structure value changing firepower).

## Comparison levels (upstream)

Every candidate must be classified:

- **Already natural** — Ares/Phobos provide a direct mechanism. Not a differentiator.
- **Possible with workarounds** — approximable via AttachEffects/dummies/conditions. Potential showcase.
- **No suitable model found** — no reasonable existing abstraction. Strong candidate, still must be verified.

Line count alone is not evidence; the difference must be in the **programming model**
(runtime state + rules + observations vs predefined mechanics + workarounds).

## Protocol (upstream)

1. Pick one small AI decision. 2. Verify what vanilla YR does. 3. Determine the
   Ares/Phobos implementation. 4. Implement the smallest Lua version.
5. Compare the two programming models. 6. Ask experienced modders to challenge it.

## Result of the first experiment

The first experiment tested whether Lua can influence **one vanilla-AI
target-selection decision** using runtime state:

```
Game state → Lua observes → Lua decides → engine executes
```

**Architecture proven (automated harness):** Lua observed a live signal (anti-air
threat concentration near an AI unit's current target), decided the current
target was unsafe (signal crossed a threshold), issued the engine's native
`unit:Attack(alternative)`, and **read back `unit:GetTarget()`** to confirm the
engine accepted the new target. This is a real runtime decision made from live
battlefield state, executed through the existing engine target-assignment path.

**What was NOT proven:** that the vanilla AI house will not immediately
re-select its own target afterward. That requires live Yuri's Revenge timing and
is **not yet verified** — it is the remaining half of the "engine executes and
persists" boundary.

## Ares / Phobos comparison

| Capability | INI | Ares | Phobos | LuaAPI |
|---|---|---|---|---|
| Static per-type target priority / weights | ✅ | ✅ | ✅ | n/a (no rule authored) |
| Conditional **runtime** reselection based on a live, arbitrary battlefield signal (e.g. "AA present near current target right now") | ❌ | ❌ | ❌ | ✅ |
| Scripted / deterministic sequence of target picks | ✅ (ScriptTypes) | ✅ | ✅ | possible |
| Custom "pick nearest/strongest X given Y" | ⚠️ fixed set | ⚠️ richer fixed set | ⚠️ fixed set | ✅ arbitrary Lua |

**Verdict:** The specific behavior — re-selecting a target mid-assault based on a
frame-queryable, arbitrary runtime signal — is **not naturally modeled by
INI/Ares/Phobos**. Those systems configure *static* priorities and *fixed*
script sequences; they do not expose a live-frame predicate over arbitrary game
state that can change a target while an attack is in progress. This is a
candidate **runtime boundary**. The experiment shows Lua can cross it (observe →
decide → act → read-back); it does **not** claim LuaAPI is "better" — only that
the boundary is reachable.

## Where the actual boundary is

### Confirmed (automated harness, `target_reselect` mod)
1. Lua can read the **current target's** runtime attributes and its local
   battlefield composition via existing primitives.
2. Lua can compute an arbitrary, composable signal from that state (no new
   native binding was required).
3. Lua can command the engine's target-assignment path (`unit:Attack`) and the
   engine **accepted** it (read-back via `unit:GetTarget()` changed).
4. The design is **generic** — the decision is a rule over a runtime signal, not
   a unit-name lookup.
5. Object lifetime safety holds: the mod tracks ids, re-resolves per frame, and
   never retains a Techno/House userdata.

### Not yet confirmed (needs live YR)
1. **Persistence across the vanilla AI:** whether, after Lua issues the order,
   the AI house re-selects its own target next frame. The current API is
   *command* (issue `Attack`), not *override* (block the AI's own selection).
   There is no primitive that says "this unit ignores its own AI selection for
   N frames." This is the sharpest open boundary.
2. In-game visual/behavioral confirmation at real frame timing.

## Why the boundary exists

- The engine's AI target selection runs inside the native AI loop, which Lua
  cannot directly silence with the current API. Lua can *inject a command*
  (`Attack`), but the vanilla AI remains free to overwrite it on a later tick.
- Ares/Phobos can configure AI weighting and script ordering, but they cannot
  express a per-frame predicate computed from arbitrary runtime state (they are
  INI/script-data driven, not a live interpreter). LuaAPI *can* compute that
  predicate — but it lacks the second half: an authoritative way to hold the
  engine's own AI away for a duration.

## Next candidate (do NOT implement yet)

The second half of the boundary: a **Lua-side "order lease"** that keeps a unit
locked to a Lua-chosen target until Lua releases it or a condition clears. To be
valuable it must be verified to actually resist the vanilla AI's re-selection;
otherwise it is a hypothesis, not a capability. If a native primitive is needed
for the engine to honor the lease, document the smallest one and compare against
Ares/Phobos before adding it.

## Verification status

| Item | Status |
|---|---|
| Architecture: observe → decide → act → read-back | ✅ verified (deterministic harness, `target_reselect`, 8/8) |
| Generic (no unit-name branch) | ✅ verified |
| No new native binding required | ✅ verified |
| Object-lifetime safety (ids, re-resolve) | ✅ verified |
| Vanilla AI does not immediately override the new target | ⏳ **Need to test** (live YR) |
| In-game runtime confirmation | ⏳ **Need to test** |

Keep this experiment documented as research until the live-YR half is confirmed.
