# Bounty Hunter Visual + Dynamic Reward — Gate 1 Research

> Date: 2026-09-20. RESEARCH ONLY, no code changes.
> Rule: every claim carries an evidence level. No upgrades without proof.

## 1. Current capability (baseline)

* `scripts/mods/bounty_hunter/main.lua` — inert historical reference, triple-dead
  (mod-table `OnPreDamage` never dispatched; `OnPreDamage` not wired;
  `house_AddCredits`/`.Owner` not real API). SOURCE VERIFIED (file read).
* Proven replacement pattern: aftermath polling in
  `scripts/mods/command_authority/main.lua:337-411` (`combatScan`, ID-diff
  death detection, `nearestHostileUnit` attribution). SOURCE VERIFIED.

## 2. Draw-hook findings

* `src/barrel_pitch.cpp:27-29,66-67,272-343`:
  MinHook detour on `UnitClass::DrawAsVXL @ 0x73B470`. Signature
  `(UnitClass*, Point2D Coords, RectangleStruct BoundingRect, Brightness, Tint)`.
  SOURCE VERIFIED.
* Q1 screen coords: YES — `Coords` + `BoundingRect` arrive from the engine per
  draw call. SOURCE VERIFIED (hook signature + call-through at :315).
* Q2 rect usable: YES in principle — `DSurface::DrawRect(RectangleStruct*, DWORD)`
  exists (`third_party/YRpp/Surface.h:89-91`). No Lua binding today; needs one
  C++ call site inside the detour. SOURCE VERIFIED (API exists, binding absent).
* Q3 draw-after-original: YES — proven sequence is decide → optional
  `FireAngle` swap → call `g_original` exactly once → restore. A bounty overlay
  draws AFTER `g_original` with no type-field write at all (simpler than pitch).
  SOURCE VERIFIED.
* Q4 simulation: NO effect if draw-only (no health/mission/money/target writes
  in detour). Precedent documented as "Draw-only, client-local, simulation
  untouched — CnCNet-safe" (`API.md` Barrel Elevation section). SOURCE VERIFIED.
* Q5 safe to extend: YES with constraints — MinHook create+enable with WARN
  degradation (`barrel_pitch.cpp:349-379`), SEH tiny helpers (C2712), validate
  `WhatAmI/IsAlive/Health/InLimbo` before dereference. Same idiom as
  `ReadUnitInfoSafe` (:88-140). SOURCE VERIFIED.
* New state needed: `unordered_map<UniqueID, {color, untilFrame}>` + two thin
  bindings (`Mark/Clear`), resolved by ID scan like `SetPersistent` (:419-436).
  No new hook address required for vehicles.

## 3. Text rendering findings

* `DSurface::DrawText(wchar_t*, int X, int Y, COLORREF)` and
  `DrawText(text, Point2D*, COLORREF)` exist (`Surface.h:244-265`).
  SOURCE VERIFIED.
* Live precedent: `src/lua_engine.cpp:576-605` (`DrawHudText` → fixed `12,42`
  yellow text every logical frame). SOURCE VERIFIED.
* Positioned `BOUNTY` above unit: NOT yet proven — requires offsetting from
  `Coords`/`BoundingRect` inside the detour. No source blocker; needs a tiny
  Gate-2 runtime probe. PARTIAL (API + fixed-pos precedent verified, positioned
  bounty text not).

## 4. Unit coverage

* `DrawAsVXL` detour filters `WhatAmI() != AbstractType::Unit → skip`
  (`barrel_pitch.cpp:93-95`). In YR `UnitClass` = vehicles incl. naval;
  infantry/buildings/aircraft draw through separate paths and NEVER reach this
  detour. SOURCE VERIFIED (`bindings_techno.cpp` RTTI switch + YR class split).
* Verdict: vehicles (+ships) COVERED; infantry/buildings/aircraft NOT covered
  by this path (would need extra detours — explicitly out of scope).
  Gate-1 scope (vehicles enough): satisfied.

## 5. UniqueID / lifetime findings

