# Miner Safety — Harvester Mining Assignment FIX Investigation

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001 · LuaAPI dev line `1.1.0`
> **Scope:** the smallest engine/API change to let Miner Safety **restore** the
> player-assigned harvest location after a threat, instead of issuing `Hunt()`.
> Source-investigation only. No fix was applied; no live verification was performed.
> Evidence is from `third_party/YRpp` (Phobos-developers YRpp fork).

---

## Problem recap (from the prior investigation, confirmed)

`miner_safety/scripts/mods/miner_safety/main.lua` `tryResume()` (lines 397–410)
replaces the player's harvest with `miner.Hunt()` (`Main.lua:401`) because the
current LuaAPI exposes no harvest/restore primitive. `Stop()` (line 331) clears
`Target` + `Destination`. Result: the assigned mining area is abandoned.

Goal: `assign HARV → harvest → threat → Stop → threat gone → **restore same area**`.
No `Hunt()` as the resume path; no ore/Tiberium enumeration; no order lease; no
general order system.

---

## Verified (what the engine/source confirms)

From `third_party/YRpp`:

- **`Mission::Harvest = 10`** (`GeneralDefinitions.h:983`). Also relevant:
  `Move = 2`, `Return = 12`, `Stop = 13`, `Hunt = 15`.
- **`FootClass::Destination` is `AbstractClass*`** (`FootClass.h:179`), and there is
  a `LastDestination` (`:180`). `FootClass::MoveToTiberium(int radius)` (line 135)
  "searches cell, sets destination" — i.e. the destination is a cell.
- **`CellClass : public AbstractClass`** (`CellClass.h:27`), with
  `CellClass::Coord2Cell` / `Cell2Coord` (`CellClass.h:298,307`) and inherited
  `AbstractClass::GetCoords()` (`AbstractClass.h:107`).
- **`AbstractType::Cell`** is a valid `WhatAmI()` value (used in
  `src/event_hook.cpp` RttiName), so a `Destination` cell can be identified
  without dereferencing blindly.
- **`MapClass::Instance.TryGetCellAt(CellStruct)` / `GetCellAt`**
  (`MapClass.h:214, 224–237`) resolve a cell→`CellClass*`.
- **`MissionClass::QueueMission(Mission, bool start_mission)`** is a virtual
  (`MissionClass.h:54`) overrideable on `FootClass`/`TechnoClass`. This is the
  exact mechanism the existing **verified** `MoveTo` binding already uses.
- **`MissionClass::Mission_Harvest()`** (virtual, `MissionClass.h:71`) is the
  per-mission harvest handler the engine invokes when `CurrentMission == Harvest`.
- `MissionClass` also carries `SuspendedMission`, `Override_Mission()`,
  `Mission_Revert()` (`MissionClass.h:54–60, 112`) — the engine's *native*
  interrupt-and-return mechanism. Not used by the API surface today.
- `UnitClass::Harvesting()` (`UnitClass.h:78`) and `bool IsHarvesting` (`:116`).

**Conclusion of the source check:** the engine model fully supports the fix. The
player's harvest assignment is represented by the harvester's cell destination
(`FootClass::Destination`, a `CellClass*`), and the engine already has a
cell-addressed harvest mission (`Mission::Harvest` + `QueueMission`). There is no
need to enumerate ore/Tiberium: the assignment is a single cell.

### Cross-check against the existing verified binding

`Techno_MoveTo` (`src/bindings_techno.cpp:391–421`) already does exactly:
```
pFoot->Destination = MapClass::Instance.TryGetCellAt(CellStruct{x,y});
pFoot->QueueMission(Mission::Move, true);
```
That is the proven, in-game-verified pattern. `HarvestAt(x,y)` is the same
three lines with `Mission::Harvest` — so the write path is a known-safe mirror.

---

## API design

Two small, symmetric bindings on the `LuaAPI.Techno` userdata (both operate on the
already-validated `FootClass` path used by `MoveTo`/`Attack`/`Stop`):

### 1. `unit:GetHarvestLocation() -> {x, y} | nil`

