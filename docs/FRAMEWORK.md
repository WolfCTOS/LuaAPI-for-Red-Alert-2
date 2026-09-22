# 🧩 LuaAPI Gameplay Framework (Milestone 14)

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001  
> **API Version:** `2.0.0`
> **Milestone:** 14 — Lua Gameplay Framework

This document describes the **Lua-side gameplay framework** built on top of the
native LuaAPI bindings. It is a thin, composable abstraction layer for common
gameplay needs — events, timers, spatial queries, multi-step tasks, and
per-unit control.

> **Important distinction:** the *native* API answers "what can the engine
> safely expose?" The *framework* answers "how can a modder conveniently use
> those primitives?" The framework is written entirely in Lua and adds **no new
> native bindings**. It composes the existing `World` / `Techno` / `House` /
> `Engine` methods.

---

## Architecture

```text
C++ Native Layer  (LuaAPI.dll — safe engine access, lifecycle, hooks)
        ↓
Engine primitives (World.GetUnitsInRadius, unit:MoveTo, unit:Attack, …)
        ↓
Lua Gameplay Framework
├── EventBus      framework/event_bus.lua
├── Timer         framework/timer.lua
├── Query         framework/query.lua
├── Task          framework/task.lua
├── UnitController framework/unit_controller.lua — ⚠️ NOT A MODULE: this file
│                 returns the TacticalPatrol demo (`UnitControl = nil` by its
│                 own header); no `UnitController.new` exists
├── CombatState   framework/combat_state.lua   (M14 Gate 1 — stateful combat tracking)
├── Tactical      framework/tactical.lua       (M14 Gate 2 — reactive tactical decision)
├── ForceGroup    framework/force_group.lua    (M14 Gate 3 — multi-force/group manager)
└── init          framework/init.lua — ⚠️ DOES NOT EXIST IN THE TREE (no
                  `Framework.update` driver, no `enableUnitEvents`)
        ↓
Lua Mods         (scripts/mods/<id>/main.lua)
```

Each component is small and independent. M14 intentionally does **not** add a
Behavior Tree, GOAP, or a coroutine scheduler — those are higher-level systems
that build on these primitives (planned for M15).

---

## Loading the framework

The ModLoader sets `package.path` to the `scripts/` directory, so any component
can be `require()`d by name:

```lua
local EventBus       = require("framework.event_bus")
local Timer          = require("framework.timer")
local Query          = require("framework.query")
local Task           = require("framework.task")
local ForceGroup     = require("framework.force_group")
local util           = require("framework.util")
-- NOTE: require("framework.init") does NOT exist (no Framework.update driver).
-- NOTE: require("framework.unit_controller") returns the TacticalPatrol demo,
-- not a controller module (UnitControl = nil by its own header).
```

Or use the aggregate table (`Framework.EventBus`, `Framework.Timer`, …) —
only if an entry module providing it exists; today it does not, so require
leaf modules directly.

---

## Session lifecycle

The Lua VM is created ONCE per process (`std::call_once` in
`src/lua_engine.cpp`); `ResetSession()` (VM teardown, house-cache clear)
exists but has ZERO callers — it is dead code. There is therefore NO
automatic per-match state reset: scripts load ONCE per process
(`std::call_once`), so every mod owns its match lifecycle and must detect
restarts itself (frame-backwards guard, the Command Authority pattern).
House userdata cached in C++ can go stale on match 2+ in one process (open
Gate 1 item 3). A mod that keeps cross-match tables without a restart guard
will misbehave on the second match in the same process. `Framework.reset()`
is documented below as part of the (nonexistent) entry module — until such
a module exists, each mod resets its own tables.

`Framework.reset()` is provided for explicit hygiene and is called by a mod when
it wants to clear framework state mid-session (e.g. when a new match begins under
its own control).

```text
Scenario start → frame loop:
                   Framework.update(frame)   ← advance timers / event tracker
                   (mod gameplay logic)
Scenario restart/exit → VM recreated → next session starts clean
```

