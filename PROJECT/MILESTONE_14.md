# Milestone 14 — Gameplay Abstractions

> **Goal:** Build a small Lua-side gameplay framework on top of the existing LuaAPI primitives.
>
> **Principle:** C++ provides engine primitives. Lua provides gameplay abstractions.

## Why This Milestone Exists

LuaAPI has reached the point where adding another low-level binding is not always the best next step.

The next layer should make existing primitives easier to compose into real gameplay systems.

The target is not to hide the engine. The target is to give modders useful building blocks so they do not need to reimplement the same control logic in every mod.

## Scope

Milestone 14 focuses on five Lua-side abstractions:

```text
Gameplay Framework
│
├── EventBus
├── Timer
├── Query
├── Task
└── UnitController
```

These systems should use existing LuaAPI functionality wherever possible.

New C++ bindings are justified only when a real framework requirement cannot be implemented from the current API.

## M14.1 — EventBus

Create a common event subscription layer for Lua gameplay code.

Target interface:

```lua
Events.On("unit_destroyed", function(unit, killer)
    -- React to the event.
end)
```

Required behavior:

- Multiple subscribers can listen to the same event.
- `Events.On()` returns a subscription handle or ID.
- `Events.Off()` removes a subscription.
- Lua callback errors must not crash the game.
- Subscriptions are cleaned up during session reset.
- Event dispatch must have a defined execution order.
- Event dispatch must remain safe when callbacks invalidate engine objects.
- Re-entrant event behavior must be defined before implementation is marked complete.

The EventBus is a Lua abstraction. Native hooks remain responsible for obtaining engine events.

## M14.2 — Timer

Create a logical-frame timer abstraction.

Target interface:

```lua
Timer.After(60, function()
    -- Run once after 60 logical frames.
end)
```

```lua
local timer = Timer.Every(30, function()
    -- Run every 30 logical frames.
end)

timer:Cancel()
```

Required behavior:

- Timers use logical game frames.
- Timers are deterministic for gameplay use.
- Timers are cleaned up on session reset.
- Cancelled timers must not execute again.
- Timer callbacks must be protected from Lua errors.
- Destroyed engine objects must not be kept alive only by timer state.

Do not introduce wall-clock timing for deterministic gameplay systems.

## M14.3 — Query

Create reusable Lua query helpers on top of existing world and object APIs.

Initial examples:

```lua
local enemies = Query.Enemies(unit)
local nearby = Query.Nearby(unit, 300)
local target = Query.NearestEnemy(unit)
```

The Query layer should remain composable.

Complex decisions should stay in Lua instead of becoming large native query bindings.

Example:

```lua
local target = Query.NearestEnemy(unit)

if target and target:GetHealth() < 100 then
    unit:Attack(target)
end
```

The exact query set should be driven by showcase requirements rather than by an attempt to expose every possible engine filter.

## M14.4 — Task

Create a minimal abstraction for actions that may take multiple logical frames.

Initial target interface:

```lua
Task.MoveTo(unit, position)
Task.Attack(unit, target)
Task.Wait(60)
```

Tasks should provide a small lifecycle model:

```text
Pending → Running → Completed
                 └→ Failed
                 └→ Cancelled
```

Where useful, tasks may expose completion callbacks:

```lua
local task = Task.MoveTo(unit, position)

task:OnComplete(function()
    -- Continue behavior.
end)
```

Milestone 14 should not build a full behavior-tree or AI scheduler. Keep the task system minimal until real gameplay use proves the need for more.

## M14.5 — UnitController

Create a small Lua abstraction that composes unit state, tasks, and commands.

Example:

```lua
local controller = UnitController.New(unit)

controller:MoveTo(position)
controller:Attack(target)
controller:Stop()
```

The controller must not become a second native AI.

Its role is to coordinate Lua gameplay behavior using existing unit-control primitives.

## M14.6 — Framework Integration

The abstractions must work together.

Expected flow:

```text
Native engine event
        ↓
LuaAPI event layer
        ↓
EventBus
        ↓
Query / decision logic
        ↓
UnitController
        ↓
Task
        ↓
Existing LuaAPI command
```

Timer should provide delayed or repeated logical-frame execution where an event-driven flow is not sufficient.

## M14.7 — Showcase: Tactical Patrol

Build one real showcase that exercises the framework rather than isolated API tests.

Target behavior:

```text
Unit patrols
    ↓
Enemy detected
    ↓
Target selected
    ↓
Attack task started
    ↓
Enemy destroyed
    ↓
Event received
    ↓
Unit returns to patrol
```

The showcase should use the framework for coordination and should minimize direct polling logic in the final implementation.

The purpose of the showcase is to discover missing primitives and framework problems.

## Completion Criteria

Milestone 14 is complete only when:

- EventBus works with real LuaAPI events.
- Timer works using logical frames.
- Query provides useful reusable world/object queries.
- Task supports at least one multi-frame gameplay action.
- UnitController composes existing unit commands without duplicating native AI.
- Session reset cleans framework state.
- Lua callback errors are isolated from the game process.
- Engine object invalidation is handled safely.
- The Tactical Patrol showcase works in the real game.
- Existing verified functionality does not regress.

## Development Rule

Use a vertical slice for each abstraction:

```text
Gameplay need
      ↓
Try existing primitive
      ↓
Lua abstraction
      ↓
Showcase consumer
      ↓
Runtime test
      ↓
Identify real limitation
      ↓
Minimal native change if required
```

Do not add framework features because they look useful in theory.

## Architecture Boundary

```text
                 C++
┌──────────────────────────────────────┐
│ Engine hooks                         │
│ Pointer safety                       │
│ Lifecycle                            │
│ Native state                         │
│ Performance-critical primitives      │
└──────────────────┬───────────────────┘
                   │
                   ▼
                 Lua
┌──────────────────────────────────────┐
│ EventBus                             │
│ Timer                                │
│ Query                                │
│ Task                                 │
│ UnitController                       │
│ Gameplay rules                       │
└──────────────────┬───────────────────┘
                   │
                   ▼
                 Mods
```

The long-term direction is:

```text
Engine Access
      ↓
Safe Native API
      ↓
Reactive Event System
      ↓
Gameplay Framework
      ↓
Extension / Override System
      ↓
Reusable Tactical Systems
```

Milestone 14 is the transition from exposing engine capabilities to making those capabilities practical to use.