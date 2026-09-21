# FSM — LuaAPI Capability Map for Global Gameplay

Every entry names the existing API, its location, what it enables, where it
was actually used, its limits, and multiplayer/determinism notes where
known. Nothing here extends the API. Claims are graded:
**FSM-VERIFIED** (used in CA/RCA/harness with evidence),
**REPO-VERIFIED** (other experiments' records, cited not re-proven),
**UNEXPLORED** (exists in source, no serious use found),
**UNKNOWN** (cannot be confirmed from current API/repo).

> Conflict note (reported, not resolved): `PROJECT/ROADMAP.md` marks
> Milestone 4 (`OnPreDamage`, damage-modification pipeline,
> `shield_overload` validation) as done, and `API.md` /
> `PROJECT/CAPABILITIES.md` describe a live damage-interception contract.
> Source inspection (2026-09-20) finds **no invocation site**: the engine
> collects the *global* `OnPreDamage` reference every frame
> (`src/lua_engine.cpp:994-1026`) but never calls it with damage
> arguments, and no `ReceiveDamage` hook is installed (hook inventory:
> MainLoop, LoadString, Bullet-Detonate, GetPrimaryWeapon, DrawAsVXL;
> ActiveClickWith disabled). Until this is resolved, live incoming-damage
> interception is graded UNKNOWN, not VERIFIED — regardless of older doc
> language. Related staleness: `PROJECT/CAPABILITIES.md` recipes use
> mod-table `OnPreDamage` + `game_RegisterEvent` (no such binding exists)
> and `Engine.PrintMessage(msg, colorIndex)` (current binding takes text
> only). FSM code uses the global-callback / text-only forms.

## FSM-VERIFIED (Command Authority / RCA probes / harnesses)

### World unit queries
- API: `World.GetUnits/GetAllUnits/GetBuildings/GetUnitsInRadius`
  (`src/bindings_techno.cpp:1772-1782`; `World.GetAircraft/GetWaypoint`
  live in the same table but were NOT FSM-exercised).
- Enables: whole-map scans, radius scans, building rosters.
- Used in: CA `combatScan/ownUnits/directiveStart/powerSabotage`;
  RCA tracking; frontline foe scans. Harnesses stub all of them.
- Limits: snapshot semantics — never retain userdata across frames,
  re-scan and re-validate (`IsAlive`) per use; radius in cells.
- MP: engine-array order is treated as stable/deterministic (project
  decision record); ID-sorting used where order-independence is required
  (placement fix).

### Owner / alliance / identity
- API: `unit:GetOwner` (registry-cached house userdata, so `==` works),
  `house:IsAlliedWith/IsHuman/GetName`, `House.GetPlayer/GetCount/GetByIndex`
  (`src/bindings_techno.cpp:1458`, `src/bindings_house.cpp:399-407`).
- Enables: attribution, diplomacy checks, house iteration.
- Used in: all CA economy/director logic; RCA owner snapshots.
- Limits: cached userdata cleared per session; exact house names
  (`Neutral`/`Special`) matched literally. Alliance is engine truth, not
  mod bookkeeping.

### Position / distance
- API: `unit:GetPosition` → `{x,y,z}` cells (256 leptons),
  `unit:GetDistanceTo` (`bindings_techno.cpp:1459,1461`).
- Enables: kill-site recording, spawn points, displacement math.
- Used in: `S.seen` snapshots, `lastKillPos`, frontline formula.
- Limits: cell granularity. MP: integer cell math preferred; float
  ops kept IEEE-double pure (`floor`/`sqrt` only).

### Health / cost
- API: `unit:GetHealth/GetMaxHealth/GetCost`
  (`bindings_techno.cpp:1447,1448,1455`).
- Enables: damage attribution, directive target pick (max HP), repair
  triage.
- Used in: CA combat scan, directives, powerRepair.
- Limits: plain integers; no per-warhead breakdown.

### Post-hoc damage observation (NOT interception)
- API: none dedicated — HP-drop polling between scans (CA `combatScan`).
- Enables: damage bank (+1 CP / 400), kill confirms.
- Used in: CA economy; live `CP +1 [dmg]` lines.
- Limits: frame-delayed; unattributed without a geometry heuristic
  (nearest-hostile); lethal blows reveal no source. This is observation
  of aftermath, not a damage event.

### Target / mission observation
- API: `unit:GetTarget/GetMission/IsIdle/IsAttacking`
  (`bindings_techno.cpp:1472-1474,1486`).
- Enables: acquisition probes, state snapshots (works for buildings too).
- Used in: RCA probe (manual-lock vs auto-acquire contrast).
- Limits: transient native fields; `GetMission`/`Attack` exist in source
  but are missing from `API.md` (doc gap, not API gap).

### Explicit orders
- API: `unit:Attack/MoveTo/Hunt/Stop/Scatter/Unload`
  (`bindings_techno.cpp:1464-1471`).
- Enables: diagnostic attacks, Hunt-on-spawn, order release.
- Used in: RCA probe (`Attack` → damage → `Stop` release); `SpawnUnit`
  `action="hunt"`.
- Limits: `Attack` needs a Foot object (buildings fail); orders route via
  `SetTarget`+`QueueMission` (ActiveClick hook stays disabled).
- MP: orders are sim writes — CA locks all powers with 2+ humans.

### Runtime spawning
- API: `house:SpawnUnit(typeId,count,x,y,facing,force,action)`
  (`src/bindings_house.cpp:304-396`).
- Enables: reinforcements, probes, synthetic control samples.
- Used in: CA `powerReinforce`; RCA synth sample (same args as CA).
- Limits: `force=true` tries the requested cell unchecked, then a
  terrain-only spiral (r=3, no tactical awareness); returns a count, not
  handles (re-find by scan); OOB/unlimbo-fail skips safely without charge;
  Unlimbo-fail leaks a limbo object (inspection note).

### House economy state
- API: `house:GetCredits/SetCredits/AddCredits/GetPowerOutput/GetPowerDrain`
  (`bindings_house.cpp:399-403`).
- Enables: engine-credit effects.
- Used in: NOT used by CA (it keeps a Lua-side CP ledger by design, so
  command points never touch engine credits) — graded REPO-VERIFIED at
  best, via `PROJECT/CAPABILITIES.md` Case Study 2, whose recipe is
  partially stale (see conflict note).
- Limits: `SetCredits` works via transaction delta.

### Lua-side match state (CP-like ledgers)
- API: none needed — plain tables keyed by house name.
- Enables: the entire CA economy/director/retialiation state.
- Used in: CA `S.*`; harness-readable via `AUTH._S` (read-only).
- Limits: must reset on match restart (backwards-frame detection);
  savegame loads don't fire `OnScenarioStart` (lifecycle caveat from
  `PROJECT/CAPABILITIES.md` case studies).

