# Miner Safety — Harvester Mining-Assignment Investigation

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001 · LuaAPI dev line `1.1.0`
> **Scope:** Determine whether `miner_safety` causes a player-ordered HARV to
> abandon its assigned mining area and search for another, even with no nearby
> enemy **now**. This is an investigation only — no fix was implemented.
> **Method:** source is authority. Live A/B could not be run from this environment
> (requires an interactive game session); findings are separated VERIFIED /
> INFERRED / UNKNOWN accordingly.

---

## Observed problem

Player issues a manual harvest order on a HARV ("HARV, work here"). Expected:
mine the selected area → return to refinery → return to that same area. Actual:
the HARV **sometimes** leaves the assigned area and goes to **another** mining area
even when there is **no enemy nearby at the moment**. Miner Safety is enabled.

The "no enemy *now*" is the key clue: the deviation is not caused by a live enemy
at the moment of observation — by then the threat has gone.

---

## Relevant Miner Safety code

`scripts/mods/miner_safety/main.lua`:

- `scanThreats(player, frame)` — every `THREAT_SCAN_EVERY` = 30 frames, iterates
  the **player's** miners (`ownedBy`) and searches for an enemy within
  `THREAT_RADIUS` = 10 cells (`findEnemyNear`, lines 417–459).
- If a threat is found and the miner's current mission is harvest/move/guard/attack
  (`shouldStopMiner`, line 307), it calls `stopMiner(miner, frame, threat)`.
- `stopMiner(miner, frame, threat)` (lines 316–346):
  ```lua
  local result = safeCall(miner.Stop, miner)   -- <== line 331
  ...
  markThreatened(miner, threat, frame)          -- state.threatened = true
                                                -- threatUntil = frame + SAFE_RELEASE_DELAY(90)
  ```
- `tryResume(miner, frame)` (lines 352–411), called when `findEnemyNear` returns
  nil for a threatened miner:
  ```lua
  if frame < state.threatUntil then return end   -- 90-frame safety buffer
  local threat = findEnemyNear(player, x, y, THREAT_RADIUS)   -- recheck
  if threat then re-arm delay; return end
  -- Current Unit Control API has no explicit ResumeHarvest().
  -- Hunt() is therefore used as autonomous recovery.        <== lines 397-399
  local result = safeCall(miner.Hunt, miner)     -- <== line 401
  if result ~= false then clearThreat(miner); msg("...autonomous behavior resumed") end
  ```

So the miner's lifecycle under Miner Safety is: **Stop (on threat) → wait 90 frames
→ Hunt (autonomous)**.

The mod's own documentation admits the consequence — `HOW_TO_USE.txt:26`:
> "Харвестер «теряет» заказ на добычу — после возобновления идёт по Hunt, а не на
> прежнюю точку сбора (нет ResumeHarvest в API)."
>
> ("The harvester *loses* its mining order — after resuming it goes by Hunt, not to
> the previous collection point (no ResumeHarvest in the API).")

---

## Whether `Hunt()` is responsible

**YES — `Hunt()` is the direct trigger of the wandering.** (VERIFIED)

- The `Hunt` binding is `Techno_Hunt` → `pFoot->QueueMission(Mission::Hunt, true)`
  (`src/bindings_techno.cpp:424–433`). It issues a **new autonomous mission**.
  It is not "resume my previous harvest"; it makes the unit act on its own.
- For an unarmed harvester, an autonomous `Hunt` is the engine's auto-mining
  path — the harvester goes find ore (typically the nearest valid patch) and mine
  it, which is **not necessarily the player's chosen field**.
- grep across `scripts/` confirms `miner_safety` is the **only** Lua module that
  calls `miner.Hunt` / `miner.Stop` on the player's units. No other code can
  introduce this.

**Why "no enemy now":** the mechanism is triggered by a *transient* threat. A
scout/fast unit crossing within 10 cells for one 30-frame scan is enough to
`Stop` the miner and set `threatened`. By the time the threat leaves and the
90-frame buffer elapses, `tryResume` re-checks the area (now empty) and issues
`Hunt()`. So the observed state is "no enemy, but the miner was already `Hunt()`-ed
and is now autonomously re-mining elsewhere." (INFERRED — this timing thread is
fully supported by the code, but the exact engine re-mining choice is engine-side.)