> ⚠️ Of the native globals, only `OnScenarioStart` is actually dispatched
> (once, at logical frame 1). `OnPreDamage` is collected but never invoked,
> and `OnUnitDestroyed` is never dispatched at all (see `API.md` — both are
> contracts without invocation). The framework therefore initialises lazily
> on the first `Update()`, matching how existing mods already handle
> scenario start. Polling-based `unit_created` / `unit_destroyed` behind the
> EventBus is the documented direction, but `framework/init.lua`
> (`Framework.enableUnitEvents` / `Framework.update`) does not exist in-tree
> and no code emits those names today — treat them as design intent, not API.

---

## Safety model

The framework never stores raw engine pointers or trusts long-lived userdata:

- **Units are tracked by id** (`unit:GetId()`, a native UniqueID) and re-resolved
  from a fresh `World` scan each frame. A controller only exposes an object it
  just validated.
- **Polled death reports carry value snapshots** (`id`, `typeName`,
  `ownerName`, `x`, `y` via `combat_unit_invalidated`) — never a
  possibly-dangling Techno userdata.
- **Every callback is `pcall`-wrapped** and isolated (EventBus handlers, Timer
  callbacks, Task steps, `onTaskDone`), so one failure cannot unwind the frame.
- Neutrals / civilians / civilian vehicles are never treated as enemies by the
  default predicates.
- All engine access builds on the native validity checks (`IsAlive`) — treat
  every `Techno*` as disposable.

---

## `EventBus` — callback subscriptions

A small, callback-safe pub/sub for gameplay events. The native engine callbacks
are global and last-write-wins; EventBus is a per-session, per-mod subscription
layer.

```lua
local EventBus = require("framework.event_bus")

local sub = EventBus.on("unit_created", function(unit) ... end)   -- returns subscription id
EventBus.off("unit_created", sub)          -- remove by id
EventBus.off("unit_created", handler)      -- remove by function
EventBus.emit("my_event", data)            -- dispatch
EventBus.listenerCount("unit_created")     -- diagnostics
EventBus.reset()                           -- clear all listeners
```

**Guarantees:**

- Multiple listeners per event, dispatched in **subscription order**.
- **Error isolation** — a failing handler is logged and skipped; the rest run.
- Safe mutation during dispatch — handlers added/removed mid-emit apply to the
  next emit.
- No engine references retained — the bus stores functions only.

---

## `Timer` — frame-based scheduling

Timers use **logical game frames**, not wall-clock time, so they are
deterministic and safe in CnCNet multiplayer (no `os.time`/`os.clock` OOS).

```lua
local Timer = require("framework.timer")

local once    = Timer.after(60, function(frame) ... end)    -- fire once after 60 frames
local repeat  = Timer.every(15, function(frame) ... end)   -- fire every 15 frames
Timer.cancel(once)
Timer.update(frame)   -- driven by Framework.update, or call manually
Timer.reset()
```

- `Timer.after(n, fn)` — once.
- `Timer.every(n, fn)` — repeats every n frames.
- `Timer.at(frame, fn)` — fire at an absolute future frame.
- Callbacks receive the logical frame; failures are isolated.
- Cancellation is idempotent and safe during dispatch.
- No native hooks: the scheduler is entirely Lua (driven once per logical frame).

---

## `Query` — gameplay queries

Composable predicates around the native spatial API. These answer "which of the
objects are enemies / allies / of a house / of a type / nearest".

```lua
local Query = require("framework.query")

local list    = Query.all_in_range(unit, 300)            -- every object in range
local enemies = Query.enemies_in_range(unit, 300)       -- legitimate enemies
local allies  = Query.friendlies_in_range(unit, 300)    -- friendly objects
local target  = Query.nearest_enemy(unit, 500)          -- single nearest enemy
local friend  = Query.nearest_friendly(unit, 500)
local owned   = Query.units_by_house(player)            -- by house (whole map)
local types   = Query.units_by_type({ TANK = true })    -- by type (whole map)
local custom  = Query.units_matching(function(u) ... end) -- arbitrary filter
```

Options (`opts` table, optional last arg):

- `reference` — override the reference house/unit.
- `includeBuildings` — include buildings in scans (default: mobile only).
- `radius` / `x`,`y` — for point queries.

