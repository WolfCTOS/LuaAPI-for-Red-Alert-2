# SmartAI M1 — Access Parity with Vanilla AI

> **Goal:** SmartAI gets access on par with Vanilla AI. No decision-making
> upgrades, no new tactics, no balance fixes in M1.
> **Status:** AUDIT DONE — implementation NOT started (waiting for scope go-ahead).
> **Rules:** no Vanilla AI changes; no LuaAPI Beta roadmap changes; no new LuaAPI
> milestones from mod problems (per `AGENTS.md` Milestone Architecture — a proven
> API gap may *later* motivate a separate LuaAPI Beta item, never automatically).
> Per-part: Release build + existing tests + minimal new tests; runtime-only
> things get a real in-game check, never code-presence claims.

## M1 capability-gap table (audit 2026-09-23, read-only)

Evidence: `scripts/mods/smart_ai/main.lua` (binding inventory below),
`third_party/YRpp` headers, `API.md`, `FSM/QUEUEUNIT_GATE.md` (QueueUnit
BLOCKED), `FSM/DYNAMIC_UNIT_BEHAVIOR.md` (order persistence limits),
`PROJECT/RUNTIME_BOUNDARY.md`. Zero C++ references to
Team/TaskForce/Script/AITrigger/Production bindings (grep-verified).

| # | Vanilla mechanism (evidence) | SmartAI access today | Gap? | M1? |
|---|---|---|---|---|
| 1 | Attack waves: TeamClass/TaskForce/ScriptTypes/AITriggers (`YRpp/TeamClass.h:45,74`; arrays `0xA8E8D0u`) | NONE (no bindings, no hooks) | **GAP (observe + command)** | **YES — read-only team visibility first** |
| 2 | Production/build decisions (factory queues, build lists) | NONE usable (`AI.QueueUnit` BLOCKED live; `AI.CountUnit` exists, unused, live-UNVERIFIED) | **GAP (blocked)** | Observe-only: live-verify `CountUnit`; writes stay out |
| 3 | Order primitives (Move/Attack/Guard/Hunt/…) | HAS (`MoveTo/Attack/Hunt`; `Stop/Scatter/Deploy/Unload/Return` exist, unused) | No access gap; **lease problem = DU research** | NO (leases are DU, not M1) |
| 4 | Targeting (SetTarget/auto-acquire) | Read `GetTarget` ✓ (diag), write `Attack` ✓ | No access gap; persistence = DU | NO |
| 5 | Aircraft/navy missions | Primitives apply to all FootClass ✓ | No access gap; tactics = not M1 | NO |
| 6 | Base defense teams, sentry auto-fire | Observes HP + orders ✓ | No access gap | NO |
| 7 | Harvest economy (refineries/miners) | `HarvestAt/GetHarvestLocation` exist, unused | Minor gap | MAYBE (verify live first) |
| 8 | Intel: shroud/radar/sensors | NONE (no bindings) | GAP (observe) | CANDIDATE (read-only, determinism caution) |
| 9 | Superweapons | NONE | GAP | NO (separate capability, defer) |
| 10 | Repair/sell/building control, transports load, MCV micro | NONE (only `Undeploy`/unit `Deploy`) | Minor gaps | NO (defer past M1) |
| 11 | Difficulty/IQ (`HouseClass:840 IQLevel`) | NONE (read) | Minor gap (observe) | CANDIDATE (read-only) |

SmartAI binding inventory today (24): Attack, GetBuildings/ByIndex/Count,
GetCost, GetHealth/MaxHealth, GetId/Kind/Name/Owner/Player/Position/TypeName/Units,
GetTarget (diag only), Hunt, IsAlive/AlliedWith/Attacking/Human/Idle, MoveTo,
PrintMessage.

## Proposed M1 scope (needs go-ahead — NOT started)

- **M1-A:** read-only Team/TaskForce/Script visibility bindings (enumerate AI
  teams: members, mission, target house). Pure observation, CnCNet-safe.
  Unlocks coordination + attribution later. Needs design + Release build +
  harness + live check.
- **M1-B:** live-verify `AI.CountUnit` read-back (no behavior change); document.
- **M1-C:** order-lease findings → feed DU-1 research (research item, no code).
- **M1-D (maybe):** harvest-primitive live check; shroud/IQ read-only candidates.

## Explicitly NOT M1

Tactics, balance, decision-making, formation API (idea-only), superweapon
control, production WRITES, Vanilla AI changes, Ares/Phobos duplication
(`FSM/DYNAMIC_UNIT_BEHAVIOR.md` boundary), roadmap changes, new LuaAPI
milestones from mod problems.

## Progress log

- 2026-09-23: M1 declared; read-only access audit done (table above);
  `CHANGELOG.md` + this file created. No implementation. No behavior changes.
