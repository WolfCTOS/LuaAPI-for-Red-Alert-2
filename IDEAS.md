# IDEAS.md

# Architecture Idea: Hybrid Event-Driven Runtime

**Status:** IDEA / COMMUNITY REVIEW REQUIRED  
**Implementation:** NONE  
**Decision:** Do not change runtime architecture until community feedback and a repository-backed feasibility review are complete.

## Hypothesis

The current LuaAPI runtime is primarily driven by a logical-frame update loop:

`MainLoop -> logical frame gate -> OnGameFrame -> OnTick -> mod.Update(frame)`

The Lua-side framework can then perform queries and polling to detect changes.

A possible future architecture is hybrid:

`Events = what changed`  
`Queries = what is true now`  
`Lua = what to do about it`

Example:

```
Game
 ├─ relevant event -> LuaAPI EventBus -> subscribed mod
 └─ state query ---------------------> Lua decision
```

This is only a design hypothesis. It is not a planned rewrite.

## Current Architecture Evidence

The current native main loop is already gated to the logical game frame. `Hooked_MainLoop()` does not dispatch Lua logic on every render/call of the game loop when `Unsorted::CurrentFrame` has not changed.

Current path:

```
Hooked_MainLoop
    -> logical-frame gate
    -> OnGameFrame()
    -> OnTick(frame)
    -> mod.Update(frame)
```

Therefore the concern is **not simply "Lua runs every render frame"**. The current design already reduces gameplay Lua dispatch to one logical frame.

The existing Lua-side EventBus is also not a native engine event source. It is a subscription/dispatch abstraction. Framework systems can emit events into it, but the current build does not provide a general native event pipeline for arbitrary engine state transitions.

The current `CombatStateTracker` is explicitly polling/state-based. It refreshes tracked units from fresh `World.GetUnits()` scans and can emit Lua-side EventBus changes. Its target-liveness pulse can perform a whole-world scan every configured number of logical frames.

The native `OnScenarioStart` / `OnUnitDestroyed` callback contracts currently exist in parts of the code/documentation but are not reliable evidence of a wired engine event pipeline. Do not use their existence as proof that native events are available.

## Proposed Hybrid Model

Do not replace queries.

Add event-driven observation where a reliable engine lifecycle hook exists.

### Events

Potential examples:

- UnitCreated
- UnitDestroyed
- UnitCaptured
- DamageTaken
- MissionChanged
- HouseDefeated

These should only be added after the actual engine hook point is identified and live verified.

### Queries

Keep current state queries:

- GetPosition
- GetHealth
- GetOwner
- GetTarget
- GetMission
- GetUnits
- GetUnitsInRadius
- etc.

Events answer:

> What happened?

Queries answer:

> What is true now?

## Filtered / Targeted Events

A mod should not necessarily receive every event in the game.

Conceptual example:

```lua
Events.OnUnitDestroyed({
    kinds = {"RHINO", "HTNK", "APOC"}
}, function(unit, killer)
    ...
end)
```

Or a category-based form:

```lua
Events.OnUnitDestroyed("Tank", function(unit, killer)
    ...
end)
```

The exact API is intentionally undecided.

The important design property is:

> If a mod only cares about tanks, unrelated infantry events should not enter that mod's Lua callback path.

The filtering strategy must be evaluated for CPU cost and determinism before implementation.

## Performance Hypothesis

Event-driven dispatch is not automatically cheaper.

The useful comparison is:

### Polling

```
Every logical frame:
    scan relevant objects
    read state
    compare against previous state
    infer changes
```

### Native event

```
Engine transition:
    identify event
    check whether any subscriber is interested
    dispatch only relevant callbacks
```

A native event can reduce Lua-side scanning and inference, especially for sparse events such as destruction or capture.

However, an event system that fires per object per frame could be worse:

```
500 units * 30 events/sec = 15,000 callbacks/sec
```