> ⚠️ **Performance:** radius scans traverse `TechnoClass::Array`. Whole-map
> scans (`units_by_house` / `units_by_type` / `units_matching`) traverse the
> entire map. Throttle these (e.g. every 10–30 frames) instead of calling them
> every frame.

`Query.is_enemy(unitOrHouse, candidate)` and `Query.is_ally(...)` are exposed
for direct use.

---

## `Task` — multi-step actions

A minimal task primitive so mods don't hand-roll a state machine for every
maneuver.

**Node status values:** `"running"` · `"done"` · `"failed"` · `"cancelled"`.

```lua
local Task = require("framework.task")

local task = Task.create({
    Task.MoveTo(x1, y1),            -- move until reached / idle / timeout
    Task.Wait(30),                  -- pause n frames
    Task.Attack(enemy),             -- attack until combat concludes
})

local status = task:update(unit, frame)   -- drives one step
task:cancel()
task:get_state()        -- created | running | completed | cancelled | failed
```

Composites:

- `Task.Sequence({ ... })` — run nodes in order.
- `Task.Loop({ ... })` — repeat a sequence forever (indefinite patrol).
- `Task.Fn(callback)` — a custom step (`fn(unit, frame) -> status`).

`Task.create` wraps a single node or an array into a runnable **task object**
with the lifecycle states above. MoveTo captures only coordinates; Attack
captures the target and re-checks `IsAlive()` every frame, treating a vanished
target as `done` — so no step pins a stale `TechnoClass*`.

---

## `CombatState` — stateful combat tracking (M14 Gate 1)

A reliable, state-based *observation* layer for combat units. It answers the
questions a future tactical AI will ask — **without making any decision**:

* is this unit still alive?
* how much HP does it have / what's its max?
* who owns it?
* what is it currently targeting?
* where is it?
* is it attacking / moving / idle (what mission)?
* did its target disappear?
* did the unit itself disappear?

```lua
local CombatState = require("framework.combat_state")

local t = CombatState.new({ eventBus = Framework.EventBus })

local id = t:track(someUnit)   -- add a live unit; returns its native id
t:update(frame)                -- refresh every logical frame

local s = t:get(id)            -- snapshot: hp, maxHp, ownerName, x, y, mission,
                               --      isIdle, isAttacking, targetId, typeName…
if t:is_alive(id) then … end
if t:target_is_alive(id) == false then … end   -- target vanished
local dead = t:drain_invalidated()             -- units destroyed this frame
```

**Safety contract (the core of Gate 1):**

* Units are tracked by **id only**. The tracker stores only primitives
  (number / string / boolean). It never holds a `Techno` or `House` userdata.
* Every `update(frame)` re-resolves each tracked unit from a **fresh** `World`
  scan, mirroring the M12/M14 showcase pattern. Engine userdata live for exactly
  one scan; nothing long-lived is retained.
* Targets are tracked by id and re-validated against an authoritative all-techno
  scan, so a **destroyed target never stays silently valid**.
* Destruction / invalidation is **deferred**: invalid records are collected
  during the scan and physically removed only after iteration completes.
* If a unit cannot be read (destroyed mid-scan), it is treated as invalid rather
  than throwing — every engine call in a snapshot is `pcall`-guarded.

**Key methods:**

| Method | Purpose |
|---|---|
| `new(opts)` | `eventBus`, `keepDead` (default true), `invalidTtl`, `pulseEvery` |
| `track(unit)` / `trackById(id)` | register a unit by its live object or by id |
| `untrack(id)` | stop tracking |
| `update(frame)` | refresh all records, detect destruction / target loss |
| `get(id)` | current snapshot for a live unit (or nil) |
| `get_last_valid(id)` | last known-good snapshot even after destruction |
| `is_alive(id)` | is the unit currently alive |
| `target_is_alive(id)` | `nil`=no target / unknown, `true`=target live, `false`=target gone |
| `target_id(id)` | the current target id (or nil) |
| `has(id)` / `count()` / `ids()` | bookkeeping |
| `drain_invalidated()` | list of records invalidated this update |
| `prune()` / `clear()` | remove invalid records / reset the tracker |