- 2026-09-23: **M1-A implemented** — `World.GetAITeams()` (read-only vanilla
  team visibility) in `src/bindings_techno.cpp` (existing TU: no cmake on
  this machine, so no new TU file — split later when cmake is available).
  Plain-data tables, SEH-guarded, C2712-clean. Registered as `World.GetAITeams`.
  Release build OK via MSBuild (only pre-existing C4731 warnings).
  Documented in `API.md`. Design deviation: single-file placement forced by
  missing cmake (re-configure impossible); logic unchanged by a future split.
- 2026-09-23: **probe mod** `scripts/mods/teams_probe/` (read-only, logs only,
  inactive by default) + harness `tools/tmp/teams_probe_test.lua` **5/5**.
  Covers M1-A fields, M1-B `AI.CountUnit` read, harvest-anchor read,
  missing-binding degradation.
- 2026-09-23: existing suites re-run green: officer 50/50, capture 28/28.
  No SmartAI behavior changes (no SmartAI edits in M1-A).
- 2026-09-23: **live verification PENDING (blocked autonomously).**
  Attempts: Syringe spawn → game AV (C0000005) without client context;
  direct `gamemd-spawn.exe -SPAWN` → silent exit ~8s; plain `gamemd.exe` →
  exit <25s. Environment cannot boot the game unattended. `active_mods.txt`
  was temporarily extended and **restored** (verified via git diff).
  Protocol for user-run match: append `teams_probe` to
  `scripts/active_mods.txt`, play ≥7 min vs AI, look for `[TEAMSPROBE]
  frame=… teams=N` + team lines in `LuaAPI.log`, then remove the line.
- 2026-09-23 (user-run full match, medium bot, ~21 min, frames to 30600):
  **M1-A LIVE VERIFIED** — `teams=0` early, 1–3 teams mid/late game; owner
  resolves (`Russians`); script missions advance (0→6); full/under flags
  move; members counts live. **M1-B VERIFIED** — `queued house=Russians
  type=HARV n=1` (read-back, no error). **Harvest-read VERIFIED** — anchor
  returns coords table (probe now unpacks `x,y`; `nil` when no anchor).
  **BUG FOUND LIVE: team `id` read 0xFFFFFFFF for every team** —
  TeamClass instances are never assigned engine UniqueIDs. Fixed: field
  replaced by scan-local `index` (+ `creationFrame` in probe output);
  tracking across scans = teamtype + creationFrame + member set (`API.md`
  updated). Rebuild OK; probe/officer/capture suites green (5/5, 50/50,
  28/28). Fixed shape is code-evident; live re-check rides on the next
  natural match.
- Observation (not a defect): skirmish teamtype IDs are generated
  (`05C643BC-G` style), `targetHouse` stays nil until the engine assigns
  one, `scriptMission=-1` = no active mission. No errors, no
  `GetAITeams unavailable` in the session.
- 2026-09-23: **M1-C research done (spec only, zero runtime changes).**
  `milestones/M1C_ORDER_LEASE_RESEARCH.md` created: vanilla order lifecycle
  (missions/teams/re-issue), SmartAI 8-site lifecycle, 6 order sources,
  5 evidence-graded overwrite cases (P1–P5, incl. benign kill-reacquire
  counter-case), unknowns/gaps, DU-1 lease contract proposal, future
  API/hook candidates (no commitments), out-of-scope list, and one
  proposed "divergence-with-cause" experiment (proposal only).
  DU-1 NOT implemented. No tactics, no re-issue logic, no behavior changes.
- Temp Experiment B fully removed (probe dir + test + native block + lua_engine
  registration; grep-clean); active_mods restored to the canonical trio.
- Visible surrender: new `unit:Sell()` binding (native virtual dispatch, no
  address; buildings only; API.md documented) + sell-all pass on latch with
  SURRENDER_SELL marker, before the Lose call. Suites: officer 77/77,
  capture 28/28. NOTE: Sell is new public API surface driven by a real
  consumer (surrender) — Beta-milestone designation left for explicit
  decision, not created here. Live verification pending (expect visible
  base collapse on surrender).
- Missing-splash diagnostic step: temp `__Phase2Read` accepts house index;
  probe logs PLAYER snapshots (BEFORE + each offset) to test whether
  IsWinner flips on the player while the AI counts down. Suites green
  (phase2 14/14, officer 75/75, capture 28/28); build OK. Awaiting
  user-run match. No production changes (spike block only).
- Phase 2.1 prep: probe AFTER schedule tightened to +1/+30/+60/+90 (dense
  post-call timeline); suites green (phase2 12/12, officer 75/75, capture
  28/28); build OK. Runtime pending user-run match. No production changes.
