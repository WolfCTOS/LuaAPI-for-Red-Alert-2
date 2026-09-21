# Rhino RCA — Revision (2026-09-20, no new run)

Supersedes the targeting verdict of `RHINO_RCA_RUNTIME.md` (that file stays
intact as the run record). Reason: live player observation that **even an
Arabs-owned CA-spawned Rhino was ignored by a Psychic Tower and player units
until manually ordered**. The previous `owner=Neutral → root cause` verdict
is WITHDRAWN as the targeting root cause (Neutral-as-recipient stands as a
separate CA bug, §5).

Method for this revision: existing runtime evidence (same session log,
`rhino_rca_session_2026-09-20.log`) + full static lifecycle comparison.
No API extension, no C++ changes, no fix applied, no second run.

## 1. FACTS (revised)

**SpawnUnit engine-call inventory** (`src/bindings_house.cpp:177-206,
304-396`; CA call site `command_authority/main.lua:431` with
`force=true, action=""`):

1. `UnitTypeClass::Find(typeId)` (SEH) — same type object factories use.
2. `GameCreate<UnitClass>(pType, pHouse)` — game allocator + concrete
   `UnitClass::UnitClass(UnitTypeClass*, HouseClass*)` @0x7353C0. Owner is a
   ctor argument: ownership is set at construction, not patched later.
3. `CellClass::Cell2Coord` + `pUnit->Unlimbo(coord, dir)` — the placement
   primitive (same one vanilla creation funnels through). Returns false →
   counted as failure (spiral fallback or skip).
4. NOTHING ELSE: no `ForceCreate`, no `CreateObject` virtual dispatch, no
   `QueueMission` (CA passes `""`), no veterancy/discovery/threat/house-
   bookkeeping/team calls. Our C++ also never calls `UpdateThreatInCell`,
   `DiscoveredBy`, `Reveal`, `CanAutoTargetObject`, `TryAutoTargetObject`
   (verified by repo-wide grep — these exist only as YRpp declarations).
5. Minor hygiene (inspection-confirmed, NOT a cause): if `Unlimbo` fails on
   the force path, the `GameCreate`'d object is dropped while still InLimbo
   (leak; observed 2× at `(45,90)` in-run). InLimbo objects fail Lua
   validation and engine liveness checks alike.

**Native post-processing of spawned units (observed, i.e. lifecycle
continues):** fresh spawns read `Guard` (Neutral pairs) / `Area Guard`
(Arabs-at-base pair) with zero Lua orders — the engine assigns guard
missions itself. Spawned units self-acquire targets ≤45f in every near-base
sample (YACNST/YAREFN/CAOILD/LTNK), retaliate when damaged
(1056262 and 1056689 both flipped to `Attack` after taking fire), accept
manual `Attack` (attackCall=true, target locked, 130/poll, kills), and die
normally. **H1 (broken lifecycle) is refuted behaviorally.**

**Ownership-correlated acquisition (ALL 60+ samples, zero exceptions):**

- Arabs-owned spawned near player (n=5: synth 1058184 + Director pairs
  1063307/09, 1063491/93): first shot taken ≤15f, player target-lock ≤45f
  (`LTNK target=HTNK`, `YAREFN target=HTNK`), dead in ~105–210f.
