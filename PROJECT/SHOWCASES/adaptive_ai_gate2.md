# Gate 2: Minimal Adaptive Counter

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001 · LuaAPI dev line `1.1.0`
> **Prompt:** implement the smallest real in-game proof of
> **Observe → Classify → Choose → Control → Reobserve → Adapt**, pure Lua, no new
> C++ bindings.
> **Method:** implementation first; verification separated. Static/logic-harness
> evidence below is labelled clearly; the live in-game run is a separate step.

---

## Status

**INCONCLUSIVE** — **live runtime verification could NOT be completed** (the full
Observe → Classify → Choose → Control → Reobserve → Adapt chain has not yet been
observed in-game).

The implementation and off-game classification logic are confirmed below. The
live-in-game proof requires an **interactive human playtester**, which is not
possible to perform from this automated environment. No result here is inferred
from source code; nothing below claims an in-game observation that did not happen.

---

## What was implemented

A single, isolated, pure-Lua showcase mod: `scripts/mods/adaptive_counter/main.lua`.
It stands in for ONE computer house. No other file was touched except
`scripts/active_mods.txt` (adding `adaptive_counter` so the loader picks it up).

The per-frame loop:

```text
Update(frame)                         (dispatched once per logical frame)
   └─ lazy init (find AI house) once
   └─ cooldown gate: pulse only every DECIDE_EVERY (30) frames
        └─ OBSERVE  player+allies army  -> Query.units_by_house(player,{allied,includeBuildings})
        └─ CLASSIFY army -> label        -> classify(playerArmy)  (data-driven)
        └─ COMPARE   label vs currentState.playerStrategy
             └─ if changed -> CHOOSE new response (RESPONSE[label])
                          -> log PLAYER STRATEGY CHANGED / AI RESPONSE CHANGED
        └─ CONTROL   command AI-house units with the persisted response
                        (unit:Attack on the nearest suitable player target, cooldown-gated)
        └─ SAVE      currentState.{playerStrategy, previousStrategy, response, ...}
```

The response only ever changes inside the `if strategy ~= currentState.playerStrategy`
branch, which can only be entered when `classify()` returns a different label,
which only happens when the **observed composition actually changed**. There is a
`NO_CHANGE_LOG_EVERY` cadence for logging "no change" pulses, but it is only a
log-throttle — it never alters the decision.

### Exact LuaAPI primitives used

| Primitive | Role | Verified source |
|---|---|---|
| `House.GetPlayer()` | identify the human house | `src/bindings_house.cpp:79` |
| `House.GetCount()` / `House.GetByIndex(i)` | enumerate all houses | `src/bindings_house.cpp:88`,`:94` |
| `house:IsHuman()` | distinguish player vs AI house | `src/bindings_house.cpp:165` |
| `house:IsAlliedWith(h)` | skip allied houses as opponent | `src/bindings_house.cpp:287` |
| `house:GetName()` | house label (log / debug) | `src/bindings_house.cpp:158` |
| `unit:GetTypeName()` | INI type → CATEGORY role | `src/bindings_techno.cpp:91` |
| `unit:GetKind()` | mobile/building + aircraft detection | `src/bindings_techno.cpp:215` |
| `unit:IsAlive()` | lifetime guard on every object | `src/bindings_techno.cpp:197` |
| `unit:GetOwner()` | (via framework `units_by_house`) | `src/bindings_techno.cpp:171` |
| `unit:GetId()` | primitive key for the command cooldown | `src/bindings_techno.cpp:206` |
| `unit:GetDistanceTo(c)` | nearest-target selection (map cells) | `src/bindings_techno.cpp:231` |
| `unit:Attack(target)` | issue the response (native SetTarget + Attack mission, NOT the disabled ActiveClickWith hook) | `src/bindings_techno.cpp:467` |
| `Engine.PrintMessage(text)` | `[ADAPTIVE]` log/HUD lines | `src/lua_engine.cpp:280` |
| `require("framework.util")` | `is_alive` / `kind_of` / `is_mobile` predicates | `scripts/framework/util.lua` |
| `require("framework.query")` | `units_by_house(house, opts)` | `scripts/framework/query.lua:184` |
| `Update(frame)` mod callback | the reliable once-per-logical-frame dispatch | `src/lua_engine.cpp:1158`, `scripts/init.lua:116` |