* `UniqueID` = monotonic `++ScenarioClass::Instance->UniqueID`
  (`third_party/YRpp/AbstractClass.h:165`). `GetId()` binding returns it
  (`bindings_techno.cpp:287-294`). SOURCE VERIFIED.
* Destruction hygiene: engine frees aggressively; cached RAW pointers unsafe.
  Required pattern: store `UniqueID → mark`, re-resolve per frame by scanning
  `UnitClass::Array` (precedent: `SetPersistent`, :424-436), validate
  (`WhatAmI/Health/InLimbo`), expiry by `untilFrame`. SOURCE VERIFIED pattern.
* Cross-match staleness: scripts load once per process (`std::call_once`,
  `lua_engine.cpp:1148-1158`); `ResetSession()` exists (:1214) — caller wiring
  not audited here. Lua-side mitigation mandatory: `frame < lastFrame → clear`
  (precedent: `command_authority/main.lua:819-834`) + C++ `ClearAll` on session
  reset. PARTIAL (pattern verified, cross-match probe deferred to Gate 3D).
* ID reuse across matches: UNKNOWN — not tested; expiry + reset-clear is the
  designed mitigation.

## 6. Veterancy capability

* `unit:GetVeterancy() → "rookie"|"veteran"|"elite"` IMPLEMENTED
  (`bindings_techno.cpp:117-133`, reads `TechnoClass::Veterancy`,
  `IsElite/IsVeteran`, SEH-guarded, fail-soft to `"rookie"`). SOURCE VERIFIED.
* Live consumer: `scripts/mods_archive/vet_diag/main.lua:8` uses it.
  HARNESS/LIVE status of vet accuracy: UNKNOWN (no fresh log audited).
* DOC GAP (reported, not fixed here): `API.md` documents no `GetVeterancy`.
  No new binding needed for Gate 2.

## 7. Candidate selection capability

* `World.GetUnits()` = every `TechnoClass::Array` entry except buildings
  (`bindings_techno.cpp:1525-1538`): vehicles + infantry + aircraft.
  SOURCE VERIFIED.
* Filters available: `GetOwner`, `GetKind` (building/unit/infantry/aircraft),
  `IsAlive`, `GetHealth/MaxHealth`, `house:GetName/IsHuman/IsAlliedWith`,
  `House.GetPlayer/GetCount/GetByIndex`. SOURCE VERIFIED (`API.md` + bindings).
* Civilian gate precedent: `NON_COMBATANT = {Neutral, Special}`
  (`command_authority/main.lua:178-190`). Reuse, don't reinvent.
* Gate-1 scope (enemy mobile combat units, no buildings/aircraft/civilians):
  fully expressible with existing bindings. SOURCE VERIFIED.
* RNG note: weighted selection must use deterministic Lua `math.random` seeded
  from logical frame, never `os.time/os.clock` (MP determinism rule).

## 8. Reward/value capability

* `unit:GetCost() → int` IMPLEMENTED (`bindings_techno.cpp:233-250`,
  `TechnoTypeClass::Cost`, e.g. HTNK=700, SEH-guarded, 0 on error).
  SOURCE VERIFIED. Same doc gap as veterancy (`API.md` silent).
* `house:AddCredits(delta)` IMPLEMENTED (`src/bindings_house.cpp:131-132`).
  SOURCE VERIFIED.
* Multipliers (1.5/1.75/2.0) = pure Lua arithmetic on `GetCost()`. No blocker.
  Rounding (int credits): UNKNOWN until Gate-2 harness reads back
  `GetCredits()` deltas.

## 9. Destruction detection capability

* `OnUnitDestroyed` is NEVER dispatched (collects refs, nothing queues them;
  `API.md:1043-1057`, `FSM/*` consensus). Do NOT use. SOURCE VERIFIED.
* Proven alternative: ID-diff polling — snapshot `id → {hp, owner, pos}` each
  scan, treat disappearance as death (`command_authority/main.lua:370-395`).
  SOURCE VERIFIED. Death attribution via `nearestHostileUnit` or direct
  targetId match for the single bounty target (simpler + exact).
* For ONE bounty target the check is trivial: each selection-cycle scan looks
  up `targetId` in `World.GetUnits()`; absent + previously alive → destroyed.
  No fake event needed.

