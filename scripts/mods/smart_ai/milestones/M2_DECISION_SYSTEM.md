# SmartAI M2 — Decision System (mod-owned milestone, NOT LuaAPI namespace)

> Architecture: per `AGENTS.md` + `PROJECT/MILESTONE_NAMESPACE.md` §7 —
> SmartAI history is independent. This file is SmartAI's own M2.
> No LuaAPI Beta milestone is created here. A Beta item appears only if M2
> proves a missing API capability (proven gap → separate `Beta My`).
> No new Gates created: work below is organized as checks inside M2.

## 1. Outcome (single sentence)

SmartAI stops being a reaction system (`if breach → rally`) and becomes a
decision system: on every pulse it compares options by live-scored utility,
commits to one, and reassesses — using memory Ares/Phobos cannot express.

## 2. Why this is the right milestone now

M1 gave access parity (audit + `World.GetAITeams` read-only + surrender
Phase 1 + divergence-with-cause diagnostics). The engine can be observed.
What is missing is **choice**: today every layer fires its own trigger
independently (defense claims → escort drafts → rally spams → recall pulls),
with coordination only as skip-lists (`isOfficerAssigned`). There is no
place where SmartAI asks "which of these 3 things is best right now?".

## 3. Reaction vs decision (contract)

| Reaction (M1 and before) | Decision (M2) |
|---|---|
| `if condition → order` per layer | `sense → belief → options → score → commit → reassess` per force |
| No comparison between alternatives | 2+ options scored on one scale, winner committed |
| No memory (same breach = same spam) | Belief persists: grudges, habits, cooldowns, hysteresis |
| Layers fight via skip-lists | Arbitrator picks ONE action per force per pulse |
| Tuned by radii/counts | Tuned by utility weights + falsifiable criteria |

## 4. Ares/Phobos limits → our pluses (boundary table)

Source: `PROJECT/RUNTIME_BOUNDARY.md`, Phobos `AI-Scripting-and-Mapping`,
Ares 3.0 docs, TK3600/MO audit. "Смотрите-ка" column is the demo line.

| # | Ares/Phobos limit | SmartAI M2 answer | Demo line |
|---|---|---|---|
| L1 | Static weights + fixed ScriptTypes. No live arbitrary predicate mid-assault | D1 threat-adaptive reselection: AA/concentration/HP-drop signal per pulse, `Attack(alt)` + `GetTarget()` readback (extends `target_reselect` victim-centric v2 into SmartAI defenders/raiders) | "Ares задаёт приоритет до боя. Мы меняем цель во время боя — по тому что видим сейчас" |
| L2 | One TeamType = one script sequence. No force-level retreat/commit/split | D2 force decisions via `framework/tactical.lua` (`own_power vs enemy_threat → continue/changetarget/retreat/disengage`) + `framework/force_group.lua` (independent same-frame decisions per group) wired INTO SmartAI (today `main.lua` uses neither) | "Phobos ведёт отряд по скрипту. Наши две группы в один фрейм решают разное: одна жмёт, другая отходит" |
| L3 | No memory between waves (triggers are stateless) | D3 belief layer: grudge table (who raided me ×3 → hunt them), player-habit model (turtle/rusher/tech from census history), harvester-harass schedule, harass cooldowns | "Ares не помнит прошлую волну. Мы помним кто приходил — и встречаем именно его" |
| L4 | No cross-force coordination primitive (retaliate is per-team reflex) | D4 arbitrator: ONE decision per force per pulse (defend vs escort vs raid vs recall), feint/split (cheap noise + real economy strike), V3+spotter pair logic | "У них отряды не договариваются. Наши делят роли каждый пульс" |
| L5 | Difficulty = cheat multipliers (MO Hard income ×50), same brain | D5 personality + adaptive pacing: rusher/turtle/tech selected per match; Director tightens/loosens `scan/rallyEvery/defendersN` by player pressure (rubber-band, no economy cheat) | "Они усложняются читами. Мы — характером и темпом" |

Functions A and B the user named map to: **A = L1 live reselection**,
**B = L2+L4 force coordination**. Both are "no suitable model" in INI/Ares/
Phobos per `RUNTIME_BOUNDARY.md:70-86`.

