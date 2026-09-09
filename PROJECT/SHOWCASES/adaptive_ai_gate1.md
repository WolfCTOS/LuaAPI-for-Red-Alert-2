# Gate 1: Runtime Boundary Audit

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001 · LuaAPI dev line `1.1.0`
> **Purpose:** Determine whether the *current* verified LuaAPI can already drive an
> Adaptive AI vertical slice, and identify the exact missing capability (if any).
> **Method:** Source is authority. Implementation verified first; documentation
> treated as evidence, not proof. No code was changed; Smart AI untouched.

---

## Status

**PASS** — with one explicitly documented *runtime* boundary (not a missing API).

The current LuaAPI already exposes every primitive needed to build the
Observe → Classify → Choose Response → Control → Observe Again → Adapt loop as a
pure-Lua mod running off `Update(frame)`. **No new C++ binding is required.**

The one boundary that is *not* an API gap is behavioural: holding an
engine-AI house to Lua's orders against that house's own vanilla AI. This is
already documented in `PROJECT/RUNTIME_BOUNDARY.md` (the "second half" of the
target-selection boundary) and in `miner_safety`/`ROADMAP.md` Gate 12.3. It does
not block the decision-and-adapt loop; it bounds how long a single order persists.

---

## Question

What exact runtime behaviour are we testing?

> Can Lua, using the **current** verified LuaAPI, run this loop **continuously and
> statefully during gameplay**:
> 1. observe the player's army,
> 2. build a small strategic state from it,
> 3. classify the player's strategy,
> 4. select a response strategy,
> 5. control Lua-driven units per that response,
> 6. observe the world again later,
> 7. detect that the strategy changed,
> 8. change the AI response?

The example is: many tanks → detect `ARMOR_HEAVY` → pick an anti-armour response;
later heavy AA added → detect composition change → switch to an anti-air response.
This is **deterministic classification, not machine learning**.

---

## Current API

Status key: **VERIFIED** = present in source AND exercised by an existing working
showcase/harness; **UNVERIFIED** = present in source but not demonstrated working
live; **NOT AVAILABLE** = no current capability.

### World observation

| Capability | Status | Evidence | Relevant file / API |
|---|---|---|---|
| Enumerate units | VERIFIED | `World.GetUnits()` reads `TechnoClass::Array`; used by `smart_ai`, `tactical_patrol`, `dynamic_objective_defense` | `src/bindings_techno.cpp:962` `World.GetUnits` |
| Enumerate all technos incl. buildings | VERIFIED | `World.GetAllUnits()`; used by `CombatStateTracker` target-liveness scan | `src/bindings_techno.cpp:978` `World.GetAllUnits` |
| Enumerate buildings | VERIFIED | `World.GetBuildings()` reads `BuildingClass::Array`; used by `tactical_patrol` base anchor, `dynamic_objective_defense` objective scan | `src/bindings_techno.cpp:956` `World.GetBuildings` |
| Query units by radius | VERIFIED | `World.GetUnitsInRadius(x,y,r)`; used by `smart_ai`, `tactical.lua`, `query.lua` | `src/bindings_techno.cpp:637` `World.GetUnitsInRadius` |
| Identify owner / house | VERIFIED | `unit:GetOwner()`; used throughout (`smart_ai`, `query`, `task`) | `src/bindings_techno.cpp:171` `unit:GetOwner` |
| Identify unit type | VERIFIED | `unit:GetTypeName()` (INI ID) and `unit:GetKind()` (building/unit/infantry/aircraft) | `src/bindings_techno.cpp:91`, `:215` `unit:GetTypeName` / `unit:GetKind` |
| Get position | VERIFIED | `unit:GetPosition()` → {x,y,z} in map cells; used by `smart_ai`, `query`, `tactical` | `src/bindings_techno.cpp:179` `unit:GetPosition` |
| Get health / alive state | VERIFIED | `GetHealth`, `GetMaxHealth`, `IsAlive` (validates Health>0 + `!InLimbo`) | `src/bindings_techno.cpp:100`, `:108`, `:197` |