## 10. Required C++ changes (Gate 2, minimal)

1. Mark registry: `unordered_map<uint32_t UniqueID, {uint32_t color, uint32_t untilFrame}>`.
2. Bindings: `unit:MarkBounty(color, durationFrames)` (resolve `UniqueID` from
   self, insert) + `unit:ClearBountyMark()` + `Engine.ClearBountyMarks()`.
   Exact names final at implementation.
3. Detour addition inside existing `Hooked_DrawAsVXL` after `g_original`:
   lookup `info.id` → if marked and `frame < untilFrame` → `DrawRect` on
   `BoundingRect` + `DrawText("BOUNTY")` above it. No sim writes.
4. `ClearAll` wired to session reset path.
5. NOTHING else: no new hook address, no render primitives API, no gameplay
   logic in C++.

## 11. Required Lua changes (Gate 2)

* Rewrite `bounty_hunter/main.lua` as polling state machine:
  `IDLE → (every X s) collect candidates → weight by veterancy
  (rookie 10 / veteran 25 / elite 40, normalized) → single weighted pick →
  MARKED (MarkBounty + announce) → poll targetId → on disappearance:
  reward = floor(cost × mult) → AddCredits → Clear → cooldown → IDLE`.
* Document exact weights/algorithm in-file. Deterministic RNG.
* Match-reset guard + Neutral/Special gate + allied/self-kill exclusion.

## 12. Risks

* Positioned text may need 1-2 draw-probe iterations (font size/color legible
  at game resolution). Mitigation: ship rect first, text second.
* `BoundingRect` tightness varies by unit art; rect may need 2px padding.
  Cosmetic, Gate-3A verdict covers it.
* Undocumented `GetVeterancy/GetCost` accuracy live: UNKNOWN — Gate-3B/C
  cross-checks credits and vet labels against `vet_diag`-style logging.
* ID reuse across matches: UNKNOWN — mitigated by expiry + reset-clear;
  Gate-3D must explicitly test back-to-back matches.
* Scope creep (infantry/buildings/combo/radar): rejected — Non-Goals stand.

## 13. Evidence ledger

* SOURCE VERIFIED: draw-hook signature/coords/rect path; DrawRect/DrawText
  APIs; fixed-pos DrawText precedent; UniqueID generation + GetId;
  GetVeterancy + GetCost implementations; AddCredits; World.GetUnits
  coverage + filters; ID-diff destruction pattern; OnUnitDestroyed
  never-dispatched; MinHook degrade-to-WARN; SEH/C2712 idiom.
* PARTIAL: positioned BOUNTY text (API yes, in-detour proof no);
  cross-match staleness handling (pattern yes, probe no).
* UNKNOWN: vet/cost live accuracy; credit rounding; ID reuse across matches.
* HARNESS/LIVE VERIFIED: none claimed in Gate 1 (no tests run).
* BLOCKED: none for vehicle scope. Infantry/building/aircraft via THIS path:
  BLOCKED by design (separate draw paths) — accepted, out of scope.

## 14. Gate 1 verdict

**PASS** — scoped to vehicles/ships via the existing `DrawAsVXL` detour.
Fundamental visual approach (rect + text from in-detour screen coords,
ID-keyed marks, draw-only) has no source-level blocker. All gameplay inputs
(veterancy, cost, selection filters, destruction polling, credits) exist in
source. Gate 2 may start. Positioned-text proof + live accuracy move to
Gates 2–3 explicitly.

---

# Gate 2 - Implementation (2026-09-20)

> Gate 1 verdict was PASS. Gate 2A built and compiles; live visual proof
> explicitly deferred to Gate 3. One documented deviation: 2B integration was
> implemented against the source-verified (not yet live-proven) visual layer,
> so a single live session proves both.

## 2A. C++ visual POC

* Registry: unordered_map<UniqueID, {color, untilFrame}> (g_bountyMarks,
  src/barrel_pitch.cpp). ID-keyed, never raw pointers. durationFrames==0
  = until cleared. SOURCE VERIFIED (code read after edit).