## 5. Architecture (no new bindings in M2)

```text
snapshot (existing one-scan, main.lua:366-385)
  ↓
belief (new, pure Lua: grudges, habits, cooldowns, per-force state by id)
  ↓
options (per force: defend / escort / raid / recall / hold — max 5)
  ↓
score (utility = threat × value × distance × belief, deterministic, ID-tiebreak)
  ↓
commit (existing primitives ONLY: MoveTo/Attack/Stop/Hunt; transition-only + refresh caps)
  ↓
readback + divergence log (existing TARGET_DIVERGENCE_CTX pattern)
  ↓
reassess next pulse (hysteresis: winner sticks unless challenger beats it by margin)
```

Reuse, not rebuild: `tactical.lua` (scoring), `force_group.lua`
(multi-group loop), `combat_state.lua` (id tracking), `target_reselect`
AA/route gate logic (ported as functions, mod stays independent).
`World.GetAITeams()` (M1-A) feeds belief: vanilla teams' composition hints
the next wave direction — SmartAI positions, never commands them (read-only).

## 6. Hard constraints (from Gate 2 gaps — not assumed away)

- No production writes (`AI.QueueUnit` BLOCKED live) → M2 commands existing units only.
- No damage/death events (`OnPreDamage` unwired, `OnUnitDestroyed` never
  dispatched) → ID-diff + HP-drop polling only.
- No order lease (vanilla re-selects eventually) → transition-only orders +
  cooldowns + divergence logging; persistence measured, never claimed.
- One snapshot per tick, pcall-hardened, Lua-side dist2, allied cache
  (existing perf contract stays).
- Determinism: ID-ordered, no RNG/clock, logical frames (CnCNet-safe).
- Difficulty stays one global preset until a `House:GetDifficulty`-class
  binding is proven needed → that proof becomes a separate Beta candidate,
  never part of M2.

## 7. M2 checks (inside the milestone — NOT Gates)

- [ ] C1 Belief layer (pure Lua, harness-tested): grudge/habit/cooldown tables, restart-clear, no userdata retention.
- [ ] C2 Arbitrator (one force first — home defense force): defend vs hold scored, hysteresis margin, log `DECIDE force=… options=… winner=… margin=…`.
- [ ] C3 Raider force (hunter group from idle surplus when home quiet ≥600f): snipe refinery/power/AA by value, retreat on unfavourable ratio.
- [ ] C4 Coordination: defense-first claim + escort/raid stand-off generalized through arbitrator (replaces ad-hoc skip-lists, behavior preserved then extended).
- [ ] C5 Personalities + Director pacing (rusher/turtle/tech select + rubber-band cadence, no economy writes).
- [ ] C6 Live verification per check (fresh match + fresh `LuaAPI.log` per check; PASS/FAIL/INCONCLUSIVE each with log lines).

## 8. Acceptance (falsifiable — M2 closes only if ALL hold)

1. Same-frame split: two forces log different winners on one frame (`DECIDE` lines).
2. Retreat visible: unfavourable ratio → `MoveTo` away (not death-in-place) ≥3 cases/session.
3. Memory visible: repeat raider gets hunted preferentially (grudge log + orders).
4. No regression: officer 70/70 + capture 28/28 green; point-defense readback ≥ prior rate; zero `FRAMEWORK-ERR`; no order-fight spam (orders/frame ≤ M1 baseline).
5. Boring-test: user plays 2 matches (different personalities) and confirms "не скучно" with one cited moment per match (log-backed).

## 9. Explicitly NOT M2

Production control, superweapons, radar/fog, alliance switches, order-lease
native primitive, difficulty-per-house binding, Vanilla AI changes, Ares/Phobos
duplication, LuaAPI roadmap changes.

## 10. Progress log