### Frame timers / cadences
- API: `Update(frame)` + `%` cadence + frame-stamped state
  (`scripts/framework/timer.lua` also exists).
- Enables: scan rates, think cycles, warn windows, directive expiry.
- Used in: everything CA does.
- MP: logical frames only — never `os.time`/`os.clock` for decisions.

### Disappearance-based death detection
- API: ID-keyed snapshots diffed across scans (CA `S.seen`).
- Enables: kill attribution without a death event.
- Used in: CA combat scan; RCA LOST tracking.
- Limits: disappearance ≠ proven death — a mobile→building transition
  (e.g. MCV deploy) reads as a death (RCA hypothesis for early Neutral
  funding; unconfirmed detail). Save/load and limbo edges untested.

### Deterministic geometry
- API: ID-sorted traversal + integer/floor/sqrt math.
- Enables: reproducible spawn-point selection.
- Used in: frontline validation (30 harness checks incl. exact
  precomputed coordinates).
- Limits: float discipline required (IEEE-double ops only).

### Runtime AI decisions (Director)
- API: composition of the above; no dedicated AI namespace used by CA
  (`AI.QueueUnit/CountUnit` exist but CA doesn't use them).
- Enables: repair/reinforce/retaliation under the same rules as the player.
- Used in: CA `directorLoop` (4 s cadence), live `DIRECTOR:` lines.
- Limits: needs `ownUnits>0`; spend-gated; announcement-labeled.

### EMP-style disable
- API: `unit:Disable(frames)` (`bindings_techno.cpp:1488`; buildings lose
  power + `DisableStuff`, mobiles paralyze).
- Enables: sabotage / retaliation.
- Used in: CA powers; harness `disabledAt` asserts; live SABOTAGE lines.
- Limits: duration in logical frames.

### HUD + input
- API: `Engine.PrintMessage(text)` (text only),
  `Input.WasKeyPressed(vk)` (`bindings_techno.cpp:1791`+).
- Enables: feeds, warnings, Z/X/C/V/T powers, T-status.
- Used in: all CA messaging/input.
- Limits: local readout; per-frame edge semantics; headless has no keys.

## REPO-VERIFIED (other experiments' records, cited)

- Sub-turret API (`AddSubTurret/SetSplitTargets/FireSplitSalvo/...`):
  battleship showcase + Gate 10 logs. Not FSM-exercised.
- `unit:TakeDamage(amount, [warhead])` through the native pipeline with a
  fallback warhead chain (`bindings_techno.cpp:337-393`). Binding-level;
  note the binding comment: stock `Fire` is ~useless vs heavy armor.
- `unit:SetAmmo/GetAmmo`, veterancy, speed, deploy family, aircraft
  family, `AttachParticleSystem`, `HarvestAt`, `FireProjectile`,
  `World.GetAircraft/GetWaypoint`, `Game.GetDebugHudText`,
  `OnScenarioStart/OnUnitDestroyed` globals, `OnDebugCommand`:
  implemented in source; live-game depth varies per case study —
  check the specific record before depending on it.
- Framework modules (`scripts/framework/`: bus, timer, query, task,
  unit_controller, combat_state, force_group, persist, tactical, util):
  M14 status is "framework logic verified, in-game runtime pending"
  — do not present as live-proven.
- M16 barrel-pitch Engine API (draw-only AUTO): verified per changelog;
  simulation-neutral by design.

## UNEXPLORED (exists, no serious use found)

- `AI.QueueUnit/CountUnit` (`src/bindings_production.cpp`): factory-queue
  production control. Implemented; no mod usage found. Natural fit for a
  future "war factory director" experiment.
- `unit:IronCurtain(frames)` (`bindings_techno.cpp:1425`): native full
  invulnerability + tint via `ObjectClass::IronCurtain`. Implemented;
  live gameplay use unverified. (Different mechanic from the selective
  Iron Curtain concept — see `FSM/IRON_CURTAIN.md`.)
- `SetHealthRatio` percent-scale behavior (binding divides by 100;
  contradicts `API.md`'s 0.35-style examples): usable with 0–100 scale,
  but the CA `1.0`→1% incident shows the trap. Open separate issue,
  deliberately unfixed.

## UNEXPLORED (exists, no serious use found)

- `AI.QueueUnit/CountUnit` (`src/bindings_production.cpp`): factory-queue
  production control (`DemandProduction`/`CountTotal` over
  `FactoryClass::Array`). Implemented; no mod usage found anywhere in the
  tree. Natural fit for a future "war factory director" experiment, and
  the closest existing primitive to production-complete awareness
  (queue counts — still not completion callbacks).
- `unit:IronCurtain(frames)` (`src/bindings_techno.cpp:1425`): native
  full invulnerability + tint. Implemented; live gameplay use
  unverified. See `FSM/IRON_CURTAIN.md` for why it is not the selective
  concept.
- `game.*` legacy namespace (`GetWaypoint/GetUnitsInRadius` mirrors):
  present, unused; prefer `World.*`.

## UNKNOWN (cannot confirm from current API/repo)

- Live incoming-damage interception (`OnPreDamage` with damage args):
  no invocation site in source — see conflict note above. Decisive for
  any reactive-armor concept.
- Psychic/warhead identification of incoming damage: no payload reaches
  Lua at all.
- `DiscoveredBy`/cell-threat internals behind auto-acquire timing.
- Superweapon state/activation API (none in bindings).
- Production-complete events (factories expose queue counts, not
  completion callbacks).
- Lua-state restore across savegame loads (`persist.lua` exists;
  live behavior unverified).
- Two-client MP behavior of any writing mod (CA gates powers; the gate
  itself is harness-only).