* Detour: DrawBountyIfMarked(info.id, Coords, BoundingRect) after g_original
  on BOTH paths (vanilla + pitched). Padded DrawRect on BoundingRect +
  DrawText("BOUNTY") above it, all inside one SEH guard; map ops strictly
  outside __try (C2712 rule). No sim writes. SOURCE VERIFIED.
  Build: cmake --build build --config Release -> EXIT 0, no errors (only
  pre-existing C4731), DLL auto-deployed. BUILT.
* Bindings: unit:MarkBounty([color[,duration]])->bool (false for
  non-Unit/invalid - scope enforced in code), unit:ClearBountyMark(),
  Engine.ClearBountyMarks() (src/bindings_techno.cpp,
  src/barrel_pitch.cpp RegisterBindings).
* Session reset: marks cleared in existing BarrelPitch::ClearAll() (called
  from ResetSession, src/lua_engine.cpp:1232) - no caller change.
* Stale-ID hygiene: lazy expiry by frame + opportunistic purge on mark
  (SEH presence probe over UnitClass::Array, fail-keep) + Lua clear on
  death/capture/reselect/reset. No path retains a destroyed pointer.
* Known uncertainty: DrawRect DWORD color format has no in-tree caller
  precedent (only DrawText at fixed pos is live-proven). If the rect color
  is off, the BOUNTY text (proven primitive) still marks the target.
  PARTIAL - decided in Gate 3A live.

## 2B. Lua integration (rewrote scripts/mods/bounty_hunter/main.lua, v2.0.0)

* State: single S.target = {id,type,vet,mult,cost,reward,ownerName,pos}.
* Selection cycle: first at +20s, idle every 30s, one weighted roll per cycle
  (never per frame). Candidates: GetKind()=="unit", alive, combat-house,
  enemy-of-player, non-allied. Weights 10/25/40, id-sorted walk, fixed RNG
  seed (reproducible). No mark -> no bounty (binding false aborts).
* Reward: floor(cost x mult), 1.50/1.75/2.00, via live GetCost().
  Paid to nearest-hostile house at last pos (CA pattern), player fallback.
* Lifecycle: ID re-resolution every 15 frames; disappearance = destroyed ->
  payout; owner change = capture -> void without reward; frame<lastFrame ->
  full reset + Engine.ClearBountyMarks(). No combo (per spec).
* mod.json -> 2.0.0. active_mods.txt untouched (mod stays out of the default
  stack; Gate 3 enables it locally for the live session).

## Harness (tools/tmp/bounty_hunter_test.lua, lua_check.exe)

29/29 pass, EXIT 0. Covers: weights/multipliers contract; no-candidates;
forced elite pick + 1800; mark creation with gold color; WANTED msg; moving
target + pos snapshot; kill -> player +1800 + CLAIMED; cooldown reselect
(rookie 1350); capture void with no payout; infantry/civilian/self
exclusion; mark-failure abort; veteran 1575; reset clears target + global
marks; post-reset run clean. HARNESS VERIFIED (Lua logic only).
NOT covered by harness (C++ pixels): rect/text rendering, moving-marker
follow, DrawRect color exactness - all Gate 3A.

## API docs

API.md: added unit:MarkBounty, unit:ClearBountyMark,
Engine.ClearBountyMarks. Pre-existing doc gap (not fixed here):
GetVeterancy/GetCost implemented (bindings_techno.cpp:117-133,233-250,
live-used by vet_diag) but absent from API.md.

## Gate 2 evidence ledger (delta)

* SOURCE VERIFIED: registry + detour integration + bindings + reset wiring +
  full Lua state machine.
* BUILT: Release build EXIT 0, no new warnings.
* HARNESS VERIFIED: 29/29 Lua behavior (list above).
* UNKNOWN (unchanged): vet/cost live accuracy; credit rounding live;
  DrawRect color exactness; positioned overlay appearance.
* BLOCKED: none new.

## Gate 2 verdicts

* 2A Visual POC: PARTIAL - implemented + built, pixel proof needs Gate 3A.
* 2B Integration: PASS (harness) - logic proven headless; live proof Gate 3.

---