- 2026-09-24: M2 declared (this file). No implementation. No behavior changes.
- 2026-09-24: **C3 implemented** (`scripts/mods/smart_ai/main.lua`: presets
  `raid/raidN/raidMin/raidQuiet/raidEvery/raidRange/raidRetreatMult`,
  `raidTargetValue`, raid section after recall, `isOfficerAssigned` +
  `lastSmartAIOrder` + hygiene coverage, `lastThreatFrame` quiet inputs).
  **Harness 8/8** (`tools/tmp/smartai_raid_test.lua`: quiet gate, form,
  refinery>power, no churn, retreat, easy silence, dead-target re-pick).
  **No regression**: officer 77/77, capture 28/28. Grade: IMPLEMENTED +
  HARNESS VERIFIED. **Live verification pending** (user-run match:
  quiet home → expect `RAID_FORM` + `RAID_TARGET` + hunters on economy).
- 2026-09-25: **first live session analyzed** (Germans vs Russians,
  frames 1800→27870): defense healthy (3 rally + 5 point-defense, 5/5
  readback, divergences with cause, surrender 16/16 + LOSE called),
  but **RAID=0 — group never formed**. Prime suspect: idle pool <3
  (vanilla tasks its armor, no idle surplus). Added throttled tells
  `RAID_POOL_SHORT` / `RAID_STATUS` (every ~1800f: quiet/pool/
  econ_in_range) so the next log names the blocking gate. Suites green
  (raid 8/8, officer 77/77, capture 28/28). No behavior change apart
  from diagnostic lines.
- 2026-09-25: **live tells analyzed** (5× `RAID_POOL_SHORT`, frames
  1800→10800): pool=0 the whole match — vanilla tasks ALL its armor,
  idle surplus never exists. **Fix (no new bindings): form prefers idle
  but fills from marching-not-fighting units** (`attacking==false`,
  same strictness as march recall; fighting units never yanked);
  `RAID_FORM` now logs `idle=X march=Y`. Harness 9/9 (new T7:
  marching-only roster forms + attacks), officer 77/77, capture 28/28.
  Surrender question answered from the same log (see below).
- Surrender semantics (user: "fires always on MCV"): per design it is a
  **conjunction** `no Barracks AND no WF AND no MCV/CY` (`main.lua`
  surrender block) — it cannot fire on MCV alone. Live proof 00:28 log:
  at frame 16200 Russians already had no NAHAND/NAWEAP (killed earlier),
  only NACNST left; CY died → 0/0/0 → latch. MCV dies last most games,
  hence the impression. Real false-positive vector stays the anchor
  tables (UNVERIFIED IDs): a missing ID surrenders early, an extra ID
  only delays — fail-safe direction documented in `main.lua`.