- Neutral-owned spawned near player (n=10 pairs + twins): `Guard/NONE/400`
  at +45f at 1–4 cells (incl. SLAV@1.0 and LTNK@2.2 — CLOSER than the Arabs
  samples' neighbors, so density does not explain the gap); first damage
  +105f at earliest; survival 300–600f+; buildings `target=NONE` in 40+
  samples across 600f windows.
- Neutral initiation asymmetry (mission-level, logged): ZERO initiated
  attacks across 10 pairs (Guard/idle held with live building targets at
  1.0 cell for 195f+); the only two `Attack` flips were post-damage
  retaliation. Player initiation vs Neutral: delayed (INIT@105f is the
  earliest organic engage; LTNK NONE@45f).
- No factory-HTNK-in-contact samples exist in the run (37 factory Arabs
  HTNKs stayed at x≈100–113; player raiders never closed to <36 cells) —
  factory-vs-spawned CONTACT comparison is UNOBSERVABLE from this evidence.
- No weapon-tower-in-range samples exist (nearest-building sampling only
  ever hit YACNST/YAREFN/NATBNK; YAGGUNs built late/far; YAPSYT never
  built) — tower-vs-spawned is UNSAMPLED, see §4.

## 2. FACTORY VS SPAWNED (HTNK)

| Step | Factory path (vanilla) | `House:SpawnUnit` | Consequence |
|---|---|---|---|
| Type lookup | Rules INI type object | `UnitTypeClass::Find` — same object | none |
| Alloc+ctor | `CreateObject` virtual → `new UnitClass(type,house)` | `GameCreate<UnitClass>(type,house)` → same ctor @0x7353C0 | equivalent (bypasses virtual dispatch; same concrete class for HTNK) |
| Owner | ctor arg | ctor arg (`GetOwner` correct in 58/58) | none |
| Placement | factory-door exit cell + `Unlimbo` | `Cell2Coord` + clearance spiral + `Unlimbo` | equivalent primitive |
| Mission | `Move` out → `Guard` | none ordered; engine assigns `Guard`/`Area Guard` natively (observed) | equivalent outcome |
| Veterancy/HP | 0 / full | 0 / full (`400/400` every NEW) | none |
| Discovery | sight-driven, no explicit calls | sight-driven, no explicit calls | same (nothing to miss) |
| Threat registration | `UpdateThreatInCell` (0x70F6E0) call sites | not called by LuaAPI | UNVERIFIABLE statically (call sites in binary); behaviorally, spawned units DO get threatened eventually → no evidence of omission |
| House bookkeeping (`CountedAsOwned`, AI counts) | production path updates | skipped | no observed targeting impact (manual/self-acquire/eventual-acquire all work) |
| Team/AI adoption | may join AI teams | none (Arabs pair showing `Area Guard` suggests native AI adoption at own base) | posture, not eligibility |
| `DiscoveredBy` per-house flags | sight-driven | sight-driven | UNOBSERVABLE WITH CURRENT API (no getter); spawns were in visible areas, both owner types equal |
| Exact Guard-scan cadence / `CanAutoTargetObject` (0x6F7CA0) / `TryAutoTargetObject` (0x6F8960) internals | engine binary | engine binary | UNOBSERVABLE WITH CURRENT API; YRpp gives addresses/signatures only |

Net: **no lifecycle step with demonstrated targeting relevance is missing.**
The only targeting-correlated variable in 60+ samples is the recipient
house, whose engine property is `HouseClass::IsNeutral() ==
Type->MultiplayPassive` (`HouseClass.h:746`) — a property of the HOUSE CA
chose, not a defect of the UNIT.

## 3. TARGETING ANALYSIS (explicit vs automatic)

Two separate systems, both evidenced:

1. **Explicit target acceptance — WORKS for every spawned sample.**
   `Attack()` → `SetTarget + QueueMission(Attack)`; target readable back;
   damage/kill pipeline normal. This rules out validity, ownership,
   alliance (`alliedWithPlayer=false` everywhere), position, mission, and
   weapon eligibility as blockers.
2. **Automatic acquisition — gated on owner-house belligerence.**
   Model fitting 100% of samples: passive-house (`MultiplayPassive`)
   units are not valid auto-targets until hostile (firing/damaging);
   retaliation is always allowed — on BOTH sides. Neutral rhinos acquire
   but don't initiate (Guard+target held 195–600f, fire only after being
   hit); player mobiles/buildings return NONE for them for 45–600f, then
   engage (INIT@105f; slow attrition to death/near-death in all pairs).
   Arabs-owned (active belligerent) are valid immediately (≤15–45f).
   The mechanism class is **H4 (player-side acquisition property)**;
   the `Neutral` recipient choice is CA's separate bug (§5), and the
   `GameCreate+Unlimbo` primitive is shared with working vanilla features,
   so H1/H2 are refuted.

On the user's Arabs observation: zero instrumented samples show a TRUE
Arabs-owned spawned Rhino ignored (all 5 died ≤210f near player forces —
too fast to even finish noticing, let alone manual-order). A spawned Rhino
standing unengaged long enough to REQUIRE manual orders matches ONLY the
Neutral signature. HTNK ownership is not reliably readable by eye (identical
voxel; only the house stripe differs), while our ownership data is
`GetOwner()`-instrumented. Most probable reconciliation: the observed Rhino
was Neutral-owned (and/or tower-situational: range/power/state — all
unsampled). Discriminating test: a TRUE Arabs-owned spawned Rhino ignored
>300f next to powered, in-range defenses would be new evidence; the run
contains none.