# Gate 3A Visual RCA (2026-09-21): screen-sized rectangle

Live observation: bounty rectangle visible but approximately screen-sized
(HTNK rookie bounty, session 2026-09-20 23:58). DrawRect path works;
geometry is wrong. Gameplay logic untouched by this RCA and the fix.

## Log findings

* LuaAPI.log (23:58:33-00:01:04): bounty_hunter active in the live mod
  stack; "[BOUNTY] hunter active" at 23:58:37; "WANTED: HTNK (rookie,
  Arabs) - dollar 1350" at 23:58:56. No CLAIMED followed in-session.
* The C++ overlay logged NOTHING (no instrumentation existed):
  Coords/BoundingRect runtime values and target UniqueID are UNKNOWN from
  this session, not inferred.

## Root cause

BoundingRect is the redraw CLIP rectangle (view-sized region passed down
the render chain for clipping), NOT the unit screen box. Gate 2A drew it
directly - hence a screen-sized outline. Evidence (all SOURCE VERIFIED):

1. Engine-wide draw convention is always (pLocation + pBounds/pClipRect):
   OverlayTypeClass::Draw(pClientCoords, pClipRect) names it explicitly;
   IsometricTile/Cell/Building/Techno draws all take the same pair.
2. ObjectClass has a SEPARATE GetRenderDimensions() virtual for an
   object own dimensions - redundant if draw calls already received them.
3. DrawIfVisible(pBounds) uses the rect for visibility testing against the
   redraw region; DrawHealthBar/DrawVeterancyPips(pLocation, pBounds) draw
   unit-anchored markers from pLocation with fixed offsets - the exact
   pattern the fix adopts.
4. DSurface helpers (DrawDashed) clip against view-sized ViewBounds.
5. Symptom match: clip rect ~= visible redraw region ~= screen-sized.

Rejected alternatives: Width/Height transform or constant rescale (would
bake one resolution/zoom in; the rect carries no per-unit information at
all). No new world-to-screen system needed: Coords is the voxel blit
anchor (the unit visibly renders at its map position through this exact
call), so it tracks the unit by construction.

## Fix (visual marker only, src/barrel_pitch.cpp)

DrawBountyOverlaySafe now ignores BoundingRect and builds a 72x56 marker
around Coords (half-extents 36/28, chosen size ~Rhino footprint, documented
in-code as approximate, NOT derived from the clip rect); BOUNTY label above
at (Coords.X-30, Coords.Y-44). Plus a 1/sec diagnostic log
"[Bounty] mark id=.. coords=(..) clip=(..)" so the NEXT live session
captures the actual runtime values this session could not provide.
Rebuilt Release EXIT 0 (no new warnings); harness still 29/29 (Lua
untouched). No selection/reward/polling/cooldown code modified.

## Status

NOT live verified - the fix needs a fresh game session (Gate 3A re-run).

---

# Gate 3A Flicker RCA (2026-09-21): every-other-frame rectangle

Live observation: bounty rectangle visible on frame N, gone on N+1,
repeating. Gameplay logic excluded by construction (Lua owns no pixel
API; the only pixel writers are the detour and the engine itself).

## Analysis (SOURCE VERIFIED where stated)

* Inline-hook fact: MinHook patches the DrawAsVXL body, so EVERY execution
  of the function runs the overlay epilogue - calls cannot be bypassed by
  alternate call paths. SOURCE VERIFIED (hook model).
* Therefore per-execution the pixels ARE written; flicker means post-draw
  erasure or presentation of a surface without them - not skipped Lua
  logic (Lua cannot erase) and not a parity-gated mod (no frame counter
  in the mark path; expiry is UINT32_MAX for duration 0).
* Engine uses dirty-rectangle incremental rendering (DirtyAreaStruct +
  Drawing::DirtyAreas, NeedsRedraw/MarkForRedraw across ObjectClass).
  The scene is composed off-Primary (Composite/Hidden/Alternate all exist
  as globals) and presented from a composed surface - a Primary-only
  overlay is erasable by the compose/blit pass. HYPOTHESIS (to be proven
  by the probe log, not asserted).
