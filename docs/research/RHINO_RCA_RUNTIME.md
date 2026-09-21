# Rhino RCA — Runtime Verification (2026-09-20)

One single runtime RCA run for: "Command Authority spawned Rhinos near my base
are often not auto-attacked by my units/defenses".

- Raw evidence: `docs/research/rhino_rca_session_2026-09-20.log` (1498 lines:
  `[RCA]` probe + `[House] SpawnUnit` + `[AUTH]` + `[M14.1]`, session slice).
- No LuaAPI/C++ extension. No mechanic changes except two authorized diagnostic
  actions (ONE manual `Attack` + release via `Stop`; at most ONE synthetic
  `SpawnUnit` fallback, which FIRED once at frame 10801).
- Mod stack (real, as observed): `command_authority` + `bounty_hunter` +
  `barrel_elevation_diag` (M16) + `target_reselect` + temporary `rhino_rca_diag`
  (appended LAST in load order, REMOVED after the run; `active_mods.txt`
  restored).
- Launch: `injector.exe --attach` + CnCNet-spawn
  (`Syringe.exe -SPAWN ... gamemd-spawn.exe ...`, `spawn.ini` → Dannath
  Revisited, YuriCountry human-slot/AI-driven vs Arabs AI). Match ran
  ~09:30 (frames 1→30900+), killed after data complete.
- Probe: `scripts/mods/rhino_rca_diag/main.lua` (deleted after run; recipe in
  §7). Existing API only (`GetOwner/GetKind/GetTypeName/IsAlliedWith/
  GetPosition/GetDistanceTo/GetId/GetMission/GetTarget/IsAlive/IsIdle/
  IsAttacking/Attack/Stop/House.SpawnUnit/World.GetAllUnits/...`).
  Note: probe's NEAR lines render `attacking=false` as `attacking=?`
  (Lua `false or "?"` formatting quirk) — `mission=` + `target=` disambiguate.

## 1. Decision tree (evidence-backed)

| Node | Verdict | Evidence |
|---|---|---|
| Spawn (CA creates Rhino via `House:SpawnUnit`) | PASS | 8 Director pairs correlate 1:1 by pos+time: `SpawnUnit: created 'HTNK' at actual (X,Y)` ⇔ `[RCA] NEW` same X,Y, e.g. `(52,87),(52,88)` 14:53:59, `(43,89),(43,90)` 14:54:45, `(105,97),(104,96)` 14:55:00, `(45,92),(46,91)` 14:55:04, `(44,92),(45,92)` 14:55:12, `(96,121),(98,121)` 14:55:39, plus late Arabs pairs `(52,104),(53,104)` / `(59,83),(60,82)` / `(58,88),(60,87)`. Call site: `command_authority/main.lua:431` (`force=true, action=""`). |
| Correct type? | PASS | All samples `type=HTNK kind=unit hp=400/400` at sighting (58 NEW total: 44 Arabs incl. factory/preplaced, 14 Neutral). |
| Correct owner? | **FAIL** (7 of 10 Director pairs) | 7 Director pairs `owner=Neutral` (frames 255, 3135, 4335, 4815, 6495, 7455, 7935); 3 pairs `owner=Arabs` (4095, 21855, 22095). Director is meant to reinforce the AI enemy (Arabs), not the civilian house. |
| `alliedWithPlayer == false`? | PASS | `alliedWithPlayer=false` on EVERY sample (58/58). Engine knows they are enemies. |
| Valid position? | PASS | `actual≈requested`, on-map, 1–4 cells from player units/buildings in all near-base samples. |
| Manual `Attack` succeeds? | PASS | `PROBE frame=300 attacker=LTNK#1056178 -> rhino=1056262 attackCall=true targetAfter=HTNK#1056262`. |
| Target assigned? | PASS | Attacker `mission=Attack target=HTNK#1056262 attacking=true` (frame 300→360). |
| `IsAttacking` / damage? | PASS | Rhino `400→270→140` over 2 polls (130/poll from ordered attacker); kills confirmed by player `CP +5 [kill]` at 14:54:02.118 + 14:54:02.837 matching LOST 1056260/1056262. `RELEASE ... stopCall=true` clean. |
| Organic auto-targeting of Neutral Rhino? | **FAIL (slow)** | LTNK@2.2 cells: `Guard/target=NONE/idle` +45f post-spawn. INIT@2.2 cells: `Guard/NONE` +45f, engaged only +105f. Buildings (YACNST/YAREFN/NATBNK): `target=NONE` in 40+ samples across 300–600f windows. Zero weapon-tower-in-range samples (towers built late/far) → tower leg INCONCLUSIVE by sampling, see §4. |
| Control: Arabs-owned Rhino, same spawn call | PASS (fast) | Synth 1058184 (Arabs, base center): `Attack` + hp 395 ≤14f; dead in 135f. Director Arabs pairs 1063307/1063491: `Attack` at first sighting (≤15f), dead in ~105–210f. Player LTNK locks `target=HTNK` ≤45f; buildings register `target=HTNK` ≤45f (YAREFN lines). |
| Reverse: spawned Rhino targets player? | PASS | Every near-base Rhino self-acquires ≤45f: `target=YACNST/YAREFN/CAOILD`; 1056262 `Attack vs LTNK#1056175`; 1056689 `Attack vs LTNK#1056178`. Spawned-unit targeting works regardless of owner. |

## 2. FACTS

1. CA Director spends for the **Neutral** house (7/10 reinforcement pairs).
2. Player-side acquisition is ownership-asymmetric: Arabs-owned → target lock +
   building threat registration + lethal damage within ~15–45f; Neutral-owned →
   `NONE` at +45f at 1–4 cells, engagement after ~105–600f, survival 300–600f+
   (e.g. 1056689 alive 585f ending hp=4; three Neutral pairs reached DONE alive).