## 4. ROOT CAUSE (revised)

**Player-side auto-acquisition filters on owner-house belligerence; it is
not a spawned-unit defect.** `House:SpawnUnit` builds a lifecycle-complete
unit (owner/type/kind/HP/mission/self-targeting/manual-targeting all
normal). The engine's automatic path treats `MultiplayPassive`-owned combat
units as non-threats until they demonstrate hostility, while manual orders
bypass that filter entirely — exactly the reported symptom
("manual works, auto doesn't", "towers don't see it"). CA's independent bug
(§5) is what keeps serving such recipients to the front line, which is why
the symptom presents as "CA Rhinos are ignored".

Residual unknowns (narrow, listed honestly): tower-vs-TRUE-Arabs sample
(never occurred); factory-contact sample (never occurred);
`DiscoveredBy`/cell-threat internals (UNOBSERVABLE WITH CURRENT API —
but nothing in behavior requires them to differ).

## 5. CONTRIBUTING FACTORS (incl. preserved Neutral finding)

- **Bug #2 (preserved, NOT the targeting root cause): CA treats
  Neutral/Special as combat houses** — 7/10 Director pairs `owner=Neutral`;
  enabled by 11 preplaced civilian vehicles (`spawnmap.ini [Units]`) passing
  the `ownUnits>0` gate + silent CP income (kill-split/damage/survival).
- `lastKillPos` front-loading drops the armor next to the player base.
- Fixed cross-faction `REINFORCE_TYPE="HTNK"`.
- `SpawnUnit` log omits the calling house (pos/time correlation required).

## 6. EXCLUDED HYPOTHESES

- **H1 (incomplete unit):** refuted — native mission assignment,
  self-acquire ≤45f, retaliation, manual, death all normal.
- **H2 (missing post-spawn lifecycle):** refuted — same observations; no
  post-spawn call with demonstrated targeting relevance is absent
  (threat/discovery internals unverifiable but behaviorally unnecessary).
- **H4-as-situational-only (range/position/alliance/mission):** refuted —
  distance-matched contrasts (SLAV@1.0 NONE vs LOCKED; LTNK@2.2 NONE vs
  LOCKED@1.4), alliance false everywhere, Guard on both sides.
- **H5 (target_reselect on player side):** refuted — rewrites ONLY
  AI-house attackers (`main.lua:113-118` victim/attacker filters, explicit
  `owner==player` and `is_neutral_house` exclusions); zero lines on probe
  ids in frames 300–420.
- **bounty_hunter / M16 / EventHook target-override:** refuted — inert
  (global-callback mismatch), draw-only native AUTO, hook disabled +
  spawner-ship-only map (empty for HTNK).
- **H6 (HTNK-specific):** unresolvable (all samples HTNK) but unnecessary —
  ownership alone separates all 60+ samples with zero exceptions.
- **API insufficiency for DIAGNOSIS:** refuted — full RCA used existing
  getters only. (For MECHANISM internals — `DiscoveredBy`, cell threat —
  current API is blind by design; see §7C.)

## 7. FIX OPTIONS

**A. Lua-only with existing API (sufficient for both bugs).**
A1 (targeting symptom): stop feeding passive recipients — `NON_COMBATANT =
{Neutral=true, Special=true}` + `isCombatant` guard at 4 points in
`command_authority/main.lua`: `S.cp` seeding, `nearestHostileUnit`
candidacy, fallback kill-split, `directorLoop` (+`retaliation.from`).
~10 lines; real combatants unchanged; old Neutral HTNKs die off; no API.
A2 (diagnostic follow-up, only if a TRUE Arabs-ignored case is ever
produced): extend the (removed) probe to sample nearest WEAPON building +
building HP — existing API only.

**B. C++ fix: not required.** No native defect demonstrated; nothing to fix
natively. Changing `MultiplayPassive` handling or auto-target internals
would alter vanilla rules for all Neutral assets (civilians included) —
disproportionate and out of scope.

**C. No-fix architectural limitation: none identified.** The correct fix
(A1) is fully within current LuaAPI. What current API cannot do is expose
`DiscoveredBy`/threat internals — but the RCA shows they are not needed
for the fix, so no extension is proposed (and none should be added merely
for diagnostic convenience).
