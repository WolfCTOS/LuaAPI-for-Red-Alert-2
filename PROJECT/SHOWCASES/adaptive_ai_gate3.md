# Gate 3: Live Adaptive AI Verification

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001 · LuaAPI dev line `1.1.0`
> **Purpose:** prove the Observe → Classify → Choose → Control → Reobserve → Adapt
> loop in a **live** Yuri's Revenge match, using the existing `adaptive_counter`
> mod, without expanding or redesigning the AI.
> **Method:** no live result is manufactured or inferred. Source/harness evidence is
> labelled separately. Live observations were **not** obtainable from this
> environment, so the live column is empty and the verdict is INCONCLUSIVE.

---

## Module under test

`scripts/mods/adaptive_counter/main.lua` (registered in `scripts/active_mods.txt`).
It stands in for one computer house, and every `DECIDE_EVERY` (30) logical frames:
observe the player's (and allies') army → classify via `CATEGORY`/`ROLE_WEIGHT`
into `ARMOR_HEAVY`/`AIR_HEAVY`/`AA_HEAVY`/`MIXED` → if the label changed, swap the
`RESPONSE` (`AIR`/`AA`/`GROUND`/`DEFAULT`) and re-command its AI-house units. This
is described in full in `PROJECT/SHOWCASES/adaptive_ai_gate2.md`.

---

## Verification status

### VERIFIED IN SOURCE

Reading `adaptive_counter/main.lua` + bindings:

- The dispatch path is real: `AdaptiveCounter.Update(frame)` runs once per logical
  frame (loader `OnTick` → `init.lua`), gated by a 30-frame cooldown
  (`currentState.lastDecisionFrame`).
- Observation uses framework `Query.units_by_house(player, {allied=true,
  includeBuildings=true})` (→ `World.GetAllUnits()` + owner/alliance filtering).
- Classification is data-driven (`CATEGORY` map + `ROLE_WEIGHT` + strict-plurality
  tie→`MIXED`), decided purely from the observed army.
- Adaptation is gated **only** on `classify(playerArmy) != currentState.playerStrategy`;
  there is **no** `os.time` / `frame % K == N` fake switch. A response change can only
  happen when the observed composition changed.
- The AI-house control path uses `unit:Attack(target)` (native
  `SetTarget`+`QueueMission(Attack)`, `bindings_techno.cpp:467`) on AI-house units,
  targeted at player units of the response's preferred role. Per-unit cooldown +
  `IsAlive` guard + `pcall` on the whole pulse.
- State persisted across frames is primitive-only (`currentState` labels/frames +
  `orderCooldown` unit-ids); the observed player list is a per-pulse local.

### VERIFIED OFF-GAME

- Classification algorithm reproduced in a standalone harness (Gate 2):
  `5 tanks → ARMOR_HEAVY`, `5 tanks + 3 AA → AA_HEAVY` (the flip), `4 aircraft →
  AIR_HEAVY`, ties/unclassified → `MIXED`. So the adapter reliably flips when AA is
  added.

### VERIFIED IN LIVE GAME

**None.** The live test was not (and could not be) performed from this environment.

Supporting facts checked directly:
- **No game is running** (`gamemd` / `gamemd-spawn` / `RA2MD` all absent).
- **The only `LuaAPI.log` is an early, short run** (recreated 09/07 18:54:45, 3
  lines: `bootstrap thread started`, `ActiveClickWith hook DISABLED`) — it never
  reached a match, so **zero** `ADAPTIVE` lines exist.
- The Lua engine only initialises **once a match is active**:
  `OnGameFrame()` returns early when `!IsInGameMatch()` (`src/lua_engine.cpp:936`),
  and the `std::call_once` that runs `init.lua` + loads mods is *after* that guard
  (`:1125`). So no mod even loads — and no `[ADAPTIVE]` line can appear — before a
  human reaches an in-progress skirmish.
- **Starting a skirmish and building/changing the player's army are interactive
  human inputs** (menu → skirmish; build tanks; add AA). There is no headless path
  to reach a match or alter the player's composition.

### FAILED

- Nothing failed — no live test ran, so there is no observed crash or observed
  regression. I deliberately did **not** hand-trigger a scenario or fabricate a log.

### UNKNOWN

- Whether the full chain (initial classification → composition change → response
  flip → re-orders) actually appears in a live match — not tested.