No new C++ binding was added; no core LuaAPI file was modified.

---

## How player classification works

A data-driven role system, not scattered branches:

- **`CATEGORY[typeName]`** maps an INI type to a coarse role. Types were taken
  from the repo's already-verified tables (`smart_ai` `HIGH_THREAT`, `tactical.lua`
  `THREAT_VALUES`) so nothing is invented:
  - `ARMOR` (heavy/ground tanks): `HTNK TTNK APOC SREF MTNK LTNK`
  - `AA` (ground anti-air): `FLAKT` (mobile), `FLAK` (building)
- **Aircraft** are detected via `unit:GetKind() == "aircraft"`, independent of the
  table.
- **`ROLE_WEIGHT = { ARMOR=1, AIR=1, AA=2 }`** — `AA` units are rarer and are
  weighted 2× so that *adding substantial AA* cleanly flips the classification
  away from `ARMOR_HEAVY` (the exact Gate 2 test scenario). This is the only
  tuning; it is data, not logic.
- **`classify()`** sums weighted roles, then picks a label by **strict plurality**
  (the single largest role wins; a tie → `MIXED`), guarded by a minimum weight.

```text
score {ARMOR, AIR, AA}
  total < 3            -> MIXED
  strict plurality wins -> ARMOR_HEAVY | AIR_HEAVY | AA_HEAVY
  tie                  -> MIXED
```

### Classification logic-harness result (off-game, deterministic)

Replicated the exact algorithm + weights in a standalone harness:

| Player composition (roles) | Result | Response |
|---|---|---|
| 5 tanks | `ARMOR_HEAVY` | `AIR` |
| 5 tanks + 3 AA | `AA_HEAVY` | `GROUND` (flip) |
| 4 aircraft | `AIR_HEAVY` | `AA` |
| 2 tanks + 2 aircraft (tie) | `MIXED` | `DEFAULT` |
| 3 tanks + 1 AA | `ARMOR_HEAVY` | `AIR` |
| 1 tank + 2 AA | `AA_HEAVY` | `GROUND` |
| 4 infantry (unclassified) | `MIXED` | `DEFAULT` |

This confirms the **adapter flips** on a composition change (5 tanks → +3 AA →
`ARMOR_HEAVY`→`AA_HEAVY`, `AIR`→`GROUND`) and that unclassified/ties degrade to
`MIXED` rather than misclassifying.

---

## How state is stored

Module-level Lua upvalues (the VM lives for the process — see Gate 1), storing
only primitives:

```lua
local currentState = {
    playerStrategy    = "MIXED",   -- most recent observed label
    previousStrategy  = "MIXED",   -- label before the last change
    response          = "DEFAULT", -- active response label
    lastDecisionFrame = 0,         -- last logic frame a full pulse ran
    hasClassified     = false,     -- first classification logged yet?
}
local orderCooldown = {}          -- unitId (number) -> next allowed frame
```

No `Techno` userdata is retained across frames: the observed player army is a LOCAL
of a single pulse (re-scanned each pulse) and is discarded immediately; the AI
house handle (`aiHouse`) is a `House` userdata, which is safe to hold across
frames (Houses live for the whole match and the C++ side caches them by registry
reference, unlike a destroyable unit). Everything else is a string/number/boolean.

---

## How adaptation is triggered

Only by observation. Each pulse:

1. re-observes the player's army,
2. recomputes `classify()` → `strategy`,
3. compares `strategy` to `currentState.playerStrategy`,
4. on a difference, records the new label + `RESPONSE[label]` and logs
   `PLAYER STRATEGY CHANGED` / `AI RESPONSE CHANGED`,
5. otherwise keeps the persisted response (just keeps executing it).

There is no `os.time`, no `frame % K == N` fake switch, no "cool-down then switch
anyway" — the value that drives the response is purely
`classify(observedArmy)`. A composition change is the **only** thing that can
change the response.

---

## What response actions are performed

The response is deliberately trivial (no focus-fire, no production, no retreat):