---

## Whether Miner Safety is confirmed as the cause

**YES — Miner Safety is confirmed as the mechanism that drops the assignment, and
it is the only Lua code that does so.** (VERIFIED from source + the mod's own
documentation.)

Confirmed, non-inferred facts:
- Only `miner_safety` calls `miner.Stop` / `miner.Hunt` on the player's harvesters
  (method-style calls at `main.lua:331`/`:401`, the only matches anywhere in
  `scripts/`).
- `tryResume()` replaces the (unrestorable) harvest with autonomous `Hunt()`
  (`main.lua:397–401`), and the mod's `HOW_TO_USE.txt` explicitly states the
  harvester loses its mining order and goes by `Hunt` after resuming.
- The LuaAPI exposes **no** harvest/restore primitive, so `tryResume` *cannot* do
  anything but `Hunt` — there is no innocent alternative in the current API.

Caveat: this confirms the **code path** is responsible. A live reproduction of a
specific annoyance (the exact "fresh, no-threat-at-all since spawn" case) was **not
performed** (see A/B test).

---

## What happens to the player's mining assignment after `Stop()`

`Techno_Stop` (`src/bindings_techno.cpp:495–514`) does:
```cpp
pFoot->SetTarget(nullptr);
pFoot->Destination = nullptr;
pFoot->QueueMission(Mission::Stop, true);
```

- **VERIFIED:** `Stop()` clears the unit's `Target`, clears `Destination`, and sets
  the mission to `Mission::Stop`. On a harvesting harvester this ends the active
  harvest and clears the cell it was heading to / mining.
- **UNKNOWN from LuaAPI source:** whether the engine retains a *separate* internal
  "previous ore field / return-to-field" memory that survives `Stop()` is not
  observable — the engine internals are not exposed. There is no binding to read
  or restore such state.
- Regardless, because Miner Safety then issues `Hunt()` on resume (a different
  mission), the previously-assigned field is not acted on again. The assignment is
  effectively lost for the purpose of this harvester until it is manually re-ordered.

---

## Whether current LuaAPI can restore that assignment

**NO.** (VERIFIED)

The available unit-control primitives are: `MoveTo`, `Attack`, `Stop`, `Hunt`,
`Scatter`, `GetMission`, `IsIdle`, `IsAttacking`, `GetTarget`, `GetDistanceTo`,
`GetOwner`, `GetTypeName`, `GetKind` (see `bindings_techno.cpp` method table,
lines 900–937; plus `AI.QueueUnit`/`AI.CountUnit` and `house:SpawnUnit`).

