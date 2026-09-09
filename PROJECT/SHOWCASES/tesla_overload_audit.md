# Tesla Overload — Audit

> **Target:** `gamemd.exe` — Yuri's Revenge 1.001 · LuaAPI dev line `1.1.0`
> **Scope:** audit `scripts/mods/tesla_overload/main.lua` for correctness, API use,
> safety, determinism and maintainability. Audit only — no code was changed.
> Status markers: **VERIFIED** (confirmed from source/code) · **RISK** (a real
> concern) · **UNKNOWN** (cannot be confirmed without a live run / game data).

---

## What the mod does (as implemented)

Every `PULSE_INTERVAL` (45) frames it:
1. gets the player house + all buildings + all mobile units;
2. gathers **generators** = player/allied mobile units (`isPlayerSide`);
3. counts **enemy** buildings map-wide (`isEnemyHouse`);
4. if any enemy building remains ("victory" logic / recovery),
5. for each enemy building within `OVERLOAD_RADIUS` (8 cells) of any generator
   (or map-wide when `DEBUG_MAP_WIDE`), calls `bld:Disable(45)` then
   `bld:TakeDamage(40, WARHEAD)`.

---

## API usage — all valid bindings (VERIFIED)

Every call maps to an existing binding:

| Lua call | Binding | Source |
|---|---|---|
| `Engine.PrintMessage(text)` | `Engine_PrintMessage` | `src/lua_engine.cpp:280` |
| `house:GetName()` | `House_GetName` (`pHouse->get_ID()`) | `src/bindings_house.cpp:158` |
| `house:IsAlliedWith(h)` | `House_IsAlliedWith` (`pSelf->IsAlliedWith(pOther)`) | `src/bindings_house.cpp:287` |
| `unit:IsAlive()` | `Techno_IsAlive` | `src/bindings_techno.cpp:197` |
| `unit:GetOwner()` | `Techno_GetOwner` | `src/bindings_techno.cpp:171` |
| `House.GetPlayer()` | `House_GetPlayer` | `src/bindings_house.cpp:79` |
| `World.GetBuildings()` | `World_GetBuildings` | `src/bindings_techno.cpp:956` |
| `World.GetUnits()` | `World_GetUnits` | `src/bindings_techno.cpp:962` |
| `u:GetDistanceTo(bld)` | `Techno_GetDistanceTo` | `src/bindings_techno.cpp:231` |
| `bld:Disable(frames)` | `Techno_Disable` (EMP/blackout path) | `src/bindings_techno.cpp:321` |
| `bld:TakeDamage(amount, warhead)` | `Techno_TakeDamage` (native ReceiveDamage) | `src/bindings_techno.cpp:261` |

No invented/unsupported methods. No new bindings required.

---

## Correctness (VERIFIED from source)

- **No "self-zap" of the player's own buildings.** `isEnemyHouse(player, player)`
  calls `player:IsAlliedWith(player)`. In YRpp `HouseClass::IsAlliedWith(HouseClass
  const* pHouse)` returns **true** when `this == pHouse` (inline impl,
  `third_party/YRpp/HouseClass.h:235-242`), so the player's own house is "allied"
  → `isEnemyHouse` returns `false`. Player buildings are correctly not enemies.
- **`owner == player` by identity works.** The C++ `PushHouse` caches
  `HouseClass* → registry ref` (`src/bindings_house.cpp:47-66`), so `unit:GetOwner()`
  returns the *same* userdata as `House.GetPlayer()` for the player's house →
  Lua `==` is reliable. Generators (player + allies) are collected correctly.
- **Neutrals / civilians / allied houses are not enemies** (`isEnemyHouse` filters
  `Neutral/Civilian/Special` by name and checks `IsAlliedWith`); allied houses count
  as generators (`isPlayerSide`), matching the committed design.
- **Enemy check is on the whole map** (`enemyCount` scans all buildings), so the
  "All enemy structures destroyed!" message is accurate map-wide, and victory
  recovery works when new enemy buildings appear.
- **No cross-frame engine references** are retained — module state is only
  `lastPulseFrame` (number) and `victoryLogged` (boolean). No stale userdata.
- **All engine calls are `pcall`-guarded and re-validated** (`IsAlive()`), and the
  mechanic is frame-gated/synchronous — no `os.time`/`os.clock`, so the
  gameplay-affecting schedule is deterministic.

---

## Risks / issues

- **RISK — post-damage deref of a possibly-destroyed building (use-after-free
  hazard).** After `pcall(bld.TakeDamage, bld, DAMAGE_PER_PULSE, WARHEAD)`, the mod
  calls `bld:GetTypeName` on the same object. If that zap killed the building
  (`hpLeft <= 0`), `ValidateTechno` will read `Health`/`WhatAmI()` on a 0-HP (or,
  worst case, engine-freed) object. In practice RA2 defers building destruction
  (death/rubble sequence) so the memory stays valid and `ValidateTechno` returns
  `false` (Health≤0) → `GetTypeName` yields `nil`, no crash — but this is not
  guaranteed, and it is exactly the "destroyed unit deref" class the repo warns
  about. **Minimal fix:** read the building name *before* the zap (or only log when
  `hpLeft > 0`), so the code never derefs an object it just killed.
