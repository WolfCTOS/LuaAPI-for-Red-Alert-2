# Showcase: Tactical Patrol (Milestone 14)

**One Lua file. No INI. No TeamTypes. No Triggers. No C++.** Built on the new
Lua Gameplay Framework.

## The Task

Show that the M14 framework reduces gameplay-script complexity. A combat unit
patrols two flanks around a base point. When an enemy enters scanner range it
breaks off to attack, then automatically resumes patrolling once combat ends.

The point is **not** the patrol. The point is that the whole loop is now
expressed by **composing framework primitives** instead of a hand-rolled state
machine with repeated engine-guarding boilerplate.

## Architecture (the point of the showcase)

```text
Tactical Patrol (gameplay / decisions — Lua, this mod)
        ↓
   UnitController          (one unit: patrol / attack / stop, task lifecycle)
        ↓
       Task                (sequence / loop of MoveTo + Attack)
        ↓
     Query                 (nearest_enemy / enemies_in_range predicates)
        ↓
  EventBus / Timer         (unit lifecycle events, decision throttle)
        ↓
   existing LuaAPI primitives (MoveTo / Attack / Stop / GetId / GetOwner …)
```

Each concern in the loop lives in exactly one framework layer. The mod file
contains **no** manual `UnitController` state machine, no repeated
`safeAttack(jet, target, id, frame, modeMsg)` guards, and no inline
`if owner ~= player and not owner:IsAlliedWith(player)` filtering.

## How It Works

1. **Select a unit** — on the first `Update()`, the mod finds the first eligible
   player combat unit (skipping MCVs). If none exists it spawns one (`LTNK`)
   next to the player's first building.
2. **Anchor + patrol route** — a base point is chosen (player's first building,
   or the unit's spawn position). Two patrol flanks are generated at ±6 cells.
3. **Drive the controller** — every frame the mod calls
   `controller:update(frame)`, which advances whichever task (patrol or attack)
   is active.
4. **Decision pass (every 10 frames)** — detection is the *high priority*:
   `Query.nearest_enemy(unit, 35)` runs on each decision frame and **interrupts
   an active patrol** when an enemy appears (`controller:attack(enemy)`).
   When nothing is in range and no task is running, it restarts the patrol.
5. **Return to patrol** — when the attack task finishes (target destroyed, or
   the unit no longer holds it), the controller calls the mod's `onTaskDone`
   hook, which calls `controller:patrol(...)` to resume.
6. **EventBus** — `framework.enableUnitEvents(30)` plus an
   `EventBus.on("unit_destroyed", ...)` handler log unit losses (the event
   carries an id + value snapshot, never a stale pointer).

## Framework API Used

```text
Framework.update(frame)
Framework.enableUnitEvents(interval)
Framework.EventBus.on("unit_destroyed", function(id, snap) ... end)
Framework.UnitController.new(unit, { onTaskDone = ... })
controller:unit()
controller:patrol(points)
controller:attack(target, opts)
controller:update(frame)
controller:has_task()
controller:get_mode()
Query.nearest_enemy(unit, radius)
```

## The Same Task Without the Framework

Writing the same patrol loop directly against the native API (as the earlier
`patrol_demo` / `dynamic_objective_defense` showcases do) requires:

- a `seen`/`anchor`/`leg`/`lastAttack` set of per-unit id-keyed tables;
- explicit cooldown bookkeeping;
- repeated `isEnemyOf(player, candidate)` filters with neutral/civilian guards;
- an inline `stop → attack → resume patrol` micro state machine.

The framework replaces those with `UnitController:patrol(...)`,
`UnitController:attack(...)`, `Query.nearest_enemy(...)`, and `onTaskDone`.

## Run It

Enable `tactical_patrol` in the launcher and start a skirmish. A player combat
unit (spawned if needed) will begin patrolling around your base. Send an enemy
next to the route and observe:

```text
[TACTICAL] patrolling around (50,50)
[TACTICAL] enemy detected: TANK, engaging
[TACTICAL] combat ended (completed), resuming patrol
[TACTICAL] unit #1234 (TANK) destroyed
```

Watch `LuaAPI.log` for `[TACTICAL]` and `[FRAMEWORK]` lines.

## Verification Status

> ⚠️ **In-game runtime verification is still pending.** The framework and this
> showcase were verified with a deterministic Lua 5.4 harness that mocks the
> engine objects (`World.GetUnitsInRadius`, `unit:MoveTo/Attack/Stop/IsAlive`,
> `house:IsAlliedWith`), confirming: event ordering & error isolation, timer
> cadence and cancellation, query enemy/ally filtering, task lifecycle states,
> and the controller's **engage → kill → resume-patrol** cycle. The final step —
> launching Yuri's Revenge 1.001 with this mod active and confirming the
> `[TACTICAL]` sequence above — must still be completed in-game.

## Related

- [`FRAMEWORK.md`](FRAMEWORK.md) — the M14 framework API reference
- [`API.md`](../API.md) — native LuaAPI reference
- [`PROJECT/ROADMAP.md`](../PROJECT/ROADMAP.md) — milestone status