| Label | Response | Controlled unit | Target |
|---|---|---|---|
| `ARMOR_HEAVY` | `AIR` | AI house **aircraft** (`GetKind()=="aircraft"`) | player **armor/ground** |
| `AIR_HEAVY` | `AA` | AI house **AA** units (`CATEGORY=="AA"`) | player **air** |
| `AA_HEAVY` | `GROUND` | AI house **ground** units | player **ground** |
| `MIXED` | `DEFAULT` | any AI mobile unit | any player mobile target |

`responseUnit(u, response)` picks which AI units to command (so the semantics are
honest — `AIR` really uses aircraft); `responseTargets(pool, response)` picks what
player units to engage; `nearestTarget()` chooses the closest via
`unit:GetDistanceTo`. Each command is `unit:Attack(target)` gated by a per-unit
cooldown and wrapped in `pcall` + `IsAlive()` guards.

---

## Runtime verification

### Verified in source

These are confirmed by reading the implementation (and the off-game harness), not
by an in-game run:

- `adaptive_counter` is registered in `scripts/active_mods.txt` and is a valid mod
  table (`return AdaptiveCounter` with `Update(frame)`), so the loader accepts it.
- Classification is data-driven and its numeric results were reproduced in a
  standalone harness: `5 tanks → ARMOR_HEAVY`, `5 tanks + 3 AA → AA_HEAVY`,
  `4 aircraft → AIR_HEAVY`, ties/unclassified → `MIXED`.
- Adaptation is gated purely on `classify(playerArmy) != currentState.playerStrategy`:
  there is **no** `os.time`, no `frame % K == N` fake switch, so a response only
  changes when the observed composition changes.
- State is primitive-only (`currentState` labels/frames/booleans + `orderCooldown`
  of unit-ids). No `Techno` userdata is stored across frames; the observed player
  list is a per-pulse local.
- Every engine call is guarded (`IsAlive`) and the whole pulse is `pcall`-wrapped,
  so a bad unit should not crash the game.

### Verified in game

**None — the live test was not run.** This is the key, honest result.

To explain why it could not be run from here:
- **No game is currently running** (`gamemd.exe` / `gamemd-spawn.exe` / `RA2MD.exe`
  absent from the process list).
- **The Lua engine only initialises once a match is active.** In
  `src/lua_engine.cpp` `OnGameFrame()` returns early when `!IsInGameMatch()`
  (lines 936–941, requiring `ScenarioClass::Instance`, `HouseClass::CurrentPlayer`,
  and `Unsorted::CurrentFrame > 0`); the `std::call_once` that runs `RunInitScript`
  (which loads `init.lua` and `require`s every mod) sits **after** that early
  return (lines 1125–1134). So **no mod even loads** until a human reaches an
  in-progress skirmish.
- **Starting a skirmish and building/changing the player's army require human
  input** (main menu → skirmish → build tanks → add AA). There is no headless way
  to reach a match or alter the player's composition.
- I therefore introduced **no test-only fix**, changed **nothing** in
  `adaptive_counter`, and did **not** hand-trigger the scenario, because doing so
  would produce log lines that are not genuine in-game observations.

**Corroborating evidence that the pipeline itself works when a human plays:** the
existing `LuaAPI.log` (15:05:32 → 15:16:11) is a **real prior live session** — it
shows loading, a continuing `Update` loop, and active gameplay
(`[Nav] MTNK moving to (109,62)`, `[CSTATE] status @frame 35370: tracked=6`,
per-mod timing, etc.). That run had `dynamic_objective_defense`, `smart_ai`,
`bounty_hunter`, `miner_safety`, `jet_veterancy`, `combat_state_tracker`,
`tactical_reassess`, `multi_force`, and `target_reselect` active. It demonstrates
the injection/launch workflow and re-classify-on-change machinery all work when a
human drives the game — but it **does not** exercise `adaptive_counter` (no
`[ADAPTIVE]` lines exist).

### Failed

- **Nothing failed** — no test was run, so there is no observed failure and no
  observed crash. I deliberately did not fabricate a run.

### Unknown

- The actual in-game behaviour of all six cases (the `[ADAPTIVE]` chain), because
  the live test was not performed.
- Whether the vanilla AI immediately overwrites the AI-house orders issued by Lua
  (the documented "order lease" boundary). This remains unobserved. The
  implementation **is** expected to hit this (per `PROJECT/RUNTIME_BOUNDARY.md` and
  `miner_safety` / `ROADMAP.md` Gate 12.3), but no in-game evidence was captured.