- **RISK — log spam.** Only the in-HUD `msg` is throttled (`hits <= 2`), but the
  `print("[LuaAPI] ⚡ Overloading ...")` line fires for **every** hit on **every**
  pulse. With several enemy buildings in-radius this writes a line every ~0.75 s
  and floods `LuaAPI.log` (the repo has explicitly flagged per-volley/frame log
  spam before). **Minimal fix:** throttle the `print` (e.g. only first N per pulse,
  or only on change / every K pulses).
- **RISK — multiplayer OOS.** The mod mutates game state (damage + disable) from
  per-client Lua deterministically on frame cadence. If both clients run it, it is
  fine; if only one client has the mod loaded, that client's game state diverges →
  Out-of-Sync. This is a **general** concern for any gameplay-affecting Lua mod, not
  specific to this one, but it must be verified for CnCNet before relying on it.
- **RISK — `WARHEAD = "TTankWH"` existence is unconfirmed.** The `TakeDamage`
  fallback chain (`TTankWH` → `TerrorBombWH` → `DemobombWH` → `Rules->C4Warhead`,
  `src/bindings_techno.cpp:284-288`) is safe (no crash) even if the ID is absent.
  However, if it falls back to an **explosive** warhead, the intended "electric/
  EMP" feel and AoE behaviour change (and the radius/self-damage semantics could
  differ). Should be confirmed against the rules.
- **LOW RISK — `Disable` re-arm accumulation.** Every pulse calls `bld:Disable(45)`
  for each in-radius building, pushing a new `DisableEntry` into the C++
  `g_disabledEntries` vector; each expires after 45 frames and is re-added next
  pulse. Steady state is bounded (~number of in-radius buildings) and
  `ProcessDisabledObjects` handles expiry, so no leak/crash — but repeated
  `EnableStuff()`/`HasPower` writes occur. Minor.
- **LOW RISK — performance.** Up to two full-world scans (`GetBuildings` +
  `GetUnits`) plus an O(enemyBuildings × generatorUnits) reach check per pulse.
  Throttled to 45 frames, acceptable on small/medium maps; may be heavy on very
  large maps with many units/buildings.
- **LOW RISK — two-pass over buildings.** Pass 1 counts enemies, pass 2 damages.
  A warhead with AoE could destroy several buildings in one `TakeDamage`, but the
  `IsAlive()` re-check in pass 2 handles the (already dead) survivors. Logic correct,
  just slightly redundant.

---

## UNKNOWN

- Whether `TTankWH` is a real YR warhead ID (and thus whether the zap actually uses
  the electric warhead or a fallback).
- Whether a 0-HP building is freed synchronously inside `ReceiveDamage` (determines
  if the post-damage `GetTypeName` is always safe or only "usually safe").
- Live in-game behaviour (not tested from this environment — requires an interactive
  YR session).
- Multiplayer (CnCNet) determinism.

---

## Fixes applied (this session)

`scripts/mods/tesla_overload/main.lua`:
- **Conscript bug (fixed).** Generators are now restricted to Tesla-capable units
  via a new `GENERATOR_TYPES` whitelist (default `TTNK` = Tesla Tank, repo-confirmed).
  `isGenerator()` requires player/allied side **and** type ∈ `GENERATOR_TYPES`. A plain
  Conscript/infantry or non-Tesla vehicle no longer conducts the zap; the aura only
  fires when an actual Tesla unit is within `OVERLOAD_RADIUS` of an enemy building.
- **Post-damage deref (fixed).** The building's name is now read **before** the zap;
  after `TakeDamage` only the returned HP number (and the pre-captured name) are used,
  so a building that dies mid-pulse is never dereferenced again.
- **Log spam (fixed).** The per-hit `print` was removed and replaced with a single
  throttled aggregate log line (every `SUMMARY_LOG_EVERY` = 180 frames). The in-HUD
  `msg` stays throttled to ≤2 per pulse.
- `HOW_TO_USE.txt` updated to document the Tesla-only generator requirement.

## Verdict

The mod is **functionally sound and uses only verified API**; its enemy/generator
classification is correct (no self-zap; player+allies generate; neutrals/civilian/
allies excluded), the radius mechanic is correctly gated, state is primitive-only,
and engine calls are guarded. The reported bug (zap firing near any infantry, e.g. a
Conscript) and the two actionable risks (post-damage deref, log spam) are now fixed.

**Remaining (documented, not code-fixed):** `WARHEAD = "TTankWH"` is unconfirmed in
the rules (the fallback chain is crash-safe, but if it falls back to an explosive
warhead the "electric"/AoE feel changes), and the mod mutates game state per-client
so CnCNet multiplayer may OOS if only one client loads it. These should be confirmed
for the intended use case.

No changes to Smart AI, Adaptive AI, or core LuaAPI were made; only the Tesla
Overload mod + its docs were touched.