- Phase 2 integration implemented (production, SmartAI-only): internal
  `Engine.__SmartAILose` bridge (`src/bindings_house.cpp`, NOT public API,
  no API.md entry) — validates house, refuses when already IsLoser,
  otherwise calls Lose(false) once in SEH; Lua fires it once per latch
  with LOSE_FALSE_CALLED marker; missing binding degrades gracefully.
  Suites: officer 75/75 (one-shot/reset/marker), capture 28/28 (also
  covers missing-binding path), probe 5/5. Live verification pending
  (user-run match; crash risk from Experiment B noted in run protocol).
  **SUPERSEDED 2026-09-26 — the bridge is no longer called by the mod.**
  Multi-house live runs showed that forcing `Lose(false)` on a non-player
  house makes the engine leave the game main loop ~90 frames later and end
  the whole match/process with every other house alive (3/3). Control-flow
  counters prove `Lose()` returns and the detour is then never entered
  again (`HOOK_NOT_ENTERED`). Default is now
  `SmartAI.SURRENDER_ENGINE_CALL = false`; the bridge itself is unchanged
  and still compiles. Evidence: `FSM/HOUSE_LOSE_FORCED.md`,
  `FSM/VERIFICATION.md`. History above is preserved as written.
  **Consequence for the notes below:** every "match ended / log stops"
  observation in the 1v1 Experiment B runs now reads differently — the
  early end was never a crash and never an ambiguous silence; it was this
  forced defeat transition leaving the game loop.
- Experiment B Run B (auto-fire, no F9): IsLoser=1 + BorrowedTime 90→60,
  match ended, Syringe exit code 0, user-confirmed surrender. Prior "crash"
  reclassified (no crash evidence in either run; silence = game over, not
  death). Defeated=1 never observed; no dump (registry never applied).
  Verdict: PASS WITH FINDINGS. Lose(true)/FlagToDie untouched.
- Experiment B result (user-run 19:19, Lose(false) on Russians at frame 900):
  BEFORE all-zero → call clean (no SEH) → AFTER+0 IsLoser=1, BorrowedTime=90,
  assets intact, Defeated stayed 0 → AFTER+1/+30 same with BorrowedTime
  89→60 → log STOPS ~1 s later (no AFTER+300/+900). Process dead, no dump
  captured (LocalDumps covers gamemd-spawn.exe only), no sankey quit reason:
  classified CRASH correlated with the test (timing), causation formally
  UNPROVEN. Per stop-rule: NO further candidates. Lose(false) NOT marked
  safe. Next: enable LocalDumps for gamemd.exe + forensics, then decide on
  repeat vs analysis. Temp probe + native block stay until verdict (marked
  PHASE2-SPIKE-REMOVE-ME).
- Experiment B built (NOT performed): temp `Engine.__Phase2Fire/__Phase2Read`
  + `phase2_probe/` mod (F9 trigger, once per process, inactive by default)
  + 8/8 harness; Release build clean; SmartAI untouched; officer 70/70.
  Signatures re-verified (Lose 0x4FCBD0 / FlagToDie 0x4FC980 / AcceptDefeat
  0x4FC0B0; BorrowedTime readable). Runtime + cleanup pending user-run match.
  Report: `SURRENDER_PHASE2_EXPERIMENT_B.md`. Status INCONCLUSIVE by design.
- Phase 2 safety spike done (research only):
  `milestones/SURRENDER_PHASE2_SAFETY_SPIKE.md`. Lose/FlagToDie/Win/Accept
  mapped (addresses+signatures PROVEN, all semantics/side effects/MP
  UNVERIFIED); Win excluded; recommended path = Experiment A (TakeDamage
  natural defeat, zero new code) then B (temp hook, described not built);
  integration verdict NOT YET. No runtime performed, nothing implemented.
- Surrender Phase 1 LIVE VERIFIED (user-run 16:34–16:45, Russians/Soviet):
  anchors present f30600 → Barracks gone by f32400 → WF+MCV gone by f33990
  → exactly one SURRENDER_DETECTED (0/0/0) → zero new Russians orders after
  (max ORDER frame 32850 < latch 33990) → diagnostics continued
  (CENSUS/DIVERGENCE/BASE) → game continued (frames to 39600+, user quit,
  no game-over by design) → no Lua errors, no crash. 11/11 criteria PASS.
- Surrender Phase 1 implemented (detection + latch + order silence, no
  engine calls): anchor tables (live-verified subset + marked-unverified
  rest, fail-safe inclusive), per-house latch permanent till restart
  (officerReset), all 5 order layers gated, one SURRENDER_DETECTED line.
  Officer 70/70 (T24 A–I incl. latch-once, silence, power/credits
  exclusion, reset recovery), capture 28/28 (A + AM2 MCV fixtures).
  No Lose(), no bindings, no tactics/balance changes.