Reads the harvester's **current assigned destination cell** so Miner Safety can
remember it *before* `Stop()`.

Maps to engine:
```
FootClass* pFoot = AsFoot(pTechno);
AbstractClass* pDest = pFoot->Destination;          // FootClass::Destination
if (!pDest || pDest->WhatAmI() != AbstractType::Cell) return nil;
CoordStruct crd = pDest->GetCoords();               // cell centre in leptons
return { x = crd.X / 256, y = crd.Y / 256 };        // map cells (as GetPosition)
```
Wrapped in `__try/__except` per repo convention.

### 2. `unit:HarvestAt(x, y) -> boolean`

Re-issues an explicit harvest at a cell (mirrors `Techno_MoveTo` verbatim, with
`Mission::Harvest`).

Maps to engine:
```
FootClass* pFoot = AsFoot(pTechno);
CellClass* pCell = MapClass::Instance.TryGetCellAt(CellStruct{(short)x,(short)y});
if (!pCell) return false;
pFoot->Destination = pCell;
pFoot->QueueMission(Mission::Harvest, true);        // Mission::Harvest = 10
return true;
```
Wrapped in `__try/__except`. Register both in `kTechnoMethods`.

Both are **read/write of one cell + one mission** — not a harvesting API, no
ore enumeration, no order lease, no refinery/Dock bookkeeping, no multi-step order
system.

### Why these two (and not `ResumeHarvest()` / `HarvestAt()` alone)

- A single `ResumeHarvest()` (no args) would require the engine/API to *remember*
  the interrupted cell — but `Stop()` clears `Destination`, and Miner Safety has no
  place to hold it. So the mod must capture it *before* the Stop.
- `HarvestAt(x,y)` alone can do the write, but there is no current way to read the
  assigned cell. So the **minimal correct pair** is: read the cell (remember) +
  re-issue harvest at that cell. The pairing is what makes the assignment
  reproducible without a hidden engine-side memory.

---

## Restoration flow (Miner Safety change, proposed)

Only Lua changes in `scripts/mods/miner_safety/main.lua`; no behaviour change to
the threat/stop logic otherwise.

1. **Extend per-miner state** (`getMinerState`, lines 116–131):
   ```lua
   harvestX = nil,  -- assigned ore cell x (captured before Stop)
   harvestY = nil,
   ```

2. **Capture the assignment** — lazily, while the miner is mid-harvest and not yet
   threatened (so the latest valid cell is kept even if `Stop()` clears it). In
   `scanThreats` (lines 417–459), when a miner is alive + player + `mission == "harvest"`:
   ```lua
   local loc = safeCall(miner.GetHarvestLocation, miner)
   if loc and loc.x and loc.y then
       -- only overwrite with a *valid* capture; never clobber with nil
       state.harvestX, state.harvestY = loc.x, loc.y
   end
   ```
   And as a belt-and-suspenders, capture **immediately before** `miner.Stop` in
   `stopMiner()` (line 331), so the last assigned cell is grabbed at the Stop
   moment:
   ```lua
   if getMission(miner) == "harvest" then
       local loc = safeCall(miner.GetHarvestLocation, miner)
       if loc and loc.x and loc.y then
           state.harvestX, state.harvestY = loc.x, loc.y
       end
   end
   local result = safeCall(miner.Stop, miner)   -- existing line 331
   ```

3. **Restore instead of Hunt** in `tryResume()` (replace lines 397–410):
   ```lua
   if state.harvestX and state.harvestY then
       local r = safeCall(miner.HarvestAt, miner, state.harvestX, state.harvestY)
       if r ~= false then
           clearThreat(miner)
           msg(string.format(
               "[miner_safety] miner #%d threat cleared, harvest restored at (%d,%d)",
               id, state.harvestX, state.harvestY))
           return
       end
   end
   -- No remembered assignment (or restore failed): lowest-risk fallback is to
   -- Hunt() as today. This is the degraded path only.
   local result = safeCall(miner.Hunt, miner)
   ...
   ```
   No `Hunt()` is used as the *nominal* resume — `Hunt()` remains only the
   no-assignment fallback, or it is dropped entirely if you prefer the miner to sit
   idle until re-ordered.