* Ruled out: mark expiry (duration 0 = persistent), Lua clear paths
  (death/capture/reselect/restart only, all logged via HUD), pitch-path
  interaction (BOTH detour branches call the overlay unconditionally),
  voxel cache (inside g_original; epilogue runs regardless).

## Fix (render-only, src/barrel_pitch.cpp)

* PaintMarkOn paints rect+text on BOTH Primary and Composite (whichever
  exist and differ) - covers compose-to-Primary and direct-to-Primary
  topologies with negligible cost for one target. Temporary
  belt-and-braces; removed once the probe identifies the live surface.
* TEMPORARY per-new-logical-frame probe while a mark is live:
  "[Bounty] frame=.. id=.. path=vanilla|pitched coords=.. clip=..
  surfP/C/H/A=..". Consecutive frame numbers prove every-frame invocation
  (=> erasure model); alternating surface pointers prove flipping.
  To be removed after the RCA is confirmed by a live log.
* Rebuilt Release EXIT 0 (one const-correctness fix on the way:
  DrawRect takes non-const RectangleStruct*); harness 29/29 (Lua
  untouched). No selection/reward/polling/cooldown/lifecycle modified.

## Status

NOT live verified - needs a fresh game session: observe whether the
rectangle is now continuous, and attach the [Bounty] frame-probe lines.

---

# Gate 3A Visual Polish (2026-09-21): ghost + green

Live state entering: marker stable and continuous (dual-surface fix
confirmed by user). Probe log (5974 [Bounty] lines) additionally proved:
overlay invoked on CONSECUTIVE logical frames (erasure model, not skipped
calls); clip constant (0,0,1198,736) = redraw region, definitively NOT
per-unit geometry; Composite pointer ALTERNATES every frame while
Primary/Hidden/Alternate stay stable (compose path is live).

## Ghost RCA

Unclipped DrawRect/DrawText vs engine convention (everything clipped to
the tactical viewport, cf. DrawDashed vs ViewBounds). Bounty unit at the
view edge (probe shows Coords outside 1198 width, e.g. x=1521) pushed
marker pixels onto the static HUD/sidebar, which never repaints on camera
moves -> stale ghosts. FIX (render-only): intersect marker rect with
DSurface::ViewBounds, skip paint when empty, gate BOUNTY text on view
containment. Dual-surface painting KEPT: probe-proven Composite
alternation means the compose path is live; Primary paint (now
view-confined) is harmless. No frame delays, no camera-move hiding.

## Color

Yellow 0xFFFF00 -> green 0x00FF00 (COLORREF green = money) in
TUNING.COLOR, C++ binding/struct defaults, API.md. Visual-only; harness
asserts against TUNING value (29/29 still green).

## Status

Rebuilt Release EXIT 0, harness 29/29. NOT live verified - fresh session
must confirm: green marker, follow, no HUD ghosts after scrolling,
cleanup on kill, next-target marker.

---

# Gate 3A Regression RCA (2026-09-21): crash + still-yellow marker

Session 00:37:58-00:44:19 (6.5 min, frames to 23635), MTNK rookie bounty
id=1056219 active throughout the tail. Ended in a hard crash (log stops
mid-frame-sequence, no shutdown lines).

## Crash

* System fact: Windows Application Error 1000, gamemd-spawn.exe,
  0xc0000005 at gamemd+0x3BC806 (0x7Bxxxx graphics region, near
  Line_In_Bounds 0x7BC2B0) - fault inside ENGINE blit code, NOT in
  LuaAPI.dll. No dump, no LuaAPI error/SEH precursors in the log.
* Bounty pixel args at crash time were sane: Coords (920,650) static,
  clip (0,0,1198,736), clipped rect (884..956, 622..678), text (890,606).
  A bad rect/pointer/surface would have crashed in g_original first or
  much earlier in the 6-minute session, not after 23k stable frames.
* Diff since last stable: ViewBounds intersect (strictly SMALLER rects,
  fewer DrawText calls), color defaults (DWORD by value - cannot AV),
  per-frame probe logging (spdlog, game thread, no shared mutation at
  crash time: single target alive for minutes, zero registry writes).
  Nothing added that plausibly faults inside an engine blitter.