- Whether the **AI-house units actually follow** the Lua `unit:Attack` orders, or
  are immediately re-issued by the vanilla AI (the documented "order lease"
  boundary per `PROJECT/RUNTIME_BOUNDARY.md` / `miner_safety` Gate 12.3). Not
  observed live; this is the most important open behaviour.
- Whether `adaptive_counter` even loads without error in the current build (only
  confirmable in a live match). Off-game I could not run a real Lua interpreter, so
  this is unconfirmed; the code was reviewed for correctness but not executed.

---

## Live test procedure (for a human playtester)

Required to close Gate 3:

1. `injector.exe` → **Launch Game** (or start the game + inject). Ensure
   `adaptive_counter` is in `scripts/active_mods.txt` (already added).
2. Start a **Skirmish** with a human player + at least one **computer** opponent
   where the AI house actually has combat units to command.
3. As the player, build **mostly tanks** (e.g. several `HTNK`/`APOC`) with **no**
   meaningful AA. Wait ~1 s (≈ a few 30-frame pulses). Expect in `LuaAPI.log`:
   ```
   [ADAPTIVE] controlling AI house '<name>' (#<index>)
   [ADAPTIVE] PLAYER STRATEGY: ARMOR_HEAVY
   [ADAPTIVE] AI RESPONSE: AIR
   [ADAPTIVE] issued N orders (response=AIR)
   ```
4. Add a **clear AA-heavy** force (several `FLAKT`/`FLAK`). Wait for the next scan
   (~1 s). Expect:
   ```
   [ADAPTIVE] PLAYER STRATEGY CHANGED: AA_HEAVY
   [ADAPTIVE] AI RESPONSE CHANGED: GROUND
   [ADAPTIVE] issued N orders (response=GROUND)
   ```
5. (If practical) repeat with an air-heavy composition → expect `AIR_HEAVY` →
   response `AA`.
6. Watch for continued `[ADAPTIVE] pulse: strategy=X response=Y (no change)` lines
   (state surviving multiple `Update` calls) and note **no**
   `[ADAPTIVE] pulse error:` (invalid-object/crash path).
7. Record whether the AI-house units visibly engage the player units after the order
   (the "response executed" question), and whether the vanilla AI overrides them.

---

## Gate 3 verdict

**INCONCLUSIVE** — the test **could not be completed** from this environment. It is
not **FAIL** (no counter-evidence was observed; nothing misbehaved) and not **PASS**
(no live evidence exists). The required behaviour — an in-game, same-match reaction
to a real change in the player's army — has not been observed. The only evidence
that exists is source-level (VERIFIED IN SOURCE) and off-game (VERIFIED OFF-GAME);
both are necessary but not sufficient for a live proof.

Because the gate did **not** pass, the "smallest next step to turn the PoC into a
Showcase" is not yet warranted. The immediate next step is:

- **Step 1 (required):** a human playtester runs the procedure above and records the
  actual `[ADAPTIVE]` log lines (the decisive evidence). If the chain appears,
  Gate 3 becomes a **PASS**.
- **Step 1b (optional automation aid, reduces manual building):** add a *test-only*
  companion mod that, on a timer, spawns the player's starting tank force and then
  spawns player AA units (via `House.GetPlayer():SpawnUnit`) purely to drive the
  classification change without the player manually building — it would not change
  `adaptive_counter`'s logic and would be disabled outside testing. (Not implemented
  here because it cannot be run/verified from this environment and is extra scope.)
- **Step 2 (only after a live PASS):** treat the PoC as proven, then design a small
  Showcase around it — but do **not** expand the AI, add bindings, or touch Smart AI
  until the live chain is confirmed.

---

## Files

- Report: `PROJECT/SHOWCASES/adaptive_ai_gate3.md` (this file). No code changes were
  made in this task; `adaptive_counter`, `smart_ai`, and all other systems are
  untouched.

## Summary

- **VERIFIED IN SOURCE:** adaptive loop implementation is real, data-driven, and
  adaptation is driven by observed composition (no timed switch).
- **VERIFIED OFF-GAME:** classifier flips correctly (harness).
- **VERIFIED IN LIVE GAME:** none (unchanged from Gate 2 — requires interactive play).
- **FAILED:** nothing observed as failing.
- **UNKNOWN:** full live chain, AI-house order execution vs vanilla-AI override, and
  whether the mod loads live.
- **Verdict: INCONCLUSIVE** (test could not be completed; no live evidence exists).