- There is **no** binding to issue `Mission::Harvest`.
- There is **no** binding to re-apply a harvester's previous mining target/field.
- There is no ore-field (`TIBTRE` / growable) query and no read of a harvester's
  assigned ore cell (the mod's header, lines 13–20, already notes this).
- `MoveTo(x,y)` queues `Mission::Move` (line 416) — it would drive the harvester
  there but would **not** make it mine; it is not a harvest order.

So after `Stop()`/`Hunt()`, LuaAPI cannot return the HARV to its player-assigned
field. This is not a Miner Safety bug per se; Miner Safety is forced to degrade to
`Hunt()` because the API has nothing better.

---

## Exact missing API capability

One primitive, in two possible forms:

1. **Re-issue a harvest order** (the minimal one that `tryResume` needs):
   a binding that sets a `FootClass` to `Mission::Harvest` at a given cell / ore,
   e.g. `unit:HarvestAt(x, y)` or `unit:ResumeHarvest()`. This is what
   `tryResume()` would call instead of `miner.Hunt()`.
2. **Read the currently-assigned harvest cell** (helper, so the previous field can
   be re-applied after a `Stop`), e.g. `unit:GetHarvestDestination()` / `unit:GetMissionTarget()`.

The smallest correct missing capability is **(1)**: a harvest-order binding.
(2) is needed only because `Stop()` clears `Destination`, so without a reader the
mod cannot know *where* "previously assigned" was unless it captured it beforehand.

---

## Minimal fix options (recorded only — NOT implemented)

- **Fix A — native (smallest correct):** add a harvest-order binding
  (`unit:HarvestAt(x,y)` / `unit:ResumeHarvest()`), then change Miner Safety
  `tryResume()` to call it instead of `miner.Hunt()`. Capturing the pre-`Stop`
  harvest cell (from `unit:GetDestination()`-style reader, or by recording the
  harvester's cell each scan when it is not threatened) lets the order be re-issued
  to the exact field. This preserves the player's assignment.
- **Fix B — Lua-only (partial, no native):** reduce how often an assignment is
  interrupted, e.g. require a threat to persist for 2+ consecutive scans / only
  stop a harvester that is actually mid-transit to ore, or add a user toggle to
  ignore transient-scan threats. This *reduces* the false-wander frequency but does
  **not** restore the assignment — the harvester still degrades to `Hunt()` whenever
  it is actually stopped. It mitigates the symptom, not the root cause.
- **Fix C — not viable:** re-issuing `MoveTo` to the old cell does not work — it
  queues `Mission::Move`, not `Mission::Harvest`, so the harvester would drive over
  but not mine. Confirmed not a real fix.

The smallest *correct* fix is **Fix A** (a harvest-order binding + a small
`tryResume` change). If no native binding is wanted, **Fix B** is the only Lua-only
option and only dampens the issue.

---

## A/B test (Miner Safety enabled vs disabled)

**UNKNOWN — not performed.** The test requires an interactive game session: launch
a skirmish, manually issue the same "HARV, work here" order on the same
player-controlled HARV with Miner Safety on, then repeat with it off, comparing
whether the HARV leaves the field with no threat. That cannot be driven from this
environment (the Lua runtime only initialises once an active match exists, and
issuing the manual harvest order is a human input). No live result is claimed.

What **can** be predicted from source (INFERRED):
- **Miner Safety OFF:** the engine alone will keep the player harvest assignment
  (unless the vanilla AI does something) — Miner Safety is the only Lua code that
  intervenes.
- **Miner Safety ON:** a transient enemy near the miner → `Stop` → (90 frames later)
  `Hunt()` → the harvester autonomously re-mines elsewhere. This is the wander.

So the A/B prediction is that Miner Safety ON reproduces the wander and OFF does
not. It is a strong prediction grounded in the confirmed source path, but it was
not observed live.

---

## Final recommendation

> **Is Miner Safety causing the HARV to abandon the player's assigned mining area,
> and if so, what is the smallest correct fix?**

**Yes — Miner Safety is the cause.** It is the only Lua code that calls
`Stop()` then `Hunt()` on the player's harvesters, and on resume it issues an
autonomous `Hunt()` (`main.lua:401`) because the current LuaAPI has **no
harvest/resume primitive** (`main.lua:397–399`, `HOW_TO_USE.txt:26`). A transient
threat triggers `Stop` → later `Hunt`, so by the time there is "no enemy now" the
harvester has already been sent off autonomously.

**Smallest correct fix:** add a native harvest-order binding (e.g.
`unit:HarvestAt(x,y)` or `unit:ResumeHarvest()`) that sets `Mission::Harvest`, and
have Miner Safety's `tryResume()` call it (against the previously-recorded harvest
cell) **instead of** `miner.Hunt()`. Optionally add a reader for the current harvest
destination so the assigned field can be re-applied after `Stop()`. Without such a
binding, no Lua-side change can restore the assignment — the best Lua-only
alternative (Fix B) only reduces the frequency of interruption.

**Next decision (not done here):** the A/B live test must be run by a human
playtester, and the harvest-order binding must be scoped/compared against Ares/Phobos
before any C++ work (per `PROJECT/RUNTIME_BOUNDARY.md`). No fix was made during this
investigation.