* Confounders: tree carries unrelated dirty work (modified smart_ai,
  486-line bindings addition with native-writing paths); any of them can
  corrupt the heap with delayed effects in engine drawing code.
* VERDICT: UNKNOWN (insufficient evidence). Bounty causation is NOT
  established - the probe lines at the tail carry no causal weight (they
  log every frame by design). No revert: the clip change only reduces
  drawing and the evidence does not support bounty causation. Probe
  instrumentation stays until stability is proven.
* Isolation experiment for the next sessions: if the crash recurs with
  bounty_hunter DISABLED in active_mods.txt, it is definitively not the
  bounty overlay; if it only ever crashes with bounty enabled, revisit
  with a dump (enable WER local dumps first).

## Color (still yellow after 0xFFFF00 -> 0x00FF00)

* Deployment EXONERATED: DLL 00:35:12 + both Lua copies green, session
  00:37:58 ran new code (new probe format + v2 WANTED line), binding
  passes arg 2 through (source-read). The value 0x00FF00 REACHED C++.
* Root cause: DrawRect does NOT consume COLORREF - it takes a RAW 16-bit
  5-6-5 value in the low word. Proof by two consistent live
  observations: 0xFFFF00 and 0x00FF00 share low word 0xFF00 = 565
  R31/G56/B0 = yellow-orange, and both displayed yellow. Layout per
  vendored Color16Struct {B:5,G:6,R:5}. DrawText honors COLORREF.
* Fix (render-only): C++ converts the caller COLORREF to 565 for the
  rect ((R>>3)<<11|(G>>2)<<5|(B>>3); green -> 0x07E0), text unchanged.
  No channel guessing - documented 565 packing. Binding comment + API.md
  updated (COLORREF in, 565 rect internally).

## Status

Rebuilt Release EXIT 0, harness 29/29. NOT live verified - fresh session
must confirm green marker + stability incl. the 9-step checklist.

---

# Crash isolation phase (2026-09-21, no code changes)

## Phase 1 inspection results

* Mod: scripts/mods/bounty_hunter/main.lua v2.0.0 (selection/weights/
  rewards/polling unchanged since Gate 2 except COLOR 0x00FF00 + pcall
  okCall/okMark fix). No gameplay edits in this phase.
* Render code: registry + DrawBountyOverlaySafe (Coords marker, ViewBounds
  clip, 565 rect conversion, COLORREF text passthrough) + per-frame probe
  + both detour branches calling DrawBountyIfMarked unconditionally.
  All read; no edits.
* Bounty vs unrelated diff: bounty = untracked src/barrel_pitch.*,
  ~45 lines in src/bindings_techno.cpp, untracked mod/test/doc, API.md
  bounty sections. Unrelated native surface: +490 lines in
  src/bindings_techno.cpp (harvester/squad/speed/EMP paths),
  src/lua_engine.cpp (22), src/injector_gui.cpp (465),
  modified smart_ai + other Lua mods. Any of these writes natively.
* Git-history comparison IMPOSSIBLE: bounty files are untracked (no
  commits); last-known-stable = previous build binaries + logs only.

## Phase 2 isolation protocol (control test is user-run)

Disable = comment out the `bounty_hunter` line in scripts/active_mods.txt
(`#` = comment per AGENTS.md), fresh game process (scripts load once per
process), same map/conditions, play 10+ min (crash was at ~6.5 min /
frame 23635). Record crash Y/N, frame/time, Event 1000 details, last log
frame. Current build (this phase, unchanged code): Release EXIT 0,
harness 29/29. Probes stay until stability is established.

## Phase 4 green-text audit (no text change required)

DSurface::DrawText(text, X, Y, COLORREF) is COLORREF (Surface.h) and the
overlay already passes the caller COLORREF unconverted - green 0x00FF00
is R/B-swap invariant (R=B=0), so the text is robustly green by
construction. Rect green comes from the 565 conversion (previous phase).
No text edit; live session confirms both.

---

# Crash isolation rig (2026-09-21; control test: bounty-disabled stable)

## Phase 1 inspection (no edits made during inspection)