`update()` emits `combat_state_changed` and `combat_unit_invalidated` on the
supplied `eventBus` (if any); handlers are `pcall`-isolated.

---

## `Tactical` — reactive tactical decision (M14 Gate 2)

A small, data-driven evaluator that answers one question on a periodic
reassessment pulse:

> "Has the battlefield changed enough that my current attack should be
> reconsidered?"

It does **not** run a full AI. It turns an observed local battlefield snapshot
into a coarse action:

```text
continue      - keep attacking the current target
retreat       - the local fight is unfavourable; disengage / pull back
changetarget  - current target is irrelevant, or a better/safer one exists
find_target   - we have no target to attack
disengage     - no effective combat force left
```

```lua
local Tactical = require("framework.tactical")
local decision = Tactical.new({ pulseEvery = 15, radius = 18 })

local snap = Tactical.buildSnapshot(tracker, house, { radius = 18 })  -- pure snapshot
local res  = decision:reassess(snap, frame)   -- throttled to pulseEvery

if res.changed and not res.clamped then
    -- res.decision, res.tier, res.reason, res.metrics are authoritative this pulse
end
```

**Why this is generic (not "if Boris then retreat"):** the control flow only ever
reads `own` force, `enemies`, and `target`. Units that genuinely need special
treatment are scored through a single **data table** (`THREAT_VALUES` keyed by
typeName) rather than scattered branches — so a hero, a heavy tank, and a flak
turret each raise the local threat score, and the decision shifts via
`ownPower / enemyThreat`, not via a unit-name rule.

**Target priority (minimum):** `target_value()` marks neutral/civilian buildings
(and civilian vehicles) as `irrelevant`, so an obviously low-value target is not
blindly attacked; strategic structures (oil derrick, air command, etc.) remain
valid. This is the only target-priority concept Gate 2 introduces.

**Reassessment pulse:** `reassess(snap, frame)` only runs a full evaluation on
`pulseEvery`-frame boundaries. Between pulses it returns the previous decision
with `clamped == true` and `changed == false`, so callers execute the *current*
decision without re-logging every frame. `changed` is `true` only when the local
battlefield signature (own alive/hp, enemy composition, target) or the clamped
decision category flips.

**Safety:** `evaluate()`/`target_value()`/`enemy_threat()` are **pure** — they
operate only on primitive snapshot fields and never hold a `Techno`/`House`
userdata. `buildSnapshot(tracker, house, opts)` composes Gate 1's
`CombatStateTracker` (own force) with a fresh `World.GetUnitsInRadius()` scan for
local enemies, resolves the current target by id, and returns only primitives.
Destroyed / invalid objects never remain valid targets.

### RETREAT is a real disengagement, not `Stop()`

`RETREAT` (the Gate 2 runtime fix) is implemented as a **lasting, per-force
state**, not a one-shot `Stop()`:

```text
RETREAT
  -> clear the locked target (no re-acquire)
  -> compute weighted hostile centroid from the snapshot enemies
  -> retreat vector = normalize(own_pos - hostile_centroid)
  -> destination = own_pos + unit_vector * RETREAT_DIST (clamped to on-map cells)
  -> issue a real MoveTo(destination)
  -> hold retreat for `retreatHoldFrames` (default 90) so the vanilla attack
     preference does not instantly cancel it
  -> re-assert MoveTo while retreating (a Move mission suppresses Attack in RA2)
  -> release only when a later decision is non-RETREAT after the hold elapses
```