### House / player information

| Capability | Status | Evidence | Relevant file / API |
|---|---|---|---|
| Identify houses | VERIFIED | `House.GetPlayer` / `House.GetCount` / `House.GetByIndex`; house userdata is registry-cached so `owner == player` identity holds | `src/bindings_house.cpp:79`,`:88`,`:94` |
| Get credits / resources | VERIFIED | `house:GetCredits` / `AddCredits` / `SetCredits` / `GetPowerOutput` / `GetPowerDrain` | `src/bindings_house.cpp:111`..`155` |
| House name | VERIFIED | `house:GetName()` → INI house ID (`get_ID()`) | `src/bindings_house.cpp:158` |
| Player vs AI controlled | VERIFIED | `house:IsHuman()` → `IsControlledByHuman()` | `src/bindings_house.cpp:165` |
| Alliance check | VERIFIED | `house:IsAlliedWith(other)`; used by `util`, `smart_ai`, `query` | `src/bindings_house.cpp:287` |

### Unit control

| Capability | Status | Evidence | Relevant file / API |
|---|---|---|---|
| MoveTo | VERIFIED | `unit:MoveTo(x,y)` set Destination + `QueueMission(Move)`; used by `task.lua`, `tactical_reassess` (RETREAT vector) | `src/bindings_techno.cpp:391` |
| Attack | VERIFIED | `unit:Attack(target)` → `SetTarget` + `QueueMission(Attack)`. Deliberately **avoids** the disabled `Active_Click_With` hook | `src/bindings_techno.cpp:467` |
| Stop | VERIFIED | `unit:Stop()` clears target/destination + `QueueMission(Stop)` | `src/bindings_techno.cpp:495` |
| Target access | VERIFIED | `unit:GetTarget()` (validated), `unit:IsAttacking()`, `unit:IsIdle()`, `unit:GetMission()` | `src/bindings_techno.cpp:557`, `:536`, `:517`, `:441` |
| Hunt | VERIFIED | `unit:Hunt()` → `QueueMission(Hunt)` | `src/bindings_techno.cpp:424` |
| Spawn units | VERIFIED | `house:SpawnUnit(typeId,count,x,y,facing,force,action)`; Gate 11.4, `spawn_test` | `src/bindings_house.cpp:313` |
| Other control | VERIFIED | `Scatter`, `Disable`, `IronCurtain`, `TakeDamage`, `FireProjectile`, sub-turrets (`AddSubTurret`/`SetSplitTargets`/`FireSplitSalvo`) | `src/bindings_techno.cpp:369`,`:321`,`:880`,`:261`,`:804`,`:685`+ |

### Runtime execution

| Capability | Status | Evidence | Relevant file / API |
|---|---|---|---|
| Continuous per-frame Lua | VERIFIED | `Hooked_MainLoop` → gated to **once per logical frame** (`Unsorted::CurrentFrame` change) → `OnGameFrame()` → `OnTick(frame)` | `src/lua_engine.cpp:212`, `:238-246`, `:1158-1187` |
| Mod `Update` dispatch | VERIFIED | `scripts/init.lua` `OnTick` calls each loaded mod's `Update(frame)` inside `pcall`, once per frame | `scripts/init.lua:90-132` |
| In-match guard | VERIFIED | `IsInGameMatch()` requires active Scenario + `HouseClass::CurrentPlayer` + frame>0; main menu stays responsive | `src/lua_engine.cpp:918` |
| Logical-frame timing (no OOS) | VERIFIED | Gate 11.3 / M5.2; `TIMING` uses `Unsorted::CurrentFrame`. Determinism rule (`os.time`/`os.clock` forbidden) | `src/lua_engine.cpp:240`; `API.md:875` |
| Timer / polling primitives | VERIFIED | `Framework.Timer.after/every/at` + `Framework.update(frame)`; framework driven from a mod's `Update` | `scripts/framework/timer.lua`, `init.lua` |