- 2026-09-25: **C5 implemented (execution-layer wiring)**: per-house
  ForceGroup (CombatStateTracker Observe + Tactical Evaluate/Decide)
  drives raid fight/flight; SmartAI keeps Act (claims/dlog), target
  selection (grudge injected into the snapshot — without it the group
  stuck at find_target past its 18-cell radius) and coordination.
  Audit rejections recorded: stock `_actDefault` bypasses the arbiter
  and per-member full scans (not used); resume resets the order window
  (retreat order doubled as attack cooldown → 600f sit-out, fixed).
  T4 scenario corrected en route (4 Rhinos vs 8 GIs must NOT retreat —
  ratio 2.38; overwhelming armor does). **Harness 8/8**
  (`tools/tmp/smartai_group_test.lua`: two houses, A retreats while B
  holds in the same frames, resume, reset re-forms). No regressions
  (raid 12/12, officer 89/89, capture 28/28, belief 14/14, arbiter 6/6,
  defense 16/16). Grade: IMPLEMENTED + HARNESS VERIFIED. **Live
  verification pending** (fresh skirmish: GROUP retreat/continue lines
  tracking the raid's fortunes).
- 2026-09-25: **mod-namespace fix (live log proof)**: on modded maps the
  AI economy is `RAZER-`-prefixed (`RAZER-NAREFN` full-HP all match) —
  exact-match blinded belief + raid valuation + garrison (+surrender
  anchors on RAZER-CY maps). Fix: `typeIn(set, t)` exact-or-`-SUFFIX`
  for all building sets. Same session found + fixed a real edge bug:
  fresh victims started `eventFrame=0`, muting the first 300 frames of
  every match. Harness T10 (belief 14/14). No regressions (officer
  89/89, raid 10/10, capture 28/28, arbiter 6/6, defense 7/7).
- 2026-09-25: **C3 LIVE VERIFIED** (user match, Russians): 4× FORM
  (first live proof of marching recruitment: `idle=0 march=4`),
  7× TARGET incl. economy snipe (`NAREFN value=3.0`), 3× RETREAT
  (incl. last-man `members=1` stand), reform cycles after wipe,
  surrender 0/0/0 + SELL 13/13 + LOSE called at 33000, zero errors.
  Live-found churn fixed: FORM→STANDDOWN→FORM on adjacent scans
  (no economy in range yet) → re-form cooldown `raidEvery` on
  STANDDOWN. Harness 10/10 (new T8: single form, no churn),
  officer 77/77, capture 28/28.
- 2026-09-25: **C1 implemented (grudge slice)** (`main.lua`: `grudge` +
  `econSeen` tables above `officerReset` and cleared in it; HP_DROP +
  ECON_KILLED detection with nearest-hostile attribution inside 12 cells,
  per-victim 300f cooldown; `grudgeMult` 1+0.5×min(n,4) applied in the
  raid target loop with a tracked no-memory winner; `BELIEF_EFFECT`
  logged on flip; test-only `SmartAI.BeliefInspect`). **Harness 13/13**
  (`tools/tmp/smartai_belief_test.lua`: baseline P → damage → flip to Q,
  growth, stability, non-economy/unattributed ignored, determinism
  replay, kill event, reset clears). No regressions (raid 10/10,
  officer 77/77, capture 28/28). Grade: IMPLEMENTED + HARNESS VERIFIED.
  **Live verification pending** (fresh skirmish: raid the AI economy
  repeatedly → expect   `BELIEF_EVENT … grudge=N` then `BELIEF_EFFECT`
  retargeting onto the attacker's economy).
- 2026-09-25: **C2 implemented (arbiter slice)**: tick-local claim
  registry (`claimTick`/`arbSkips`/`tryClaim` in `Update`, priority =
  file order GUARD > DEFENSE(+MARCH) > ESCORT > GARRISON > RALLY >
  RECALL > RAID), all 8 drafting sites honor it, `ARBITER skips …`
  summary on contested ticks. Audit corrected itself mid-work:
  MARCH↔RAID need no arbitration (exclusive gates + assigned-memory).
  Ruling recorded: live breach outranks recall presence (RALLY >
  RECALL) — pre-C2 both ordered (last-write-wins mush); T14/T18
  re-asserted to the arbitration outcome. **Harness 6/6**
  (`tools/tmp/smartai_arbiter_test.lua`: rally/recall single order,
  garrison single send, reset re-drafts). No regressions (raid 10/10,
  officer 79/79, capture 28/28, belief 13/13). Grade: IMPLEMENTED +
  HARNESS VERIFIED. **Live verification pending** (fresh skirmish:
  contested tick → expect `ARBITER skips …` + no double orders).
- 2026-09-25: **C4 implemented (severity slice)**: `intruderSev`
  (suicide 4.0, vehicle 1+cost/2000, infantry 0.5) → LOW/NORMAL/HIGH →
  sevN (1 / defendersN / defendersN+1) + assignR (×1.5 on HIGH),
  `DEFENSE_SEV` logged on change. Harness found + fixed two real
  defects: holders never counted toward the cap (over-draft on later
  scans) and the holding branch was unreachable (assigned-memory
  excluded holders from candidacy) — Tier-1 rewritten (holders seed
  slots first, this-target holders pass the filter). **Harness 7/7**
  (`tools/tmp/smartai_defense_test.lua`: LOW single defender +
  reserve, HIGH 3 on truck, downgrade, replay). No regressions
  (officer 81/81 incl. new T25 suicide-first, raid 10/10, capture
  28/28, belief 13/13, arbiter 6/6). Grade: IMPLEMENTED + HARNESS
  VERIFIED. **Live verification pending** (fresh skirmish: scout →
  1 defender; push →   `DEFENSE_SEV … HIGH` + massed defense).
- 2026-09-25: **C4 air slice (user Kirov report)**: 5 Kirovs walked over
  the base — AA never prioritized (Rhinos can't hit air; FLAKT not
  drafted at all). Fix: `AA_TYPES` (HTK/FLAKT/FV/YTNK, INI-confirmed)
  sort first vs air jobs (`isAirThreat`: aircraft kind or ZEP/KIROV
  type); infantry drafts only as AA. Harness T27/T28 (officer 89/89).
  No regressions (raid 10/10, capture 28/28, belief 13/13, arbiter 6/6,
  defense 7/7). Early-warning radius left as follow-up.
- 2026-09-25: **weaknesses fixed (senior review)**. (1) Lease-lite:
  yanked live targets get bounded re-asserts (60f window — deliberately
  shorter than Tier-1 refresh 150f, lease is first responder; max 2
  retries; then LEASE_RELEASED), arbiter-claimed, log-visible. Harness
  proved the first design (window = Tier-1) was dead code and that
  divLogged anti-spam gated the lease — both fixed (defense 11/11).
  Full native lease still needs C++ (separate Beta case if proven).
  (2) Director rule #1 (C5 slice): HIGH home severity disbands a live
  raid to the defense pool (`HOME_HIGH`, re-form needs quiet). Harness
  T9 (raid 12/12). No regressions (officer 89/89, capture 28/28,
  belief 14/14, arbiter 6/6, defense 11/11).
- 2026-09-25: **retaliate slice (user Apoc-vs-choppers report)**:
  vanilla holds a low-value target while dying — AI Apocs chased
  choppers instead of answering Apocs. Fix: HP-dropping AI combat
  units turn onto the nearest hostile (native Attack + readback,
  stays in combat, 300f cooldown, assigned/non-combat skipped).
  Harness T7–T9 (defense 16/16). No regressions (officer 89/89,
  raid 12/12, capture 28/28, belief 14/14, arbiter 6/6).
- 2026-09-25: **bomber slice (user Kirov report)** — two findings:
  (a) SmartAI itself drafted AI Kirovs as point-defense interceptors
  (`POINT_DEFENSE ZEP->HTNK` live) — bombers excluded from Tier-1
  candidacy (strike assets, not interceptors); (b) AI bomber holding
  troops with an enemy base within 25 cells is redirected onto the
  base (`BOMBER_FOCUS`, 300f cooldown, readback, claimed; skips
  assigned roles). Harness T29/T30 (officer 93/93). No regressions
  (raid 12/12, capture 28/28, belief 14/14, arbiter 6/6, defense
  16/16, group 8/8). Same session validated v2 live three times
  (`SURRENDER_DEFERRED` 52/32/39 fighters) + RETALIATE x66 +
  BELIEF x13 in one match.
- 2026-09-25: **first emergent moment live (user report + log)**:
  production destroyed → `SURRENDER_DEFERRED … fighters=82 value=26900
  topfoe=5100` (futility gate held, no early surrender) → AI attacked
  with V3+tanks+Kirovs (vanilla waves + escort/raid coordination) →
  army wiped → clean latch (SELL 0/0, LOSE called). Same session found
  escort HUD spam (`+0 bodyguard(s)` on every moved-V3 refresh) — fixed
  (HUD only on real adds). No regressions (full suite green).
- C6 evidence #1 (user-reported, 2026-09-25): after losing production
  the AI deferred surrender and counterattacked (V3+tanks+Kirovs);
  user quote: "if I don't destroy his base urgently, he'll destroy
  mine and I'll lose." Player forced to react to the AI — the C6
  metric, observed in the wild.
- 2026-09-25: **defense tuning vs demo-rush (data-only, NOT C4)**:
  user beats the AI with 2–3 DTRUCK + rhinos — cheap suicide units
  sorted mid-pack by price while defenders engaged rhinos. Fix:
  `SUICIDE_TYPES = { DTRUCK }` (INI-confirmed) rank -1 in intruder
  sort (threat != price). Harness T25 (officer 81/81). C4 stays
  unstarted — no severity/reserve logic added.
- 2026-09-25: live check SKIPPED by user decision (crate-elite rush =
  outright base kill, no repeated economy attacks — not valid C1
  evidence). C1 live status stays: NEEDED.
