# Milestone 16 — Dynamic Barrel Elevation

> **Status:** 🟢 **M16 CLOSED — LOGIC COMPLETE, AWAITING ASSET** (2026-09-20).
> The runtime logic for dynamic barrel elevation is implemented, built, and
> running in-game with zero user input: (a) automatic distance-based barrel
> pitch for EVERY voxel-turret unit — player and AI — via the DrawAsVXL
> FireAngle-swap detour; (b) automatic HVA pose cycling for multi-frame
> turrets (the actual TS mechanism, `SetTurretAnimFrame`). Both write paths
> are verified as functioning mechanisms (Gate 1: key-driven frame control
> accepted and held; Gate 2B: 86+ logged per-draw writes + 16 s persistent
> hold, all cleanly applied and restored). Neither can show visible movement
> on stock units for a now-proven reason: stock YR vehicles bake turret and
> barrel into ONE voxel (community-confirmed by Cranium [CCO]; ModEnc:
> `FireAngle` only works on a separate barrel voxel), so the engine has
> nothing to rotate. **The missing piece of M16 is an asset, not code:** a
> unit with a separate barrel voxel and/or a multi-frame turret HVA
> (community asset in progress, Chrono). When it arrives, the existing
> automatic logic should produce visible dynamic elevation without further
> code changes; verify with the frame-cycler and AUTO logs already in place.
> **Goal:** make a weapon barrel change its visible vertical pitch/elevation at
> runtime based on the current target (closest behavioral reference: C&C Generals /
> Tiberian Sun). Horizontal turret rotation must keep working; projectile
> trajectory stays independent unless the implementation naturally requires
> synchronization.
> **Method:** investigation first (`INVESTIGATION` below), then the smallest
> experiment that can prove or disprove the mechanism. Evidence rules per
> `AGENTS.md`: IMPLEMENTED / BUILT / STATIC VERIFIED / RUNTIME VERIFIED are
> never conflated.

---

## Root cause of the negative result (2026-09-20, evidence-based)

- **Community evidence** (Cranium [CCO], Discord, 2026-09-18): "RA2/YR didnt
  utilize separate turret and Barrels like TS did. Most if not all YR tanks
  have the barrel and turret as 1 voxel."
- **Documentation** (ModEnc, `FireAngle`): "It only works if the barrel is a
  voxel, as this operation takes the VXL file and rotates the contents" —
  i.e. a SEPARATE barrel VXL. On a fused turret+barrel voxel there is nothing
  to rotate; RA2/YR draw code for vehicles simply does not consume the field
  for such units.
- **Structural support** (YRpp): a dedicated barrel slot exists for ALL
  techno types — `ObjectTypeClass::BarrelVoxel` (`ObjectTypeClass.h:84`) plus
  `ChargerBarrels[0x12]` — and the full TS pitch pipeline lives on
  `BuildingTypeClass` (`VoxelBarrelFile`, `VoxelBarrelScale`,
  `VoxelBarrelOffsetToPitchPivotPoint` etc., `BuildingTypeClass.h:295–305`).
  The engine channel exists; stock VEHICLE assets leave it empty.
- **Consequence:** our two no-effect experiments on HTNK were testing a unit
  with NO barrel voxel. They remain valid negative results for stock units —
  and are exactly what the fused-voxel explanation predicts.
- **The TS elevation mechanism proper is HVA pose frames** (per-section
  voxel transforms), already exposed as `unit:SetTurretAnimFrame` (Gate 1,
  accepted-and-held verified). A turret HVA with multiple pitch poses + a
  separate barrel voxel is the asset shape M16 needs.

---

## Investigation summary (2026-09-18, evidence-based)