**Note — the only reliably-dispatched mod callback is `Update(frame)`.** The native
`OnScenarioStart` / `OnUnitDestroyed` / `OnPreDamage` global callbacks are **not
wired up for mod tables** in the current build (M13 restoration is deferred).
The framework therefore lazy-initialises on the first `Update()`, and provides
`unit_created` / `unit_destroyed` via a low-frequency poll behind the EventBus.
An Adaptive AI mod must follow this same pattern (lazy init in `Update`).

### Persistent runtime state

| Capability | Status | Evidence | Relevant file / API |
|---|---|---|---|
| Module-level Lua state across frames | VERIFIED | Lua VM is created **once per process** via `std::call_once(g_engineOnce)`; module upvalues / global tables persist for the whole process | `src/lua_engine.cpp:52`, `:1125-1134` |
| State across update cycles | VERIFIED | Same VM → upvalues, tables, RNG keep state between `Update(frame)` calls; `smart_ai` `assignedCooldown` proves it | `scripts/mods/smart_ai/main.lua:16,49` |
| Reset / restart behaviour | UNVERIFIED (contradicted docs) | `ResetSession()` (would `lua_close`) is **defined but never called** anywhere in the codebase. So the VM is NOT auto-recreated per match as `docs/FRAMEWORK.md:69` / `ROADMAP.md:469` claim. State persists across matches within a process | `src/lua_engine.cpp:1190`; grep confirms no caller |
| Savegame implications | VERIFIED (by design) | `OnScenarioStart` does not fire on savegame load. The framework avoids this by lazy init in `Update` + re-resolving units by id from a fresh `World` scan each frame (`CombatStateTracker`), storing primitives only | `scripts/framework/combat_state.lua`; `API.md:867` |

**Discrepancy to record:** the documentation says the VM is recreated on every
scenario reset, but the code creates it once (`call_once`) and `ResetSession` is
never invoked. For an Adaptive AI this is actually *favourable* (state survives a
rematch), but it is a documentation-vs-code contradiction and must be treated as
UNVERIFIED, not trusted either way.

---

## Smart AI baseline

**What it already proves** (`scripts/mods/smart_ai/main.lua`):
- Discover units: `World.GetUnits()` + owner / type / kind filters.
- Identify enemies: owner house ≠ player and not allied (`isEnemyOf`, `:33-39`).
- Select targets: a data-driven `threatScore` (economy / MCV / high-threat data
  table + veterancy bonus + proximity) then `selectBestTarget` by score (`:102-132`).
- Update cadence: every `SCAN_EVERY = 30` frames, `frame % 30` (`:135`).
- Remembers state: `assignedCooldown[id] = frame + ASSIGN_COOLDOWN` (`:16,49,76`).
- Safety: every engine call re-checks `IsAlive` and wraps the update in `pcall`
  (`:56-78, 234-239`).

**What it does NOT prove:**
- It does **not** build a persistent strategic state, nor a classification of the
  *player's strategy* over time.
- It does **not** adapt a *response strategy* — it only switches between two target
  sub-strategies (defend / strike cluster) each scan, choosing the best target now.
- It has no "remember the previous classification, detect it changed, change the
  categorical response" step. So it exercises the **primitives** the slice needs,
  but not the **adaptation loop** itself.
- Its dependency on `_G.CapabilityRegistry` is **intentional** (it demonstrates a
  registry-driven design) but is **not** a required dependency for Adaptive AI.

---

## Framework

Modules reusable by a new Adaptive AI showcase. "Logic verified" = exercised
through the deterministic Lua harness per `FRAMEWORK.md`; "in-game" marks what has
**not** yet been verified in a live YR session. All are pure Lua (no native
bindings).