- Surrender DoD agreed (SmartAI contract, not vanilla claim): no Barracks +
  no WF + no MCV/CY → SmartAI surrender. Implementation plan (not impl):
  `milestones/SURRENDER_IMPLEMENTATION_PLAN.md` — Lose()/FlagToDie()
  mapped but unproven from Lua (no binding created); soft-surrender
  (order silence + latch) needs zero new API; anchor IDs must be verified
  from live BASE lines; test plan A–F defined, not implemented.
- Surrender research got live evidence (user-run base kill): 34→11 buildings,
  power→0, production gone, game continued with 11 defenses/tech/supers +
  2 units and 1 live team. Production/power confirmed non-defeat-inputs
  live; H-state never reached so the exact trigger stays open.
- `[SMARTAI][BASE]` diagnostic shipped (read-only): per-AI-house buildings
  with HP + unit count + power + credits, same 1800f gate as census, capped.
  Officer 55/55 (T23), capture 28/28. Answers base-state questions from logs
  without new bindings. Defeat flags themselves still unreadable (missing
  binding, recorded).
- Lua upvalue bug fixed (diagnostic-only impact): `divLogged` /
  `lastCensusFrame` were declared AFTER `officerReset`, so restarts wrote
  globals and the locals never cleared (census cadence drifted; stale
  divergence suppression could leak across matches). Moved above the reset.
  No gameplay behavior change (both are diagnostics-only state).
- Surrender Conditions research done (read-only):
  `milestones/SURRENDER_CONDITIONS_RESEARCH.md`. Defeat = per-house flags
  (`Defeated/IsLoser/Lose()/FlagToDie()`, YRpp-mapped); evaluator unmapped;
  power/production are NOT defeat inputs (signatures); production loss ≠
  surrender (hypothesis on proven premises); edge cases A–M need live test;
  `Defeated`-flag read recorded as missing binding (not implemented).
- Live session 23:26–23:45: 82 SmartAI ORDERs (10 defense w/ 10/10 read-back,
  58 march-recall, 8 idle-recall, 6 rally), 11 divergences (2 NATURAL,
  9 overwrite-candidates, reissue=none 11/11), 0 target_reselect lines,
  0 errors. Cumulative reissue=none: 24/24 across two sessions.
- **ZEP anomaly RESOLVED (was §c).** Bounty `MarkBounty` requires
  `WhatAmI()==Unit` natively (`bindings_techno.cpp:1287-1291`) and fired
  WANTED on ZEP#1070902; reward $3000 = 2000×1.5 rookie = Kirov price;
  census kind=unit (mobile: 99,144→95,114); CLAIMED + payout completed.
  Verdict: Kirov (ZEP) is **UnitClass-kind in YR 1.001** — engine ground
  truth, not a binding bug (PDPLANE reads `aircraft` correctly on the same
  path). Past ZEP recalls/defenses reclassified as normal unit-kind
  handling. Kirov escort remains a LIMITATION (escort table is V3-only).
- Decision 2026-09-23 (post 22:56 session): (b) accumulate session analyses
  toward DU-1, no new diagnostics for now; (c) Kirov↔ZEP visual correlation
  on the next game if Kirovs appear; (a) mission-at-recall telemetry only
  if stuck recalls repeat AND classification becomes impossible without it.
- Live session 22:56–23:09 (user-run, Germans vs Russians): 27 POINT_DEFENSE
  orders (27/27 read-back match, 0 mismatch), 13 TARGET_DIVERGENCE (all
  reissue=none), 11 ESCORT + 19 MARCH_RECALL orders, 0 guard/garrison/rally/
  idle-recall lines, 0 target_reselect lines, 0 Lua errors.
  Classified: E1→E1 flips = NATURAL (kill-reacquire); APOC→live CMIN and
  ZEP→live FV = OVERWRITE CANDIDATEs; dead-actual cases (mission=Attack) =
  stale-target race (ENGINE/AI BEHAVIOR, medium).
  New findings: (a) recalled Rhinos (e.g. 1067285) static at one cell across
  90 s despite repeated accepted MARCH_RECALLs — orders not taking effect,
  mechanism UNKNOWN (needs mission-at-recall + dest diagnostics);
  (b) ZEP passes `kind=="unit"` gates for the 3rd session while PDPLANE
  reads `aircraft` correctly — kind anomaly stands, needs visual
  correlation (user: match Kirov sightings to ZEP log lines);
  (c) enemy CLEG (Chrono) appeared only as a SmartAI *target*, never actor.
- Divergence-with-cause diagnostic shipped (read-only): `TARGET_DIVERGENCE_CTX`
  (unit/owner/type, commanded + expAlive, actual + actAlive/actType/actKind/
  actOwner, `GetMission`, SmartAI re-issue audit across guard/escort/recall/
  rally memories). No new orders, no retries, no lease. Harness: officer
  53/53 (incl. T22 natural-cause proof), capture 28/28. DU-1 stays SPEC ONLY.