- Whether `adaptive_counter` even loads without error in the current build (only
  confirmable in a live match).

### What is needed to close this

A human playtester should:

1. Run `injector.exe` → **Launch Game** (or start the game and inject).
2. Start a **Skirmish** where there is a human player + at least one computer
   house, with `adaptive_counter` in `scripts/active_mods.txt` (already added).
3. Build mostly tanks as the player; wait ~1 s; confirm in `LuaAPI.log`:
   ```
   [ADAPTIVE] controlling AI house '<name>' (#<index>)
   [ADAPTIVE] PLAYER STRATEGY: ARMOR_HEAVY
   [ADAPTIVE] AI RESPONSE: AIR
   [ADAPTIVE] issued N orders (response=AIR)
   ```
4. Add substantial AA units to the player's army; wait ~1 s; confirm:
   ```
   [ADAPTIVE] PLAYER STRATEGY CHANGED: AA_HEAVY
   [ADAPTIVE] AI RESPONSE CHANGED: GROUND
   [ADAPTIVE] issued N orders (response=GROUND)
   ```
5. Keep the game running and watch for continued `[ADAPTIVE] pulse: ... (no change)`
   lines (state surviving multiple `Update` calls) and for any
   `[ADAPTIVE] pulse error:` (which would indicate an invalid-object/crash path).
6. Record whether the AI-house units actually follow the Lua `unit:Attack` orders
   or are immediately re-issued by the vanilla AI (the persistence question).

Expected **successful continued Update execution** is a succession of
`[ADAPTIVE] pulse: strategy=X response=Y (no change)` lines every ~3 s
(frames advance) — the framework's `Framework.update` path proves `Update` keeps
running.

### Verdict

**INCONCLUSIVE (test could not be completed).** The reason is that the required
behaviour is inherently **interactive** — it needs a human to reach a match and
change the player's composition. It is **not** a runtime limitation (no concrete
blocker was observed) and therefore should **not** be marked BLOCKED; nothing
*failed*, so it should not be marked PASS either. The next decision must be made
from genuine in-game evidence, which has not yet been collected.

---

## Files

**Implementation**
- `scripts/mods/adaptive_counter/main.lua` (new — the showcase mod)
- `scripts/active_mods.txt` (added `adaptive_counter` so the loader enables it)

**Report**
- `PROJECT/SHOWCASES/adaptive_ai_gate2.md` (this report)

No core LuaAPI file was changed; no new C++ binding; Smart AI untouched.

---

## Gate 2 conclusion

- **Gate 2 status: INCONCLUSIVE** — implementation complete and the classification
  adapter verified off-game (deterministic harness). The final in-game
  Observe→Classify→Choose→Control→Reobserve→Adapt chain is implemented but has
  **not** been observed in a live Yuri's Revenge session (the test requires an
  interactive human playtester; it could not be executed from this environment).
- **API primitives used:** `World`/`House`/`Techno` bindings above + framework
  `Query.units_by_house` + `Update(frame)`. No new C++ binding.
- **Exact in-game behaviour verified:** none (no `[ADAPTIVE]` line exists in
  `LuaAPI.log`; the mod has not yet been run in a live match).
- **Limitations (source-level, documented, not hidden):**
  - Vanilla AI may overwrite an AI-house order Lua issues (`unit:Attack` is a
    *command*, not an *override*; no "order lease" primitive). This is documented
    in `PROJECT/RUNTIME_BOUNDARY.md` and hit by `miner_safety` / `ROADMAP.md` Gate
    12.3. It was **not** observed live here.
  - Observing-only mode when no suitable (non-human, non-neutral, non-allied)
    AI house exists.
  - Classification is intentionally coarse and recognises only the four labels.
- **Recommended Gate 3:** a human playtester runs the six-case live test (steps in
  §What is needed to close this) and records the actual `[ADAPTIVE]` log lines.
  Only then decide PASS/BLOCKED on the real chain. If (and only if) the evidence
  shows the AI house cannot be held to a chosen response at all, evaluate the
  **single smallest** native primitive — a leased target/order that resists the
  house's own reselection — after comparing with Ares/Phobos (per
  `RUNTIME_BOUNDARY.md`). Do **not** broaden the AI or change `adaptive_counter`
  until the chain is proven live.