Therefore the design must avoid a generic `OnUnitUpdate` / `OnEveryObjectTick` event.

Prefer semantic transitions and selective subscriptions.

## Example: Bounty Combo

This idea is one possible test case, not a commitment.

Existing Bounty-style behavior:

```
kill -> predefined reward
```

Possible stateful combo behavior:

```
kill 1 -> 50%
kill 2 within 15 sec -> 75%
kill 3 within 15 sec -> 100%
no kill for 15 sec -> reset
```

An event-driven implementation would conceptually become:

```
UnitDestroyed
    -> identify killer/player
    -> update combo state
    -> check logical-frame timeout
    -> calculate reward
```

A polling implementation instead needs to infer destruction from state changes.

This is a useful architectural example because the mechanic depends on **history + timing + an observed event**, rather than only static unit configuration.

Ares/Phobos already provide Bounty-related functionality. Therefore this idea must not be presented as "LuaAPI invented Bounty". The actual research question is whether a stateful kill-combo/time-window reward layer can be expressed directly with existing Ares/Phobos mechanisms.

## Architectural Boundary

Potential future model:

```
Ares / Phobos
    Unit definitions
    Stats
    Weapons
    Warheads
    Engine extensions

LuaAPI
    Runtime events
    Runtime state
    Observation
    Decision
    Orders
    Coordination
    Memory
```

This is a hypothesis, not a claim that Ares/Phobos cannot implement a given behavior.

Every proposed event-driven feature must be stress-tested against existing Ares/Phobos capabilities before being considered LuaAPI-specific.

## Risks

1. **Hook complexity**
   Finding a reliable engine lifecycle hook may be harder than implementing the Lua API around it.

2. **Pointer lifetime**
   Events can deliver objects at dangerous lifecycle boundaries. Event payloads need strict validity rules.

3. **Multiplayer determinism**
   Event ordering, filtering, and callback behavior must remain deterministic.

4. **Callback cost**
   A large number of subscribers or high-frequency events could create more overhead than polling.

5. **Reentrancy**
   A callback may issue an order that changes engine state while the original engine event is still executing.

6. **Session lifecycle**
   Event subscriptions and cached native references must not leak across matches.

7. **False architectural rewrite**
   Adding events does not require deleting the current query/update model. A hybrid design should preserve the existing working path.

## Community Gate

No architectural change should be made based only on this idea.

Before implementation:

1. Ask the Ares/Phobos community whether equivalent event/stateful mechanisms already exist.
2. Ask specifically about:
   - kill streak / combo tracking
   - time-window state
   - dynamic bounty scaling
   - per-player runtime state
   - event-like scripting hooks
3. Record the answers.
4. Compare the answers against the actual current LuaAPI source.
5. Define one minimal proof-of-concept only if the architectural hypothesis survives the comparison.

## Research Gate

If community feedback supports further investigation, the first technical experiment should be one low-frequency semantic event, preferably something like:

`UnitDestroyed`

Do not begin with a generic event framework rewrite.

Required evidence:

- exact engine hook point;
- callback frequency;
- payload validity;
- subscriber filtering cost;
- logical-frame / multiplayer determinism;
- behavior when no Lua mod subscribes;
- behavior with multiple subscribers;
- behavior across match reset;
- live verification.

## Non-Goals

This idea does NOT currently authorize:

- replacing `mod.Update()`;
- removing World queries;
- rewriting EventBus;
- rewriting the Lua VM;
- adding a generic per-unit-per-frame callback;
- implementing Bounty Combo;
- implementing Dynamic Unit Behavior;
- changing C++ architecture before community review.

## Decision Rule

The current architecture remains unchanged unless evidence demonstrates that a hybrid event/query model provides a meaningful capability or efficiency advantage that cannot be obtained cleanly with the existing polling/query layer.

**Current decision: KEEP CURRENT ARCHITECTURE. RESEARCH ONLY.**