| Module | Status (as relevant to Adaptive AI) | Why reusable |
|---|---|---|
| `Framework.Query` (`query.lua`) | VERIFIED logic | `units_by_house`, `units_by_type`, `units_matching`, `enemies_in_range`, `nearest_enemy` directly build the observed composition |
| `Framework.CombatState` (`combat_state.lua`) | VERIFIED logic | Id-keyed, primitive-only state snapshots → the "small strategic state" from observation |
| `Framework.Tactical` (`tactical.lua`) | VERIFIED logic | Pure `continue/retreat/changetarget/find_target/disengage` evaluator — a ready-made observe→decide skeleton |
| `Framework.Timer` (`timer.lua`) | VERIFIED logic | Frame-based `after/every/at` — throttled re-observation and response re-issue |
| `Framework.EventBus` (`event_bus.lua`) | VERIFIED logic | Pcall-isolated pub/sub for state-change/adaptation notifications |
| `Framework.UnitController` (`unit_controller.lua`) | VERIFIED logic | One-unit `move_to`/`attack`/`patrol`/`stop`, tracked by id — cleanly issue a chosen response |
| `Framework.Task` (`task.lua`) | VERIFIED logic | Multi-frame `MoveTo`/`Attack`/`Wait` sequences with lifecycle |
| `Framework.ForceGroup` (`force_group.lua`) | VERIFIED logic | Multiple groups each running their own observe→evaluate→decide→act→reassess loop |
| `Framework.util` | VERIFIED logic | `is_enemy`/`is_ally`/`is_alive`/`near` predicates (neutral/civilian-safe) |

> ⚠️ The framework's *live in-game* runtime verification is still pending
> (`FRAMEWORK.md` table = "Need to test"). The **native primitives** it composes
> are the ones already live-verified by `smart_ai` / `tactical_patrol` /
> `dynamic_objective_defense` / `multi_turret_battleship`. A showcase should treat
> the framework modules as convenient, tested-by-harness building blocks and rely
> on the underlying native bindings for in-game certainty — which is exactly how a
> minimal slice is best written.

---

## Minimum vertical slice

A single pure-Lua mod (`scripts/mods/adaptive_counter/main.lua`), registered in
`scripts/active_mods.txt`, requiring **no new C++ bindings**, no rebuild, reloaded
on match start (like every other mod).

```text
Lazy init in first Update():
   - identify the AI house        : iterate House.GetByIndex(i) until !IsHuman()
   - collect the AI house's units : Query.units_by_house(aiHouse) filtered mobile
   - seed classify(playerArmy)     -> store "currentStrategy"

Update(frame) every logical frame (throttled to N=20..30 frames):
   Frame tick (every N frames):
     1. OBSERVE    : playerArmy = Query.units_by_house(House.GetPlayer())
                    build counts by kind/type via a CATEGORY data table
     2. STRATEGY   : counts -> classification string (ARMOR_HEAVY / AIR_HEAVY /
                    MIXED / ...) using a threshold table (pure Lua, data-driven)
     3. CLASSIFY   : set classification from the CATEGORY table
     4. CHOOSE     : strategy -> response id via RESPONSE table
                    (ARMOR_HEAVY  -> "ANTI_ARMOR"),
                    (AIR_HEAVY    -> "ANTI_AIR"),
                    (MIXED        -> "GENERAL")
     5. ADAPT      : if classification != lastClassification then
                       log/announce strategy change via Engine.PrintMessage
                       REISSUE the AI house units with the new response
                    lastClassification = classification
     6. CONTROL    : for each AI-house unit (mobile, alive), on a cooldown
                       issue the response as an order:
                       ANTI_ARMOR -> concentrate Attack on tanks in a radius
                       ANTI_AIR   -> concentrate Attack on aircraft / AA counters
                       GENERAL    -> Attack nearest enemy / Hunt
                    (unit:Attack / unit:MoveTo / unit:Stop as appropriate)
   every frame: pcall + IsAlive re-check on every engine object
```