* Logger EXONERATED as overflow source: log() uses heap
  fmt_lib::format + try/catch + mutex (logger.hpp:58-67) - no fixed
  buffer. Per-frame probe lines are allocation churn, not corruption.
* Registry: plain UniqueID->POD map, no raw pointers, no leaks; purge +
  expiry + reset-clear all reviewed. No invalid-memory path found.
* Remaining unprovable-by-inspection surface: engine virtual calls
  DrawRect/DrawText on engine surfaces (sane args at crash, but same
  call shape every frame for 23k frames). Needs A-E live isolation.

## Diagnostic rig (minimal, this phase)

* Native Engine.SetBountyDrawMode(0..3): 0=off (probe still logs, zero
  pixels), 1=rect, 2=text, 3=full (default). Checked in the detour.
* Lifecycle logs (event-rate): mark/clear/clear-all with id + reg size.
* Probe line extended: reg=N painted=P/C/PC/- (no new lines).
* Lua TUNING.VISUAL_ENABLED (default true): false skips registration,
  full selection/reward/lifecycle otherwise (Test A).
* TEMPORARY F6 key-cycle (3-1-2-0) with HUD announce + pcall guards.
  Harness: 35/35 (29 original + visual-off x4 + F6 x2).

## User protocol (one variable per session, 10+ min each)

* A: VISUAL_ENABLED=false (file edit). No registry, no pixels.
* B: VISUAL true + F6 to mode 0 at start. Registry live, no pixels.
* C: mode 1 (rect only). D: mode 2 (text only). E: mode 3 (full).
* Record per session: crash Y/N, frame/time, Event 1000, last log frame
  (mark/clear/painted lines bracket the exact last bounty operation).
* WER LocalDumps for gamemd-spawn.exe recommended before E (dumps stay
  local): HKLM\SOFTWARE\Microsoft\Windows\Windows Error
  Reporting\LocalDumps\gamemd-spawn.exe, DumpFolder + DumpCount.

## Status

Built Release EXIT 0, harness 35/35. RCA: UNKNOWN (isolation pending).
Gate 3A stays OPEN. Probes stay until stability is established.

---

# Dump forensics (2026-09-21): 3 gamemd crashes, bounty NOT on fault path

Tool: tools/tmp/dump_inspect.ps1 (local minidump parser, no debugger).
Dumps (auto-captured to %LOCALAPPDATA%\CrashDumps, no setup needed):
* gamemd-spawn.exe.19840.dmp @00:44:19 (bounty session, frame ~23635)
* gamemd-spawn.exe.15676.dmp @01:03:51 (session config unknown - log rotated)
* gamemd-spawn.exe.6560.dmp  @11:21:13 (bounty session, frame ~23755)

Findings (identical across all 3):
* Exception 0xC0000005 at gamemd+0x3BC806 (addr 0x7BC806), graphics region.
* Faulting-thread stack top is 100% engine frames (0x411612, 0x4378E7,
  0x4373A2, 0x817758, 0x7BBCCF, 0x7B902A/0x4BB298, 0x7F7BC4, 0x7BA273,
  0x7B8678/0x7B8610 loop, kernel32/ntdll) - ZERO LuaAPI.dll frames in the
  visible 616-852 byte window (~10+ call levels).
* An overlay DrawRect/DrawText crash would HAVE to show LuaAPI return
  addresses at the stack top (detour -> engine virtual). Absent 3/3.
* painted=3 on the last pre-crash probe lines: the overlay COMPLETED
  (both surfaces, no SEH) on the final logged frame - the fault follows
  in subsequent engine render work, not inside the marker calls.
* Both bounty crashes land at ~6.4-6.6 min match time (control-test
  duration therefore must exceed ~7 min to be meaningful).

Conclusion: direct bounty-render causation DISFAVORED by forensics;
correlation (enabled<->crash) unexplained - candidates: latent engine
render bug with timing/heap-layout dependence (observer effect), or
another native writer (dirty tree). A-E live isolation still required;
Test A (no registry/pixels) is now the highest-value run. RCA: UNKNOWN.
Probes stay. No speculative fix applied.