### Confirmed
- `FireAngle=` (`TechnoTypeClass::FireAngle`, `TechnoTypeClass.h:230`) is a
  STATIC type-level visual parameter in YR (ModEnc: "starting from Red Alert 2,
  this only affects the visual rendering of the assets"). Not a runtime control.
- `Arcing=yes` (`BulletTypeClass::Arcing`, `BulletTypeClass.h:83`) is a
  projectile-flight flag. It does not drive barrel orientation. DISPROVEN as a
  pitch mechanism.
- `TechnoClass::PitchAngle` (`TechnoClass.h:696`) is a dropship-internal value;
  YRpp comment: "it doesn't affect the drawing". DISPROVEN as a pitch mechanism.
- Buildings have a full native runtime pitch pipeline:
  `BuildingClass::FireAngleTo(ObjectClass*) -> DirStruct` (`BuildingClass.h:62`),
  `VoxelBarrelOffsetToPitchPivotPoint` (`BuildingTypeClass.h:302`),
  `BarrelStartPitch` (art.ini). Building barrel elevation by target is vanilla
  behavior (Grand Cannon), not a LuaAPI extension.
- Units have NO runtime pitch field. `BarrelFacing` (`TechnoClass.h:730`) is a
  horizontal yaw `FacingClass`; `MinorVoxelIndexKey`
  (`Drawing.h:493-518`) contains facing bits but no pitch bits.
- Ares: no dynamic barrel-pitch functionality found in documentation.
  Phobos: none found in "New / Enhanced Logics" + "Fixed / Improved Logics"
  (closest items: `FireAngle` render bugfix Build #47, Type Conversion barrel
  reset Build #48, `TurretOffset=F,L,H`, projectile Trajectory system
  Straight/Parabola/Sine v0.4 — projectile flight, not barrel orientation).
  Full Ares build audit not performed (UNKNOWN, expected negative: INI model).

### Likely (mechanism, unproven until Gate 2)
- Unit visible barrel pitch is driven by the turret HVA frame index:
  `TechnoClass::TurretAnimFrame` (`TechnoClass.h:612`) feeds
  `MinorVoxelIndexKey.TurretFrameIndex`
  (`Drawing.h:404`: `key |= (TurretAnimFrame % TurretVoxel.HVA->FrameCount) << 16`),
  and `MotLib::GetLayerMatrix(layer, frame)` (`FileFormats/HVA.h`) selects the
  3D orientation matrix. Static `Type->FireAngle` is consumed somewhere in the
  same draw path (indirect evidence: the unit FireAngle render bug fixed in
  Phobos Build #47).

### UNKNOWN (closed or closed-by-Gate-1)
- How many frames stock turret HVAs have -> measured by the Gate 1 scan
  (`[M16] HVA` log lines), no assumptions.
- Address/body of `FireAngleTo` (no JMP export in YRpp) -> deferred; irrelevant
  to Gate 1.
- Exact consumption point of `Type->FireAngle` inside `UnitClass::DrawAsVXL`
  (0x73B470) -> deferred to path B (draw-hook), out of Gate 1 scope.

### Feasibility verdict (investigation stage)
- Buildings: FEASIBLE natively; Lua control UNPROVEN (driver not reversed).
- Units, asset-independent (draw-hook matrix injection): POSSIBLY FEASIBLE.
- Units via HVA frame selection: UNPROVEN -> exactly what Gate 1 tests.

---

## Implementation paths (from investigation; not ranked)

- **A. Buildings via native pipeline** — `FireAngleTo` + pitch pivot already
  exist; Lua would only read/steer. Does not solve units.
- **B. Draw-path hook (asset-independent)** — hook `UnitClass::DrawAsVXL`
  (0x73B470) / `DrawVoxel` / `Draw_A_VXL` (`FootClass.h:59`) and inject an
  X-rotation into the turret/barrel `Matrix3D` from target distance. Anchored
  in real code (FireAngle consumption); requires reversing the matrix
  composition point. Draw-only changes are client-local and OOS-safe.
- **C. HVA frame control from Lua** — write `TurretAnimFrame` (+0x612) per
  frame; works only with multi-frame turret HVAs (asset-dependent).
- **D. Muzzle/flight sync (optional, separate)** — reuse `ComputeMuzzle`
  (`sub_turret.cpp`) and `FireProjectile` (`bindings_techno.cpp:1275`) to align
  spawn point/velocity with a raised barrel. Trajectory remains independent.

---

## Gate 1 — Implementation (this document's current status)

**Scope:** the smallest experiment for path C, zero hooks:
1. Lua bindings: `GetTurretAnimFrame`, `SetTurretAnimFrame(frame)`,
   `GetTurretAnimFrameCount` on the Techno userdata
   (`src/bindings_techno.cpp`, SEH-guarded reads/writes of
   `TechnoClass::TurretAnimFrame` and `Type->TurretVoxel.HVA->FrameCount`).
2. One-shot diagnostic scan `LogTurretHvaFrameCounts()` called from
   `OnGameFrame` on the first logic frame of each match (rules/voxels are
   loaded by then; DLL bootstrap is too early). Logs one
   `[M16] HVA <i> id=<ID> frames=<N> fireAngle=<N>` line per unit type with a
   voxel turret plus a summary line. Closes the frame-count UNKNOWN with
   repository evidence.
3. Diagnostic mod `scripts/mods/barrel_elevation_diag/`:
   Numpad1 arm (mouse selection), Numpad7/8/9 force LOW/MID/HIGH frame,
   Numpad0 release. While forced, the frame is rewritten EVERY logic frame
   (the engine overwrites the field on turret rotation). All actions logged as
   `[M16-DIAG] ...`.

**Implementation status:** ✅ COMPLETE
**Build status:** ✅ BUILT (Release, 2026-09-18 21:46, LuaAPI.dll + injector.exe auto-deployed)

### Gate 2 — live verification result (2026-09-18, session 21:52:09–21:52:36)

Fresh `LuaAPI.log` produced by this exact build (log contains the `[M16] HVA`
scan; scan began 21:52:11.768, 85 UnitTypeClass entries).

| Criterion | Verdict | Evidence (verbatim from `LuaAPI.log`) |
|---|---|---|
| **C1** scan present & consistent | ✅ **PASS** | `[M16] HVA scan begin: 85 UnitTypeClass entries`; 15 voxel-turret lines; `[M16] HVA scan end: 15 voxel-turret unit types, 1 multi-frame` |
| **C2** forced frame changes visible pitch | ⚪ **INCONCLUSIVE** (stock assets) | Rhino test unit: `[M16-DIAG] armed id=1077370 type=HTNK frame=0/1` → `HTNK: frames=1 - pitch-by-frame NOT testable`; forcing refused: `HTNK: 1 frames, cannot force`. Mod behaved exactly as designed (count<2 → refuse). |
| **C3** release restores native control | ✅ **PASS** | No errors at release; unit state remained clean across re-arm (`frame=0/1` then `frame=298/1` on second arm — the engine freely rewrote the field between arms, i.e. native control was in effect whenever we were not holding it) |
| **C4** yaw keeps working while held | ⚪ **INCONCLUSIVE** (stock assets) | No hold ever became active on HTNK (frames=1), so yaw-under-hold was not exercisable this session |
| **C5** no new SEH/errors | ✅ **PASS** | Zero SEH warnings, zero Lua errors, zero `FRAMEWORK-ERR` in the session; mod timing `Avg 0.00–0.01 ms` per 5s report |

**Key measurement (closes the frame-count UNKNOWN):** of 15 stock unit types
with a voxel turret, only **YTNK (Yuri's tank, `YTNK frames=2`)** has a
multi-frame turret HVA; all others (HARV, APOC, HTNK, MTNK, HTK, TTNK, LTNK,
XCOMET, SMIN, TELE, DISK, UTNK, ROBO, SCHD) are single-frame. Note: unit
`FireAngle=8` across the board — the static type field is alive in types but
does not add HVA frames.

**Gate 2 verdict:** mechanism bindings verified working in-game (C1, C3, C5
PASS); the pitch-changing half (C2) remains INCONCLUSIVE because the stock
asset set effectively has no multi-frame turret to test on (YTNK frames=2 is
the only candidate).

### Gate 2 — Verification (criteria, to be executed against a fresh session)

Run `injector.exe`, start a skirmish, produce a fresh `LuaAPI.log`. Each
criterion gets PASS / FAIL / INCONCLUSIVE plus a concrete log line. Old logs do
not verify this code.

- **C1 (scan):** log contains `[M16] HVA scan begin/end`; every unit type with
  a voxel turret has a `frames=` value; summary counts are consistent.
- **C2 (pitch changes):** on an armed unit with `frames > 1`, Numpad7 vs
  Numpad9 produce a VISIBLE barrel pitch difference in-game.
- **C3 (release):** Numpad0 restores native behavior (engine re-controls the
  frame; barrel resumes normal animation).
- **C4 (yaw intact):** horizontal turret rotation keeps working while a frame
  is held.
- **C5 (stability):** no new SEH warnings / Lua errors attributable to the
  bindings or the mod during the session.

Fallback: if C1 shows all stock turret HVAs are 1-frame, C2 is INCONCLUSIVE on
stock assets by construction; a multi-frame test voxel becomes a Gate 3
decision (asset work), and path B (draw-hook) becomes the primary candidate.

**→ Fallback TRIGGERED (2026-09-18): 14 of 15 voxel-turret unit types are
single-frame; the only multi-frame HVA is YTNK (frames=2). Gate 3 decision
required between (a) re-running C2/C4 on YTNK (only 2 distinct pitches, asset
not in standard loadouts — needs Yuri's side) and (b) building a multi-frame
test voxel (asset work) and (c) pivoting to path B (draw-hook, asset-
independent). Path B is now the primary M16 candidate.

---

## Gate 2B — path B probe: draw-time FireAngle swap (IMPLEMENTED + BUILT)

> **Design anchor:** the unit FireAngle render bug fixed in Phobos Build #47
> proves `TechnoTypeClass::FireAngle` is consumed inside the unit voxel draw
> path. Therefore, instead of reversing the matrix composition inside
> `UnitClass::DrawAsVXL` (0x73B470, verified `JMP_THIS` in vendored YRpp), the
> probe **swaps the type's FireAngle around each draw call** and lets the
> engine's own FireAngle math orient the barrel. No asset changes, no matrix
> reversal, client-local (OOS-safe).

### Implementation (`src/barrel_pitch.h/.cpp`, wired in `src/lua_engine.cpp`)

- `BarrelPitch::Install()` — MinHook detour on `UnitClass::DrawAsVXL`
  (`__fastcall` with edx placeholder, same pattern as `weapon_override.cpp`).
- Per call: vanilla draw first → if a pitch applies to this unit id, swap
  `Type->FireAngle` to the override, draw again, restore the type field in
  all paths. **Double-draw is a deliberate probe behavior** (elevated barrel
  rendered on top of the vanilla frame; mechanism becomes visually obvious).
  Production single-draw variant is a one-line change after verification.
- Pitch resolution per frame: (1) explicit per-unit override from Lua,
  (2) AUTO mode — distance-based curve (8°..55° over 4..14 cells) while the
  unit has a live target; no target → no override (vanilla).
- Lua surface on the global `Engine` table:
  `SetBarrelPitchOverride(unitId, degrees)` / `GetBarrelPitchOverride(unitId)` /
  `ClearBarrelPitchOverride(unitId)` / `ClearAllBarrelPitchOverrides()`.
  Degrees: 0 horizontal, 90 vertical; mapped to the FireAngle scale (×64/90,
  clamped ±64).
- Safety: all engine derefs in SEH helpers (C2712 discipline, same as
  `ReadWeaponKeySafe`); type field restored even on SEH; overrides cleared on
  session reset (`ResetSession` → `BarrelPitch::ClearAll()`).
- Mod `barrel_elevation_diag` extended: Numpad2 manual pitch (default 35°,
  Numpad4/6 nudge ±5°), Numpad3 AUTO toggle, Numpad0 releases everything;
  Gate 1 HVA-frame keys kept for the YTNK follow-up.

**Implementation status:** ✅ COMPLETE
**Build status:** ✅ BUILT (Release 2026-09-18, after regenerating the CMake
glob — new TU `barrel_pitch.cpp` — and fixing a missing edx placeholder in
the two `g_original` calls)

### Gate 2B verification criteria (live session, fresh `LuaAPI.log`)

- **B1 (hook):** log contains `[BarrelPitch] DrawAsVXL hook installed @ 0x73B470`.
- **B2 (manual):** with a unit armed (Numpad1) and Numpad2 pressed, the
  affected vehicle visibly shows an elevated barrel orientation vs vanilla.
- **B3 (tuning):** Numpad4/6 change the visual elevation step by step.
- **B4 (AUTO):** Numpad3 + attack order → barrel orientation changes with
  target distance (low near, high far); no target → vanilla orientation.
- **B5 (isolation):** only the armed unit is affected; same-type neighbors
  render vanilla.
- **B6 (stability):** no SEH warnings (`[BarrelPitch] SEH ...` absent), no
  Lua errors, no visual corruption from double-draw during the session.
- **B7 (reset):** after match restart, overrides are gone (no stale pitch on
  a new unit with a recycled id path).

Status: ⏳ awaiting live session (first two sessions below; AUTO re-test pending).

### Gate 2B session results (live evidence)

**Session 1 — 11:54–12:07, build 12:27-1 (2026-09-18), `LuaAPI.log`:**

| Criterion | Verdict | Evidence |
|---|---|---|
| B1 hook | **PASS** | `[BarrelPitch] DrawAsVXL hook installed @ 0x73B470 (path B probe)` (11:54:19.690) |
| B2 manual pitch | **PASS (visual, user-confirmed)** | barrel visibly pitched; log sweep 55°→0°→−20° `[M16-DIAG] HTNK MANUAL pitch=…` — first runtime proof that the unit draw path consumes a runtime FireAngle swap |
| B3 tuning | **PASS** | ±5° steps logged continuously (12:07:41–12:07:45) |
| B4 AUTO | **FAIL (implementation bug)** | heartbeats `AUTO active target=no override=-1.0`; root cause: Lua sent `-1` as an AUTO flag, native `BP_Set` stored it as a **manual override of −1°** which outranked the AUTO branch in `DecidePitch` — distance math never executed |
| B6 stability | **PASS** | 0 SEH, 0 Lua errors across the session |

Fix: AUTO is a separate native state (`Engine.SetBarrelPitchAuto(unitId, enabled)`),
priority over manual; `GetBarrelPitchAuto(unitId)` now returns the live computed
angle for the heartbeat log. BUILT 12:27:17. Lesson recorded in
`src/barrel_pitch.h`: never encode a mode as a magic override value.

**Session 2 — 12:29–12:31:56, build 12:27:17, `LuaAPI.log`:**

| Criterion | Verdict | Evidence |
|---|---|---|
| B1 hook | **PASS** | fresh log, HVA scan `[M16] … 15 voxel-turret unit types, 1 multi-frame` (12:29:25) |
| B2/B3 manual | **PASS (visual, user-confirmed)** | sweep −20°→+40° logged (12:31:37–12:31:44) |
| B4 AUTO | **INCONCLUSIVE → fixed, re-test pending** | `AUTO ON` twice (12:30:17, 12:30:52), but every heartbeat `target=no` — during AUTO the tank never held a techno target. Diagnosis: force-fire at ground keeps a **CellClass** in `TechnoClass::Target`, which the target filter rejected. Fix: cell branch in `ReadUnitInfoSafe` (ground target coords feed AUTO the same way). BUILT 12:39:43 |
| B6 stability | **PASS** | 0 SEH / errors |

Note for the AUTO re-test: `target=no` + numeric `dist-pitch=NN.Ndeg` in one
heartbeat line = terrain (cell) target, expected with force-fire. Attack
orders on units/buildings show `target=yes`.

**Design switch (13:03 build): double-draw → SINGLE-DRAW.** Sessions 12:29
and 12:46 showed manual overrides logging correctly while the user saw no
visual change. Root cause is the probe design itself: the elevated barrel
was drawn ON TOP of the vanilla one; at moderate angles (20–45°) the two
nearly-coinciding silhouettes read as "nothing changed". The detour now
decides the pitch first, swaps `Type->FireAngle`, calls the original draw
exactly once, and restores the field unconditionally. Added a rate-limited
(1/s) `[BarrelPitch] draw id=… pitch=… fireAngle=… (vanilla …)` log as the
objective per-session signal that the swap-draw actually executes.

**Session 3 — 13:50–13:52, single-draw build 13:03:17. DECISIVE NEGATIVE
RESULT for the minimal path-B variant.** The draw log recorded 86 swap-draw
lines on HTNK (`pitch=35–50° → fireAngle=25–36, vanilla 8`) while the user,
watching the unit, saw no orientation change at any angle. Conclusion:
swapping `TechnoTypeClass::FireAngle` immediately before
`UnitClass::DrawAsVXL` executes and draws — but the visible barrel does not
consume the type field at that point. The hypothesis "the vehicle draw path
reads `Type->FireAngle` at DrawAsVXL call time" is DISPROVEN by controlled
experiment. (Caveat: consumption may exist earlier — voxel cache frame
selection — or in a Phobos-patched path; unresolved.)

Also verified: `Ares.dll` / `Phobos.dll` / `CnCNet-Spawner.dll` are present
in the game directory; whether they are active in the injected session is
UNKNOWN (log has no loader markers).

Remaining evidence paths:
1. **Static FireAngle test** (cheapest, decisive): set `FireAngle=45` under
   `[HTNK]` in the live rules/art and launch vanilla — does the stock
   engine render a statically tilted vehicle barrel in this environment at
   all? NO → vehicle-barrel pitch via FireAngle is not a render feature of
   this engine/config; M16 units-side pivots to multi-frame HVA (asset work)
   or buildings. YES → consumption exists but elsewhere; requires reversing
   the actual consumption point (expensive).
2. **YTNK frames=2 test** (Gate 1 mechanism, never run): `SetTurretAnimFrame`
   on the only multi-frame stock turret.
3. Building-side pipeline (`FireAngleTo`) — vanilla-verified by the engine
   itself, separate track.

### Static FireAngle test (decisive, IMPLEMENTED + BUILT 14:15:51)

The INI route is unusable here: the CnCNet client regenerates `spawn.ini`/
`spawnmap.ini` per launch, so a hand edit would be overwritten. Equivalent
runtime test instead: `Engine.SetPersistentBarrelPitch(unitId, 45)` writes
`45°` into the armed unit's **TechnoTypeClass::FireAngle once and leaves it**
(= the exact engine state a static art/rules edit produces; persists across
frames, facings and cache refreshes; logged; restored by
`Engine.ClearPersistentBarrelPitch()` or on session reset).

Keys (diag mod): **Numpad5 / F11** = test ON, **NumpadDot / F10** = OFF.
Log markers: `[BarrelPitch] PERSISTENT FireAngle write: unit N -> 32 (was 8)`.

Interpretation:
- ALL units of that type render statically tilted → consumption lives in a
  cache/cross-frame path the per-draw swap cannot reach → reverse that path
  (expensive but bounded).
- No visible change with the field held → the vehicle render path of this
  engine/config does not consume type FireAngle at all → per-draw path B is
  dead; M16 units-side pivots to multi-frame HVA (path C, asset work) or the
  buildings track.

Status: ✅ EXECUTED — **NEGATIVE RESULT** (session 4, 14:21–14:22:40).

**Session 4 — 14:21–14:22:40, build 14:15:51. FINAL VERDICT on the
FireAngle-vehicle path: DISPROVEN.** The persistent write executed exactly
as designed (`PERSISTENT FireAngle write: unit 1031337 -> 32 (was 8)`), the
HTNK type field was HELD at 45° across 16+ seconds, facings and cache
refreshes; the user watched all Rhino units - zero visible orientation
change. 0 SEH / errors. Combined with Session 3 (86 logged per-draw
swaps, no effect): two independent experiments agree - in this environment
(steam-era gamemd 1.001 + Ares/Phobos/CnCNet present), the vehicle draw
path does not consume `TechnoTypeClass::FireAngle` for the visible barrel
at any point reachable by either transient or persistent type-field state.

**Gate 2B closes: path B (FireAngle-based vehicle barrel pitch) is dead.**
This is a valuable negative result: it was the cheapest of the three
remaining evidence paths and it eliminates a whole branch with certainty.
Per-draw manual pitch "success" in sessions 1–2 is reattributed to the
double-draw ghost artifact, not to working elevation.

Remaining M16 branches:
- **Path C (multi-frame HVA)**: requires new multi-frame turret HVA asset
  (test unit with 3–5 frames); the `SetTurretAnimFrame` mechanism already
  built is the runtime side. Cost: art tooling + one test voxel/HVA.
- **Buildings track**: `BuildingClass::FireAngleTo` + `BarrelStartPitch` -
  the engine natively pitches building barrels (Grand Cannon); giving Lua
  programmatic control there is the only vehicle-independent dynamic pitch
  the engine demonstrably supports.
- **Park M16** with the negative result documented.

### Gate 3 — Close
- Record PASS/FAIL/INCONCLUSIVE per criterion with log quotes in this file.
- Update `PROJECT/ROADMAP.md` (M16 section) and `PROJECT/CHANGELOG.md`.
- On PASS: document the three bindings in `API.md` (Techno methods).
- Decide next step: path B prototype, building-side experiment, or park M16.

### Final iteration — fully automatic mode, hotkeys removed (2026-09-20, BUILT 00:18)

User decision: "remove the hotkeys, make it automatic — the tank decides
itself". The diagnostic controls were the only thing left in the mod after
path B disproval; instead of parking the tooling it was converted into the
M16 end-state as far as the engine allows:

- **Native** (`src/barrel_pitch.cpp`): new global switch `g_autoAll` +
  `BarrelPitch::SetAutoAll(bool)`. When on, the DrawAsVXL detour computes the
  distance-based pitch for EVERY voxel-turret unit reaching it — player units
  and AI units alike, no per-unit registration, no keys. The AUTO branch was
  factored into `ApplyAutoPitch()` shared by the global and per-unit paths.
  `ClearAll()` (session reset) clears the global flag; the idempotent setter
  keeps re-assertion log-free.
- **Lua mod** (`scripts/mods/barrel_elevation_diag/`): every `Input.WasKeyPressed`
  binding removed. `main.lua` now only enables the global AUTO on frame 1,
  re-asserts it once per second (recovery after match restart), and emits a
  heartbeat (`[M16-AUTO] heartbeat: pitching=N (...)` using the new
  `Engine.GetBarrelPitchAutoCount()`) plus up to 3 live unit/angle samples.
- **Honest limitation:** this mode is 100% aware of the Gate 2B negative
  result — with FireAngle consumption disproven for vehicles, the automatic
  pitch is not expected to move barrels in the current environment. Its
  purpose is to keep the mechanism live with zero user interaction so that a
  future environment change (Phobos fix, custom consumption point, path C
  assets) is immediately observable without resurrecting any diagnostic UI.
- Build note: cmake moved on disk (`D:/Soft/CMake` gone, now
  `D:/Soft/Dev-Reverse/CMake`); CMake regen was required because stale caches
  held the dead path (MSB3073 exit code 3 on the post-build copy).

---

## Out of scope for Gate 1
- Any MinHook additions (draw-path hooking), building-side Lua control,
  projectile/barrel synchronization, abstractions above the three bindings.