Net flow: `Stop` (with the cell captured before it) → `HarvestAt(cell)` on resume →
the harvester re-mines the same field and keeps the normal return-to-refinery cycle.

---

## Risks / uncertainty

- **Harvest cell vs exact coordinates (HIGH importance, needs in-game check).**
  The design assumes `FootClass::Destination` holds the player-assigned ore cell
  for the whole Harvest cycle. Header evidence supports it, but the exact behaviour
  at the "mining-in-place" phase (is `Destination` retained, or cleared once at the
  field?) is engine code not visible in the headers. Mitigation: continuous capture
  (+ capture-at-Stop), and fall back to `Hunt()` / idle if capture returns nil. If
  `Destination` is cleared at the field, capture-while-mining must be relied on;
  if it's also cleared then, the assignment cannot be captured and the fix needs a
  different (engine-remembered) source. **This is the main unknown.**
- **Mission queue behaviour.** `QueueMission(Harvest, true)` immediately engages
  `Mission_Harvest`. Whether a stale (already-mined) cell makes the harvester idle vs
  re-scan for nearby ore is engine behaviour. A fully-depleted cell may cause a
  brief idle/auto-recheck — acceptable, but not source-confirmed.
- **Refinery association.** Re-issuing Harvest does not change the refinery dock
  logic; the harvester returns to its refinery normally. The dock selection
  (`FootClass::FindNearestDockBuilding` etc.) is engine-driven. Low risk.
- **Pathing.** `HarvestAt` sets the destination like `MoveTo`; pathfinding is the
  engine's normal cell pathing. A blocked/stale cell would just be an
  unreachable-move case (the miner re-checks). Low risk.
- **Multiplayer synchronization.** `HarvestAt` is cell-based and deterministic
  (no wall-clock), so it should be OOS-safe like `MoveTo`. **Not verified** — the
  binding has not been exercised in CnCNet.
- **Interaction with vanilla AI.** Miner Safety targets only the *player's* house
  (`ownedBy`). A player harvester is not vanilla-AI-run. If the fix is ever applied
  to AI-house harvesters, the vanilla AI could re-order — but that is out of scope
  (and already the known `miner_safety` AI-side limitation).
- **Watch: `MegaMission`/`Override_Mission`.** The engine has its own
  suspend/revert (`SuspendedMission`, `Mission_Revert()`). This approach bypasses
  it and re-issues a fresh Harvest — simpler and self-contained, but it does not
  use the engine's native "resume exactly what was suspended" path. That path is the
  alternatiive if the cell-capture approach proves unreliable.

---

## Implementation (applied)

The fix was implemented exactly as designed, then compiled.

### 1. New bindings — `src/bindings_techno.cpp`

Added two methods to `LuaAPI.Techno` (registered in `kTechnoMethods`, right after
`MoveTo`), both operating on the validated `FootClass` path:

**`unit:GetHarvestLocation() -> {x, y} | nil`**
```cpp
FootClass* pFoot = AsFoot(pTechno);
__try {
    AbstractClass* pDest = pFoot->Destination;         // FootClass::Destination
    if (pDest && pDest->WhatAmI() == AbstractType::Cell) {  // AbstractType::Cell = 11
        coords = static_cast<CellClass*>(pDest)->GetCoords(); // AbstractClass::GetCoords()
        got = true;
    }
} __except (EXCEPTION_EXECUTE_HANDLER) { got = false; }
if (!got) return nil;
return { x = coords.X / 256, y = coords.Y / 256 };     // map cells (as GetPosition)
```

**`unit:HarvestAt(cellX, cellY) -> bool`** (mirrors the proven `MoveTo` body)
```cpp
CellStruct cell{ (short)cellX, (short)cellY };
CellClass* pCell = MapClass::Instance.TryGetCellAt(cell);
if (!pCell) return false;
__try {
    pFoot->Destination = pCell;                        // same as Techno_MoveTo
    pFoot->QueueMission(Mission::Harvest, true);       // Mission::Harvest = 10
    ok = true;
} __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
return ok;
```