**Proof of adaptation:** the log contains a monotonic sequence like
`strategy=ARMOR_HEAVY -> response=ANTI_ARMOR`, then later
`strategy=AIR_HEAVY -> response=ANTI_AIR` after the player adds AA. The player's
composition is re-observed each pulse, compared against the remembered
classification, and the response is changed on the flip. This is exactly the
8-step loop asked for.

**Known runtime boundary (does NOT block the slice):** the AI house still runs its
own vanilla AI, which may re-issue orders on top of Lua's. This is the "order
lease" / "second half" boundary documented in `PROJECT/RUNTIME_BOUNDARY.md` —
Lua's API is *command*, not *override* (there is no "hold this target for N
frames" primitive). `miner_safety` (Gate 12.3) hit the same wall. For the slice:
issue orders on a short cooldown, keep the classification-change detection in Lua,
and record the persistence limit separately. Optionally drive a force created via
`house:SpawnUnit` or a near-inactive AI house in a test map to minimise competing
orders.

---

## Missing capability

**There is no missing C++ binding that blocks the experiment.**

Every primitive needed by the slice already exists and is live-verified:
enumerate (units/buildings/radius), owner/type/kind/position/health, house query +
player-vs-AI + alliance, and control (`MoveTo`/`Attack`/`Stop`/`GetTarget`/`Hunt`/
`SpawnUnit`), all driven by the reliable `Update(frame)` once-per-logical-frame
loop, with persistent module state (VM lives for the process).

The only *capability-level* gap is a **runtime behaviour** one, not an API gap:
> There is no primitive to **authoritatively hold an engine-AI house to Lua's
> orders** (an "order lease"), so the vanilla AI may overwrite a Lua command on a
> later tick.