The hostile centroid, retreat vector, and destination are all computed in **map
cells** (the API's `GetPosition()`/`MoveTo()` unit), using only primitive
snapshot data — `buildSnapshot()` exposes enemy `x,y` for exactly this. If no
hostile coordinates are resolvable, the force holds position (`Stop`) rather than
attacking. No unit-specific logic: the Apocalypse/Boris/Flak scenarios all flow
through the same generic threat score → RETREAT path.

In-game showcase: `scripts/mods/tactical_reassess/` (logs `[TACTICAL]` lines).

---

## `ForceGroup` — multi-force / group manager (M14 Gate 3)

A reusable manager that coordinates **several independent attack-force groups**
(squads), each with its own `CombatStateTracker` (Gate 1) and `TacticalDecision`
evaluator (Gate 2), all driven from ONE per-frame update. Each group runs the
full **OBSERVE → EVALUATE → DECIDE → ACT → REASSESS** loop independently, so two
groups in different places can reach **different decisions on the same frame**.

```lua
local ForceGroup = require("framework.force_group")

local mgr = ForceGroup.new({ radius = 18, pulseEvery = 15 })
local alpha = mgr:add_group({ id = "alpha", getHouse = function() return House.GetPlayer() end })

alpha:add_member(unit)            -- track a unit by id
alpha:add_member_by_id(1234)

function MyMod.Update(frame)
    mgr:update(frame)             -- advance every group's Observe/Eval/Decide/Act/Reassess
    local d = alpha:decision()    -- current decision ("continue" | "retreat" | ...)
end
```

**Group API:** `add_member` / `add_member_by_id` / `remove_member`, `has` /
`is_alive` / `get` / `count` / `ids` / `target_id`, `centroid`, `decision`,
`set_handler("onDecision" | "onAct", fn)`, `update(frame)`, `reset`.

**Manager API:** `new(opts)` (shared tuning), `add_group(opts)`, `group(id)`,
`remove_group(id)`, `group_ids()`, `update(frame)`, `reset()`. A failing group is
`pcall`-isolated so it does not break the others.

**Safety / genericity:** units are tracked by **id only** (via Gate 1); the
manager never retains a `Techno` or `House` userdata. The house used for enemy
detection is resolved through the `getHouse` callback on every pulse, so no House
userdata is held persistently. The action handler is **type-agnostic** — it maps
the Gate 2 decision to `MoveTo` / `Attack` / `Stop`; there is no
`if typeName == "..."` branch. Retreat behaviour (a real `MoveTo` away from the
hostile centroid, held for a window so it is not instantly overwritten) is the
same generic logic as Gate 2, applied per group.

This is the minimum manager. It deliberately does **not** implement platoons,
commanders, morale, reinforcement, economy/production, or strategic-map AI.

In-game showcase: `scripts/mods_archive/multi_force/` (archived; logs `[MULTIFORCE]` lines).

---

## `UnitController` — one-unit control (DESIGN REFERENCE, NOT IN TREE)

> The module below does not exist: `scripts/framework/unit_controller.lua`
> returns the TacticalPatrol demo (`UnitControl = nil` by its own header).
> The interface is preserved here as the design contract for a future
> implementation. Do not `require` it expecting a controller.

Combines movement, targeting, tasks, and queries for a single unit. It exposes
control **primitives**, not decisions: *what* to do is chosen by the mod's Lua
layer; the controller executes and reports completion.

```lua
local UnitControl = require("framework.unit_controller")

local c = UnitControl.new(unit, { onTaskDone = function(task, status, mode) ... end })

c:move_to(x, y)      -- one-shot move
c:attack(enemy)      -- attack until combat concludes
c:patrol({ {x=10,y=10}, {x=40,y=10} })  -- indefinite loop
c:stop()             -- cancel task + order Stop
c:task(t)            -- run an arbitrary Task object
c:update(frame)      -- advance (call every frame)
c:unit()             -- current validated unit, or nil if gone
c:is_alive()         -- is the controlled unit still alive
c:get_mode()         -- "move" | "attack" | "patrol" | "idle" | nil
c:has_task()         -- is a task still running
```

Safety: the controller tracks the unit by **id** and re-resolves it each frame.
If the unit dies, `unit()` returns `nil` and the controller becomes inert — it
never surfaces a stale userdata.

`onTaskDone(task, status, mode)` is the single point where the controller hands
control back to Lua (used by the showcase to auto-resume patrol after combat).

---

## Framework integration (`framework/init.lua` — DOES NOT EXIST)

> `scripts/framework/init.lua` is not in the tree: there is no
> `Framework.update` driver, no `enableUnitEvents`, no aggregate
> `Framework.*` table, no `Framework.reset`. The contract below is the
> design reference for a future entry module. Require leaf modules
> directly (see "Loading the framework").

The entry point that glues the components together and drives them on one tick.

```lua
local Framework = require("framework.init")

function MyMod.Update(frame)
    Framework.update(frame)          -- advance timers + (opt) unit tracker
    Framework.enableUnitEvents(30)   -- enable unit_created / unit_destroyed
    Framework.EventBus.on("unit_destroyed", function(id, snap) ... end)
    ... gameplay ...
end
```

- `Framework.update(frame)` — advance scheduler + event tracker.
- `Framework.enableUnitEvents(interval)` — poll `World.GetUnits` and emit
  `unit_created` (fresh, valid userdata) / `unit_destroyed` (id + snapshot).
  The first scan **seeds silently** so you don't get a `unit_created` burst for
  units already on the map.
- Component sub-tables for convenience (`Framework.EventBus`, `Framework.Timer`,
  `Framework.Query`, `Framework.Task`, `Framework.UnitController`,
  `Framework.util`) — or `require` them directly.
- `Framework.controller(unit, opts)` — shortcut for `UnitController.new`.
- `Framework.getFrame()` — the last logical frame driven.
- `Framework.reset()` — clear all framework state.

---

## `util` — shared helpers

`util.safe_call`, `util.is_alive`, `util.is_idle`, `util.is_mobile`,
`util.is_enemy`, `util.is_ally`, `util.log_error` / `util.log_info`
(routed to `LuaAPI.log`), `util.distance`, `util.near`, `util.kind_of`. These
are the low-level predicates every other component builds on.

---

## Quick example — a guard that defends only when threatened

> Uses leaf requires only (`framework.init` and the UnitController module
> do not exist — see above).

```lua
local Query = require("framework.query")
local Timer = require("framework.timer")

local guard = nil  -- validated userdata, re-resolved every frame

function MOD.Update(frame)
    Timer.update(frame)

    if not guard or not guard:IsAlive() then
        guard = nil
        local units = World.GetUnits()
        for _, u in ipairs(units) do
            if u:IsAlive() and u:GetOwner() and u:GetOwner() == House.GetPlayer() then
                guard = u
                break
            end
        end
    end

    if guard then
        local e = Query.nearest_enemy(guard, 300)
        if e then guard:Attack(e) end
    end
end

return MOD
```

---

## Verification status

| Component | Logic verified (Lua harness) | In-game runtime | 
|---|---|---|
| EventBus | ✅ | Need to test |
| Timer | ✅ | Need to test |
| Query | ✅ | Need to test |
| Task | ✅ | Need to test |
| UnitController | ❌ NOT IN TREE (design reference only) | n/a |
| CombatState (M14 Gate 1) | ✅ | Need to test |
| Tactical (M14 Gate 2) | ✅ | **Need to test** |
| ForceGroup (M14 Gate 3) | ✅ | Need to test |
| init / unit tracker | ❌ NOT IN TREE (design reference only) | n/a |
| Tactical Patrol showcase | ✅ | Need to test (mod archived) |

"Logic verified" means the component was driven through a deterministic Lua 5.4
harness with mocked engine objects (event ordering, timer cadence, query
filtering, task lifecycle, controller engage/resume). "**Need to test**" marks a
component whose live in-game runtime behaviour has **not** yet been verified in a
Yuri's Revenge 1.001 session. **Final in-game verification still requires
launching Yuri's Revenge 1.001** — see
`SHOWCASE_TACTICAL_PATROL.md` for the exact steps and the `LuaAPI.log` evidence
to look for.

---

## Compatibility

The framework is **additive and opt-in**: existing mods that never `require`
framework modules are completely unaffected. It registers no globals (module
returns are locals), so it cannot collide with a mod's global functions. No
native bindings were added, removed, or renamed.

---

## Related

- [`API.md`](../API.md) — native LuaAPI reference
- [`SHOWCASE_TACTICAL_PATROL.md`](SHOWCASE_TACTICAL_PATROL.md) — the M14 showcase
- [`PROJECT/ROADMAP.md`](../PROJECT/ROADMAP.md) — milestone status
- [`docs/TUTORIAL.md`](TUTORIAL.md) — beginner tutorial