Both are wrapped in `__try/__except` (SEH) as required; the `__try` blocks contain
no C++ objects with destructors (only enums/raw pointers), so no C2712.

### 2. Miner Safety — `scripts/mods/miner_safety/main.lua`

- `getMinerState()` now seeds `harvestX = nil, harvestY = nil`.
- `stopMiner()` captures the assigned cell **before** `miner.Stop` clears
  `Destination` (guarded by `getMission(miner) == "harvest"`).
- `scanThreats()` refreshes the saved cell each scan while the miner is on a
  `harvest` mission (only overwrites with a valid cell).
- `tryResume()` prefers `miner.HarvestAt(state.harvestX, state.harvestY)` and logs
  `... harvest restored at (x,y)`; `Hunt()` is used only as the fallback when there
  is no saved location (or the restore fails, as a stuck-miner safety net).

---

## Verification status

### Verified (build / compile — actually performed)

- **Build result: SUCCESS.** `cmake --build build --config Release` completed; the
  only warnings are pre-existing YRpp inline-asm `C4731` warnings; `LuaAPI.dll` and
  `injector.exe` were produced and auto-deployed to the game directory.
- **Binding result:** `bindings_techno.cpp` compiled and **linked** cleanly, so
  `GetHarvestLocation` and `HarvestAt` are present in the built DLL. This confirms
  the exact engine symbols used exist as written (no typo/interface mismatch).
- Engine symbols confirmed in `third_party/YRpp`: `Mission::Harvest = 10`,
  `FootClass::Destination` (`AbstractClass*`), `CellClass : public AbstractClass`,
  `AbstractType::Cell = 11`, `CellClass::WhatAmI()`, `AbstractClass::GetCoords()`,
  `MapClass::Instance.TryGetCellAt(CellStruct)`, `QueueMission(Mission, bool)`.

### Requires live testing (NOT performed — do not claim)

- Whether `unit:HarvestAt(x,y)` actually reproduces the player's harvest **assignment**
  (the behavioural half) — i.e. the HARV returns to the same cell after resume and
  keeps mining there rather than switching to an autonomous area.
- Whether `GetHarvestLocation()` returns the correct cell during the mining-in-place
  phase (the main unknown from the investigation).
- Runtime registration/calling of the new bindings, absence of crashes, and behaviour
  of the fallback path.
- CnCNet multiplayer determinism (no OOS check performed).

---

## Newly identified limitation

- **Restore semantics not live-confirmed.** The engine-schema is verified, but
  `QueueMission(Mission::Harvest, true)` at a saved cell being a faithful "resume my
  assignment" is engine runtime behaviour that headers cannot confirm. If the live
  test shows the HARV still re-mines elsewhere, the capture source
  (`FootClass::Destination`) is the likely culprit (cleared at the field), and the
  fallback is the engine's native `SuspendedMission`/`Mission_Revert` path or a
  larger harvest-state binding.
- **Fallback is broader than "no saved location".** `tryResume()` will also `Hunt()`
  if `HarvestAt` fails even when a location was saved — chosen deliberately so a
  restore failure cannot leave the miner permanently "threatened" and idle. This is
  a safety net, not the nominal path.

---

## Recommendation

**Implement → verify in-game.** The fix is implemented and compiles. The remaining
step is the live test:

1. Rebuild (done). Launch YR via `injector.exe`.
2. Skirmish: give a player HARV a manual mining assignment; confirm it mines there.
3. Bring a nearby enemy threat in and out.
4. Confirm `[miner_safety] ... stopped` then `... harvest restored at (x,y)` in
   `LuaAPI.log`, and the HARV returns to the **same** cell (not a new mining area).
5. Repeat with Miner Safety disabled to confirm no other cause.
6. Confirm the fallback (`Hunt`) is used only when there is no saved location.

**Do not** mark this fix live-verified until steps 2–6 are actually observed in-game.