3. Manual `Attack` vs Neutral Rhino always accepted; damage/kill pipeline normal.
4. Neutral Rhinos self-acquire player assets fast (≤45f) and fight back.
5. Player kill income is partly funded BY the bug (probe kills credited via
   nearest-hostile: `CP +5 [kill]` 14:54:02.118/.837).

## 3. ROOT CAUSE

**Command Authority treats every non-human house as a Director client —
including the civilian `Neutral` (and `Special`) houses.**
`S.cp` seeded from all houses (`main.lua:611-618`); `directorLoop` acts on all
`!isHumanHouse` (`:583-592`); kill fallback-split/damage/survival income credit
non-combatants; `nearestHostileUnit` accepts civilian cars as kill earners.
Neutral qualifies for `powerReinforce` because the map preplaces 11 Neutral
civilian vehicles (`spawnmap.ini [Units]`, `CAR/PTRUCK/...` in Guard) and
`ownUnits()` counts any mobile. Spawns land `at the front` (Neutral's
`lastKillPos`, i.e. next to the player's base) and are Neutral-owned. The
engine's player-side auto-acquire deprioritizes Neutral-owned threats
(proven contrast §1) while manual orders work — hence "stands in my base,
nobody shoots it".

First-spawn funding detail (frame ~241 think, requested `(54,89)` ≈
YACNST@55,90 site, `"at the front"` ⇒ killer-branch credit): exact source is
INCONCLUSIVE from silent economy; strongest hypothesis is MCV-deploy
false-death (mobile→building transition) with a Neutral civilian car as
nearest hostile. Does not affect the fix.

## 4. CONTRIBUTING / INCONCLUSIVE

- `powerReinforce` drops armor at `lastKillPos` (the front) — maximizes exposure.
- Fixed `REINFORCE_TYPE="HTNK"` for all factions (cosmetic).
- `SpawnUnit` log omits the calling house (correlation by pos/time only).
- Tower leg: no weapon-tower-in-range sample occurred (Gattlings built late at
  57,82/57,96/60,89, spawn zone 43–49,89–95 out of range; probe logged only the
  NEAREST building). Mobile + building-threat contrast still proves the
  acquisition asymmetry through the same pipeline towers use.
- Exact killers of some final blows (high-HP LOST: 1056691@232, 1057096@307):
  INCONCLUSIVE (60f poll granularity), irrelevant to the tree.

## 5. NOT THE CAUSE (excluded by evidence)

- `House:SpawnUnit` mechanics (valid units, type/kind/mission/HP all normal).
- Alliance (`alliedWithPlayer=false` everywhere).
- Position/range (in-range contrast samples).
- Initial `Guard` mission (Arabs pairs spawn Guard/Area-Guard too, engaged fast).
- `bounty_hunter`: INERT — engine collects (not dispatches) the **global**
  `OnPreDamage` reference (`lua_engine.cpp:1012`) but never invokes it;
  mod defines table method; `Update` no-ops on nil.
  [Current-status note: "dispatches" above is imprecise — collection only,
  no invocation. Preserved as the run record.]
- M16 barrel diag: draw-only native AUTO (`barrel_pitch.cpp` draw detour) + log
  heartbeat; no targeting writes (only `[BarrelPitch] draw` lines on rhinos).
- `target_reselect`: never touches player-owned or Neutral-owned units
  (`main.lua:115,117,131,161` + `util.is_neutral_house`); zero reselection
  lines on probe ids during frames 300–420 (only unrelated `SMIN`
  `VICTIM_LOST`).
- No API extension needed: entire RCA used existing bindings.

## 6. FIX (proposed, NOT applied)

Minimal Lua-only fix in `scripts/mods/command_authority/main.lua`:

```lua
local NON_COMBATANT = { Neutral = true, Special = true }
local function isCombatant(name) return name ~= nil and not NON_COMBATANT[name] end
```

Apply at 4 points: (1) `S.cp` seeding loop — skip non-combatants (or keep
storage but skip everywhere below); (2) `nearestHostileUnit` — skip
non-combatant candidates (parked civilian cars must not "earn" kills);
(3) fallback kill-split loop — skip non-combatants; (4) `directorLoop` (+
`retaliation.from`) — never spend for, or retaliate as, non-combatants.
Survival income for skipped houses goes away with (1). ~10 lines, no API
changes, no mechanic change for real combatants. Already-spawned Neutral
HTNKs in old saves die off naturally; no migration.

Incidental static observation (OUT OF SCOPE, runtime-UNVERIFIED, NOT fixed):
`powerRepair` calls `SetHealthRatio(1.0)` but the binding computes
`ratio/100` (`bindings_techno.cpp:1127-1131`), i.e. 1% HP — contradicts
`API.md` (`1.00 = 100%`). Separate issue, needs its own verification.

## 7. Reproduction recipe (temporary probe, removed after run)

Files were `scripts/mods/rhino_rca_diag/main.lua` + `mod.json`
(appended last in `active_mods.txt`): scan `World.GetAllUnits()` every 15f for
new alive `HTNK` ids → snapshot
(id/type/kind/owner/alliance/pos/mission/idle/attacking/target/hp) via `print`;
poll every 60f + nearest player mobile/building; ONE `Attack` probe on first
in-range pair + `Stop` release after 3 polls; at most ONE synthetic
`aiHouse:SpawnUnit("HTNK",1,px,py,0,true,"")` after 10800f with no in-range
Rhino. Syntax-check: `buildlua_check.exe -e
"assert(loadfile('scripts/mods/rhino_rca_diag/main.lua'))"`.