That is the smallest genuine limiting capability. It is already the documented
"second half" boundary in `PROJECT/RUNTIME_BOUNDARY.md`. It does **not** prevent
demonstrating Observe → Classify → Choose → Command → Reobserve → Adapt; it only
darkens how *long* each command sticks. It should be treated as a candidate
for a future single native primitive (e.g. a leased target/order that resists the
house's own reselection), not as a prerequisite for Gate 1.

---

## Ares / Phobos boundary

Classifying the exact Adaptive AI behaviour: **observe army composition at
runtime → classify → choose a categorical response → re-observe → change the
response.**

- **NATURAL** — the *static* parts are natural: fixed per-type priority /
  weighting (Ares/Phobos `[AI]`/`[General]` blocks), fixed script sequences
  (`ScriptTypes`), and configuration of *what* to prioritise.
- **NO NATURAL MODEL FOUND** — the *adaptive decision loop itself* is **not
  naturally modelled**. It requires a live interpreter that reads an arbitrary,
  aggregated runtime signal (the player's current army composition) at runtime,
  computes a category, and changes a categorical response when the signal crosses
  a threshold. Ares/Phobos are data/script-driven (they *configure* priorities and
  *sequence* fixed scripts); they do not expose a per-frame predicate over
  arbitrary game state that can re-decide an open-ended response. This matches the
  verdict already recorded in `PROJECT/RUNTIME_BOUNDARY.md` (the runtime
  reselection boundary is "**not naturally modeled by INI/Ares/Phobos**").
- **POSSIBLE WITH WORK** — one could *approximate* a bounded reaction by pairing
  Ares/Phobos script triggers with configured counters (e.g. a script that leans
  anti-air once AA units are built), but that is a fixed, authored reactive script,
  not a general data-driven adaptive classifier over arbitrary composition. It is
  a workaround, not a natural model.

**Reasoning constraint honoured:** this is a *research classification*, not a
marketing claim. Static configuration and scripted sequencing → NATURAL. An
arbitrary runtime adaptive interpreter → NO NATURAL MODEL FOUND (grounded in the
existing `RUNTIME_BOUNDARY.md` result). We do not claim Ares/Phobos "cannot" do
anything — only that the specific live adaptive loop has no natural abstraction
there, whereas LuaAPI's `Update(frame)` + `World`/`House`/unit bindings model it
directly.

---

## Gate 1 conclusion

> "Can we start implementing the first Adaptive AI vertical slice using the
> current LuaAPI without adding new C++ bindings?"

**YES.**

The current API is sufficient for Gate 1. The loop — observe → classify → choose →
control → observe again → adapt — runs entirely in Lua, driven by the once-per-
logical-frame `Update(frame)` callback, using bindings that are already present in
source and already exercised by live-verified showcases (`smart_ai`,
`tactical_patrol`, `dynamic_objective_defense`). No new C++ binding, no native
hook, no ML, no full AI replacement is required.

The single genuine limiting capability — *authoritatively holding an AI house to
Lua's orders against that house's own vanilla AI* — is a **runtime boundary**, not
a missing API. It is documented in `PROJECT/RUNTIME_BOUNDARY.md` and does not block
this slice; it only bounds how long a single issued order persists.

---

## Recommended Gate 2

**Build the thin `adaptive_counter` mod as a pure-Lua vertical slice, and wire it
for runtime verification.**

Step next (smallest, no design of a full AI):

1. Create `scripts/mods/adaptive_counter/main.lua` (module-local tables:
   `CATEGORY`, `RESPONSE`, thresholds) plus add `adaptive_counter` to
   `scripts/active_mods.txt`.
2. `Update(frame)` throttled to every 30 frames; lazy-init once on the first
   `Update` (find the non-human house via `House.GetByIndex` + `:IsHuman()`).
3. `OBSERVE` the player army via `Query.units_by_house(House.GetPlayer())`
   (fallback to `World.GetUnits()` + `GetOwner()` filter if the framework is
   avoided for in-game certainty), building a kind/type count table.
4. `CLASSIFY` the composition into one label (e.g. `ARMOR_HEAVY`, `AIR_HEAVY`,
   `MIXED`); `CHOOSE` a response from the `RESPONSE` table.
5. `ADAPT`: when the label flips from the remembered value, `Engine.PrintMessage`
   the change and re-issue commands to the AI house's units (`unit:Attack`/
   `unit:MoveTo`/`unit:Stop` on a short cooldown, `IsAlive`-guarded + `pcall`).
6. **Verify in-game** (`injector.exe` → YR 1.001 → `LuaAPI.log`): confirm the log
   shows the label flip (e.g. `ARMOR_HEAVY -> ANTI_ARMOR`, later
   `AIR_HEAVY -> ANTI_AIR`) and that the re-issued order took effect
   (`unit:GetTarget()` read-back changed). Confirm the order-persistence limit
   against the vanilla AI and record the finding (this is the known boundary).

Only if the live test shows the AI house cannot be held to a chosen response at
all should **Gate 3** consider the one smallest native primitive — a leased
target/order — and that only after comparing with Ares/Phobos.

---

## Files inspected

- Bindings: `src/bindings_techno.cpp`, `src/bindings_house.cpp`, `src/bindings_production.cpp`
- Engine / lifecycle: `src/lua_engine.cpp`, `src/dllmain.cpp`, `src/event_hook.cpp`
- Loader / dispatch: `scripts/init.lua`
- Framework: `scripts/framework/{init,event_bus,timer,query,task,unit_controller,combat_state,tactical,util,force_group}.lua`
- Baseline mod: `scripts/mods/smart_ai/main.lua`
- Docs: `API.md`, `PROJECT/ROADMAP.md`, `PROJECT/RUNTIME_BOUNDARY.md`,
  `docs/FRAMEWORK.md`, `PROJECT/CAPABILITIES.md`, `README.md`

## Files changed

- `PROJECT/SHOWCASES/adaptive_ai_gate1.md` (this report) — the only file written.
