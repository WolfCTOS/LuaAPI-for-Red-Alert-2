# SmartAI CHANGELOG (mod-owned history, NOT part of the LuaAPI milestone namespace)

> architecture: SmartAI is a consumer/reference implementation of LuaAPI
> (`AGENTS.md` Milestone Architecture). LuaAPI milestones stay API-only;
> this file is SmartAI's own history.

## [Unreleased] — VALOR progress (GetVeterancyProgress, no RE)

- **Ответ на вопрос:** RE не понадобился — `VeterancyStruct.Veterancy`
  (float XP) уже замаппен в вендорном YRpp, тиры захардкожены 1.0/2.0.
  Новый биндинг `unit:GetVeterancyProgress()` (0.0..1.0; SEH; ±15
  строк + регистрация + API.md), DLL пересобрана и задеплоена.
- **Use:** `e.vetProgress` в снапшоте (без биндинга читается 0 —
  graceful). Veteran top-up в `valorBonus` (0.9-vet ≈ +1.9: denied
  почти как элита; rookie без топ-апа — deny elite-перехода важнее).
  Preservation через `isPrecious` (веты + прогресс ≥0.5 + T3) в
  feint/wave. Ретрита ветов по-прежнему нет (оборона).
- **Harness.** V7 (0.1-from-elite gets kill-priority over fresh
  veteran) + V8 (0.9-rookie spared from feint) — valor **10/10**.
  Полная регрессия зелёная (22 файла).
- Grade: IMPLEMENTED + HARNESS VERIFIED + BUILT. **Live
  verification pending** (биндинг живьём не читан — INCONCLUSIVE до
  матча; прогресс в CENSUS не светим).

## [Unreleased] — VALOR (veterancy doctrine: promote ours, kill theirs)

- **Read.** `e.vet` в снапшоте via `unit:GetVeterancy()` (биндинг уже
  был; без биндинга читается как rookie — promotion blindness, never
  misgrade). T3 из INI: APOC/BFRT/SREF/MGTK (Prism=`SREF`,
  Fortress=`BFRT` — проверено по `rulesmd_ref.ini`).
- **Anti-valor (убивать).** Общий `valorBonus`: elite +2.0, veteran
  +1.0, enemy hero +1.5, enemy T3 +1.0. Вшит в `retargetValue`
  (элитный GI 2.5 > rookie Rhino 1.175 — множители первыми) и в
  HERO-скоринг через общий `heroBaseValue` (скан и focus-keep больше
  не дублируют формулы). Для rookie поведение бит-в-бит прежнее.
- **Preservation (сохранять).** Feint берёт только rookie non-T3
  (min-cost среди пушечного мяса; было: вет того же типа мог уйти
  как самый дешёвый). Wave-shepherd: vet-led группа (половина+)
  отзывается при 2x, не 3x (`mult=` в строке). Raid строится
  T3-first (ID внутри тира). Tier-1 без изменений (элиты обороняются
  и качаются — так и должно). Ретрита ветов нет сознательно (ломает
  оборону; см. минусы).
- **Harness.** Новый `tools/tmp/smartai_valor_test.lua` **8/8**
  (retarget на элиту, герой на elite-T3 и на вражеского героя, feint
  стоит без баннера когда доступен только вет, wave 2x vs rookie
  hold, APOC ведёт рейд). Полная регрессия зелёная (21 файл).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (ветераны в строках CENSUS/ORDER + видимый фокус по ним).

## [Unreleased] — HERO officer (heroes on every difficulty)

- **Layer (officer loop, first).** Own BORIS/TANYA/YURIPR get their
  own targeting: Tanya infantry 3.0 / buildings via raid economy /
  units 1.0; Boris vehicles 2.5 / infantry 1.5; Yuri Prime
  cost-based control (cap 4.0, never buildings). All plain Attack —
  airstrike/deploy have no bindings. Anchor = min-id own building;
  leash to anchor (easy 8 = base bodyguard, medium/hard 25);
  transition-only + per-preset cooldown (900/600/300) with same-target
  refresh (cheap yank-resistance, no lease). Hard adds retreat below
  30% HP (threat-gated resume) + focus (no switch below +1.0 gap).
- **Screen.** HERO holds every live hero (`heroState` ⇒ global
  `isOfficerAssigned` stand-off); Tier-1 + garrison additionally
  exclude `HERO_TYPES` by kind filter (execution-order race: commander
  runs before the officer loop, so type-exclusion, not claims, guards
  scan 1). Rally never took infantry (vehicles-only pool, verified).
- **Caught by the harness:** first version passed snapshots to
  `orderAttack` — silently dead live (snapshot has no GetId). Fixed
  to `best.u.u` (escort `prime.u.u` convention); 16/16.
- **Harness.** New `tools/tmp/smartai_hero_test.lua` **16/16**
  (targeting x3, cooldown, focus, retreat+resume, Tier-1/garrison
  screen, easy leash vs medium hunt, hold silence). Full regression
  green (20 files).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (hero built → `ORDER site=HERO` + visibly focused hero).

## [Unreleased] — Phase 1 (SetTarget hook + Tier-1 leases)

- **C++ (`src/target_lease.h/.cpp`, warpaw-private style).** MinHook
  детур на `TechnoClass::SetTarget` (`0x6FCDB0`, YRpp-verified):
  чужие re-task лизованного юнита — VETO (оригинал не вызывается) +
  журнал; свои приказы (цель == лиз) и target-clear (nil) текут;
  стейл-лизы (UID mismatch) — fail-open в ваниллу. Журнал bounded
  (128, ring): `TargetLease.Journal() -> {frame, unit, want, got}`.
  `Install`/`ClearAll`/`RegisterBindings` вшиты в `lua_engine.cpp`
  рядом с WeaponOverride; `ClearAll` на ресет сессии (хук всегда
  проигрывает новому матчу без боя). Билд: MSVC Release зелёный,
  `target_lease.obj` в деле, DLL+injector задеплоены в корень.
- **Lua (SmartAI, только Tier-1).** Acquire после
  `claimed and ordered` (unit+target userdata), Release при разрешении
  эпизода (живой деф) и при surrender (записи ledger не трогаем),
  `TargetLease.Clear()` в `officerReset`. Всё nil-guarded: без C++
  (харнесы) и при неактивном хуке — unleashed (ваниль + readback).
  Retarget/raid/escort НЕ лизуются (watch остаётся defenseState-only).
- **Известное ограничение (§6.5 брифинга):** хук не различает ручной
  re-task человека и ванильный — оба ветируются в окне лиза; лизы
  короткие, после экспайри ручной приказ липнет. Дизайн veto-фильтра
  — Phase 2.
- **Harness.** Зонд P1 расширен стабом TargetLease: acquire on draft
  (unit, target) + release on resolve — 9/9. Остальные 18/18 зелёные.
- Grade: IMPLEMENTED + HARNESS VERIFIED + BUILT. **Live
  verification pending** (user-run матч: строки журнала вето,
  инверсия P1 `final_target=vanilla → SMARTAI`, отсутствие крашей;
  при отказе хука — чистый fail-open в ваниллу).

## [Unreleased] — 3 micro-probes (Phase-1 зонды, каждый <= 3600ф)

- **P1 overwrite** (`smartai_probe_overwrite.lua`, 7/7): same-mission
  target-swap детектируется за 20ф (<=2 сканов), reassert один,
  one-shot yank проигрывает ванилле без всякого хука. Fight-back
  (срыв каждые 150ф x6): SmartAI борется в пределах лиза
  (LEASE_RETRIES=2), затем уступает — стойкая ванилла побеждает.
  Инвертированный критерий Фазы 1: после хука там должно быть SMARTAI.
- **P2 handoff drain** (`smartai_probe_handoff.lua`, 7/7): три резерва
  стаггером (иначе пул 3 idle собирает рейд и съедает сетап), три
  визита интрудеров. Итог ordered=3 handoff=3 — drain ratio **1.00**:
  в полосе драфта кросс-слойные клеймы съедают ВЕСЬ выход rally.
- **P3 furball** (`smartai_probe_furball.lua`, 5/5): 6 связанных боем
  танков (idle 0 — French-hard shape): retarget свитчей 6/6, срывы на
  декой исцелены 6/6, финальная эффективность 6/6. GAP-находка:
  diagnosed=0 — divergence watch идёт только по `defenseState`
  (приказы retarget/raid/escort не покрыты): исцеление без диагноза.
  Вводная для Фазы 1: исход приказа нужен ВСЕМ командующим слоям.
- **Побочная находка (readback):** 6-скановое окно вердиктов слепло к
  Tier-1 драфту через 600ф после rally-приказа (drain реален, вердикт
  его не видел). Окно заменено на 3600ф + вердикт STALE (считается,
  логируется с `verdict=STALE`); MOVE_STATS/MoveInspect несут `stale`.
- Grade: IMPLEMENTED + HARNESS VERIFIED (19/19 с учётом зондов).

## [Unreleased] — KEEP/QUIET/CUT (log chatter, user-approved 2026-10-03)

- **Evidence (two live matches).** `LuaAPI.log.prev` (7-min,
  2026-10-01): ORDER 832, of which RADEVAC 415 + GARRISON 392 (97%);
  GROUP 103, ARBITER 81, RECOVERY 78 (incl. 3x identical lines in 4s).
  `LuaAPI.log` (12-min hard loss, 2026-10-02): ORDER 592, of which
  RADEVAC 346 + GARRISON 245 (99.8%) — one unit re-issued the same
  MoveTo to the same dest every 300f. Periodic diagnostics every
  1800f x 8 channels ≈ 150-170 lines/match of near-verbatim repeats.
  Rare causal lines stay: TARGET_DIVERGENCE 8+CTX, LEASE_REASSERT 5,
  RAID_POOL_SHORT 10-11, BELIEF_EVENT 10-28, BOMBER_FOCUS 9-14.
- **Decision (user).** KEEP all order/event/causal lines. QUIET:
  periodic diags (RALLYDIAG/C4DIAG/ADAPTSTATE/C4PRESET/CENSUS/BASE)
  to on-change + every-3rd heartbeat; GROUP to decision|reason
  transitions (ratio rides along, does not trigger); ARBITER to
  skip-set change; RECOVERY to on-change with force on claimed/
  ordered (a real response is always news). CUT: C4DIAG_SAMPLE merged
  into C4DIAG as a `samples=` suffix (same fields, one channel).
  DEFENSE_SEV was already on-change; MOVE_STATS stays periodic (the
  new meter). Liveness preserved by design (heartbeat = "running
  with nothing new" vs "not running").
- **Mechanism.** One helper `dlogChange(tag, frame, key, msg, cmp,
  force)`; `diagLast` cleared on match restart (frame-backwards, next
  to guardReset/officerReset). Zero behavior change: orders, claims,
  state transitions untouched — emission gating only.
- **Harness.** Defense 140/140 (helper `c4diagFor` falls back to
  last-seen; T44 re-asserts samples inside merged C4DIAG). Full
  regression green (16 files, same counts as readback entry).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (user-run match: expect periodic diags ~1/3 volume,
  contention only on transitions, ORDER collapse via post-hold
  RADEVAC/GARRISON + MOVE_STATS split).

## [Unreleased] — MoveTo readback (order-outcome verdicts + availability census)

- **Why.** Crossed-action pool reordering (Phase 1 prep): without
  knowing which MoveTo orders arrived, got yanked by vanilla, or were
  claimed by another SmartAI layer, availability is a guess. Readback
  closes the loop read-only — no orders added, no behavior changed.
- **Mechanism.** Rally + escort kite issue tracked MoveTo
  (`orderMoveTracked` records issue frame/dest/mission); a 6-scan
  readback window verdicts each: ARRIVED (on dest), YANKED (mission
  flipped by nobody — vanilla overwrite), HANDOFF (mission flipped by
  a live SmartAI-holder — cross-layer claim, e.g. Tier-1 drafting a
  rallied reserve). Counters `MOVE_STATS` per 1800f plus
  `MOVE_DIVERGENCE` cause-lines and an idle/marching census on the
  same gate (availability probe: the French-hard loss ran idle 0 +
  marching ~0 all game). Attack re-tasks by other layers log
  `READBACK_MISMATCH` (same non-interference discipline as lease
  research: report, never fight).
- **Harness.** New `tools/tmp/smartai_movereadback_test.lua` **8/8**
  (tracked order, arrival, yank, handoff geometry, counter
  accumulation, census). Full regression green (16 files: officer 41,
  escort 12, rad 16, radevac 10, miner 14, wave 8, buildlaw 37, raid
  26, belief 16, arbiter 6, defense 140, group 8, difficulty 23,
  surrender 45, retarget 8, movereadback 8).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (match → `MOVE_STATS ordered/arrived/yanked/handoff/
  idle/marching` lines show the real overwrite-vs-availability split).

## [Unreleased] — Retarget officer (fight-smarter for engaged matches)

- **Found in live log 2026-10-02** (French hard, 12-min loss): the
  AI's entire army spent the match permanently `attacking`
  (border skirmishes) — idle pool 0 AND marching pool ~0, so every
  surplus-driven layer (raid/escort/recall) starved all game (11
  POOL_SHORT). SmartAI commanded nothing while vanilla fed tanks
  into counters one by one. Retargeting is the only move that works
  at 100% engagement: redirect fire, never withdraw (no lease
  fight — positions stay vanilla's, targets become ours).
- **Layer (raid family, medium/hard).** Engaged AI combat with a
  strictly better target in reach switches fire (value gap 1.0+,
  never farther than current +2; attack-movers commit within 15).
  Values: MCV 3.0, suicide 4.0, harvesters 2.0, artillery 2.5,
  tanks 1.0+cost/4000, infantry 0.5, buildings via raid economy
  weights. SmartAI-held fighters keep SmartAI targets; harvesters/
  MCVs never re-aimed (but are priority targets); civilians never
  targets. Transition-only + 300f cooldown; dlog only — the focus
  fire itself is visible, banners would spam every battle.
- **Harness.** New `tools/tmp/smartai_retarget_test.lua` **8/8**
  (switch, hold-best, range gate, cooldown + richer re-switch,
  attack-move commit, civilian exclusion). Self-checks forced a
  redesign mid-work: value-first selection let a far rich target
  veto a near good switch — selection is now range-first (a far
  rich target can neither trigger nor block). Full regression
  green (15 files).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (engaged match → `RETARGET from=X to=Y` lines +
  visibly focused AI fire).

## [Unreleased] — Garrison arrival-hold (churn fix from live log)

- **Found in live log 2026-10-01** (French, normal): 249 GARRISON
  orders in one 7-minute match — the refresh re-issued MoveTo every
  600f per building with no arrival memory (unlike escort/radEvac/
  miner, which all hold). Functionally near-no-ops, but 249 orders
  for zero visible effect, and each re-issue resets the unit.
- **Fix.** Holders (recorded + squatters already on the cell) keep
  their slots; only the shortfall to GARRISON_N is drafted, and
  on-cell infantry is never a candidate. Same transition-only +
  cooldown discipline; self-healing (a yanked holder stops holding
  and gets replaced next window).
- **Harness.** Officer 41/41 (+T5b squatter/arrival proofs).
- Grade: IMPLEMENTED + HARNESS VERIFIED.

## [Unreleased] — Wave-shepherd (doomed-attack recall)

- **Report (user, 2026-09-30).** AI opens every match feeding tanks
  into prepared counters — countering it is the player's reflex by
  now. Vanilla waves, not SmartAI orders: the fix is preserving the
  force, not banning attacks.
- **Layer (recall family, medium/hard).** A marching-not-fighting AI
  group (3+ within 12 cells) far from home (>25) facing >=3x local
  mobile strength (within 15) is ordered home: deny the free kill,
  let vanilla mass bigger later (or not at all — the reflex stops
  working either way). Fighters/unknown-state/loners/harvesters/
  MCVs/artillery/officer-held never touched. Transition-only +
  shared recall memory/cooldown (300f); claims MARCH (defense tier,
  loses ties to rally correctly); one HUD per wave per 900f.
  Structures not counted as threat in v1 (mobiles decide fights).
- **Harness.** New `tools/tmp/smartai_wave_test.lua` **8/8**
  (recall + single HUD, fair odds proceed, fighter/unknown/loner
  untouched, cooldown silence + refresh). No regressions anywhere.
  Self-check caught a missing `GetUnitsInRadius` in the new stub
  (raid group sync needs it once a group forms).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (early waves vs prepared counters → `WAVE_RECALL` +
  preserved armor massing later).

## [Unreleased] — M3 seed: stance, escalation, counterpunch (truly smart)

- **Why.** Review verdict: SmartAI was all reflexes (reactive layers,
  no memory that matters, no readable intent, no initiative). This
  slice adds character + adaptation + initiative on existing
  machinery, no new bindings, no new Gates.
- **Stance (character).** First AI house rushes, then alternate
  (deterministic engine order; easy skips it, rally-only by design).
  Rusher: bigger/faster/farther raids; turtle: smaller/rarer/closer
  (adjusted preset COPY — shared table never mutated). Announced
  once per house with difficulty; hard houses present as
  **Mastermind** (the one CnCNet-lobby name we own is the one we
  say — the lobby label itself is client-owned, unchangeable here).
- **Escalation (adaptation from evidence).** Raid WIN (target gone,
  members alive) levels up (cap +2, bigger next form, "escalating");
  loss ticks and wipes level down (floor -1, "bloodied"/"wiped
  out"). Wipe detection: members held, none live.
- **Counterpunch (initiative).** A rusher whose enemy is bled dry
  (<=4 combat units, or none but standing economy) does not wait
  for quiet — it finishes ("Hunter group out: smells blood").
  Self-resolving (no targets -> STANDDOWN). Cooldown 1800f.
- **Visible memory.** Grudge flips that change the target now HUD
  ("Avenging past raids: hunting X economy!").
- **Defense-first guard.** No NEW raid forms while a breach is
  active this tick (live groups continue unless HIGH) — keeps fresh
  releases flowing to the rally instead of being drafted mid-handoff
  (caught live by officer T8: counterpunch stole rally reserves).
- **Harness.** Raid 26/26 (S10 stance split + announce, S11 5-form
  then escalated 6-form via kill/re-pick/standdown/reform cycle,
  S12 counterpunch despite blocked quiet, S-hard hard-always-rushes
  + Mastermind title, S-feint distraction with singularity proof;
  T1 rewritten for rusher cadence + strong-enemy gate, T6 restores
  auto (found a leaked global-medium pinning per-house difficulty),
  T9 e3 GAREFN). Group 8/8 (turtle timing). Belief 16/16 (S13
  Avenging banner). Self-checks caught: counterpunch preempting
  every form at 0 enemies (fresh start would raid at frame 30 —
  gated on enemy existence now) and the T8 handoff steal above.
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (stances announced, escalation visible across raids,
  counterpunch after a won battle).

## [Unreleased] — Visibility pass: HUD for reactive layers + T11 isolation

- **Problem.** Most SmartAI reactions were log-only: a player never
  sees RADEVAC evacuate, miners resume, or a V3 kite — invisible
  smarts might as well not exist (the "boring" complaint is half
  perception). Raid/surrender/BUILDLAW already announced; the
  reactive layers did not.
- **Change (announces only, zero behavior).** RADEVAC HUDs one
  banner per house per 900f on evac (`radEvacHud`); miner officer
  HUDs once per house per match (`minerHud`); V3 kite HUDs per
  principal per 900f (`kiteHud`, pruned by hygiene). Intercepts
  stay log-only (150f cadence would spam — the radiation-banner
  lesson: 13 banners/event cut to 3). All state reset in
  `officerReset`.
- **Harness.** New `tools/tmp/smartai_radevac_test.lua` **10/10**
  (RADEVAC never had a test: evac, margin/ACTIVE semantics,
  cooldown + HUD throttle, IDLE/absent silence). Miner 14/14
  (+HUD assert), escort 12/12 (+kite HUD assert). Belief 15/15
  (+T11 multi-house isolation; OPEN BUG entry closed).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (banners visible in-match, no spam).

## [Unreleased] — Miner officer (idle harvesters go back to work)

- **Report (user, 2026-09-30).** AI builds 3 refineries, one miner
  works, the rest idle at their refineries. (The 3-refinery build
  itself is vanilla's choice — untouched by design; the idle
  miners are SmartAI's jurisdiction: units, not construction.)
- **Layer.** New Miner officer (medium/hard): idle HARV/CMIN get
  `HarvestAt` — resume own field anchor (`GetHarvestLocation`),
  else share the nearest ACTIVE donor's field; docked anchors (at
  own base) don't count as fields. Only idle miners (workers are
  non-idle by definition — kicking one cannot interrupt work);
  SMIN excluded (deployed slave-miner minigame untouched);
  vehicles/enemies untouched; HarvestAt only, never MoveTo/Attack.
  Per-miner 300f cooldown + arrival hold; claimed miners skipped by
  rally via `isOfficerAssigned`. First live consumer of the
  `HarvestAt`/`GetHarvestLocation` primitives (previously no live
  consumer on record).
- **Self-found bug.** First version added miner assignees to
  `isOfficerAssigned` (for the rally skip) without exempting the
  miner pass itself — kicked miners became officer-held forever and
  the re-kick never fired. Fixed with `officerHeldElsewhere()`
  (all layers honored except self); other layers' claims still win.
- **Harness.** New `tools/tmp/smartai_miner_test.lua` **13/13**
  (resume, share, docked-redirect, worker/SMIN/vehicle/enemy
  untouched, HarvestAt-only, cooldown silence + refresh, arrival
  hold, no-donor silence). No regressions across all suites.
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (match with idle AI miners → `MINER` HarvestAt lines;
  readback via anchor).

## [Unreleased] — BUILDLAW: strict build order (3 factions)

- **Report (user, 2026-09-30).** AI Battle-Lab-less Weather
  Controller standing on the map; later: refinery-first rebuild
  after total base loss (power/barracks/refinery, then refineries
  instead of tech).
- **Ground truth.** `rulesmd_ref.ini` is MODDED (campaign rows,
  lamps) while live `rulesmd.ini` is intentionally empty (stock
  engine rules) — so the INI is advisory only. Prerequisite GROUPS
  are stock (`PrerequisitePower/Factory/Barracks/Radar/Tech/Proc`
  + `ProcAlternate=SMIN`); community-confirmed "AI ignores
  prerequisite" explains bypass paths. Encoded: STOCK-CONFIDENT
  rows only (labs <= WF+radar; superweapons/high-tech <= lab).
  AND-of-ORs with radar alternatives (`Radar=yes` set), PROC incl.
  SMIN alternate, modded `-SUFFIX` tolerance. Deliberately NOT
  encoded: CY token (packed-MCV edge), ALL recovery infrastructure
  (power/barracks/refineries/factories/yards/depots/radars/
  defenses — policing recovery order griefs worse than the bypass;
  powerless-fixture barracks proved it: 11 sells -> surrender
  cascade -> 29 red, repaired by exclusion), walls, pillboxes,
  modded/corrupt rows — unknown = allowed.
- **Rule (final).** Sell IFF: chain broken NOW + broken at BIRTH
  (birth-legal never touched) + never queue-marked fresh at birth
  (AI.IsQueued read-only binding: ordered while satisfied =
  queue-completion; marks refresh while queued, expire QMARK_WINDOW
  after last sighting) + birth scan > 1 (load inventory legal) +
  past BUILDLAW_GRACE (default 1800f) + per-type sells below
  SELL_CAP 3 (then log-and-leave, no bricking) + AI +
  non-surrendered + preset on (medium/hard). History: v1 sold any
  NEW building without a CURRENT lab (insta-sold legal French GAWEAT
  f=31652 — snapshots can't tell queue-completion from fresh cheat);
  v2 never-had (spared everything, incl. true cheats); v3+v4 are
  this rule (HazelAI order-time doctrine made measurable).
- **Harness.** `tools/tmp/smartai_buildlaw_test.lua` **37/37**
  (superweapon/lab/tech sells + keeps, refund clawback, grace,
  easy off, human untouched, birth-law, queue-mark spare/expire,
  no-binding compat, sell-cap + give-up on nukes, SMIN alternate
  via Industrial Plant, modded/defense exclusions, MCV depot-less
  kill + backed/Yuri keeps + rally exclusion). Self-checks caught
  a dropped `if missing` guard, a vacuous SMIN test, and a fixture
  GADEPT that legalized its own target via history.
- Grade: IMPLEMENTED (incl. native `AI.IsQueued`) + HARNESS
  VERIFIED. **Live verification pending**: `QUEUE_GRAND` proves the
  read path; French-class cases classify correctly.
- **MCVLAW (user report 2026-10-01).** AI orders MCVs with only
  infantry/defense/WF standing. INI: `AMCV <= GAWEAP,GADEPT`,
  `SMCV <= NAWEAP,NADEPT`; YMCV has no Prerequisite line (exempt by
  design). Units can't be sold: enforcement is destruction via
  TakeDamage (no attacker/refund/bounty — damage events unwired),
  never-had rule (legally ordered + depot died + rolled out must
  survive relocation), birth>1, mobile-only (deployed MCVs are
  bases — IsDeployed gate), same gates/cap. Combat units never
  policed (killing armies = griefing). Companion fix: rally pool
  excludes MCVs/harvesters (live: AMCV ordered into a breach;
  every other layer already did).
- **Senior cut (user: stop fighting symptoms).** Tier-0/1 rows
  REVERTED after one iteration: punishing a recovering AI for its
  rebuild order griefs worse than the bypass (refinery-first is
  ugly, harmless, self-healing). Build ORDER lives in INI Build
  lists, not in a Lua policeman — offered as follow-up.

## [Unreleased] — RADEVAC: SmartAI understands the radiation mod

- **Contract (radiation side, additive only).** `Mod.GetStatus()` in
  `scripts/mods/radiation/main.lua`: pure read-only snapshot
  `{phase, untilFrame, targets={{x,y}}, radius}` — fresh table per
  call, zero behavior change to the hazard cycle. This is now a
  published cross-mod contract with a live consumer (below).
- **Consumer (SmartAI side).** New `RADEVAC` layer: while radiation
  reports WARNING (evacuate preemptively, radius+2 margin — the
  12s warning is the head-start) or ACTIVE (move out whoever is
  still inside), own infantry in the blast cells is ordered to the
  nearest own building (garrison-or-screen, mirroring the player's
  own counterplay). MoveTo only; idle first, marching-not-fighting
  backfills; fighting / unknown-state / exempt (AGENT/ENGR/THIEF —
  mirrors radiation `CFG.exempt`) / civilians never yanked;
  per-unit 300f cooldown + destination memory + arrived-holds (no
  churn while the cloud sits still). Arbiter priority
  `... RALLY > RADEVAC > RECALL > RAID` (breach kills faster than
  attrition); preset-gated (`radevac=true` on medium/hard, off on
  easy). Absent/inactive radiation or IDLE phase = silent zero-cost
  skip. Hygiene + `lastSmartAIOrder` coverage included.
- **Why it matters.** Engine `RadLevel` ground damages ANY infantry
  on the cell (`API.md` caveat) — even with radiation
  `affected="player"`, AI reserves walking through green take
  engine damage nobody attributes. RADEVAC is the first layer that
  reads another mod's published state and acts on it.
- **Harness.** New `tools/tmp/smartai_rad_test.lua` **16/16**
  (preemptive warn-margin evac, marching backfill, all skip classes,
  cooldown silence + refresh, arrival hold, IDLE silence,
  absent-mod no-op). No regressions: officer 39/39 (incl. externally
  added T10–T12 escort-combat tests, green), escort 11/11, raid
  12/12, belief 14/14, arbiter 6/6, defense 140/140, group 8/8,
  difficulty 23/23, surrender 45/45. `radiation_test.lua` crashes
  identically pre/post (stale harness vs radiation v0.8.0, line 283
  `reset` — not this change).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification
  pending** (both mods active: WARNING during an AI infantry
  exposure → expect `RADEVAC` orders in `LuaAPI.log`).

## [Unreleased] — Rally Hunt removed (OpenTS mission-queue mechanics, 2026-09-28)

- **Root cause (was KNOWN SUSPECT).** Rally issued `MoveTo` +
  back-to-back `Hunt`. OpenTS `code/mission.cpp`
  (`MissionClass::Assign_Mission`) shows the queue is a single
  `MissionQueue` slot — last write wins; YRpp cross-checks
  (`third_party/YRpp/MissionClass.h`: one `QueuedMission` field next
  to `CurrentMission`). Bindings confirm the call sequence
  (`src/bindings_techno.cpp`: `MoveTo` = `Destination` +
  `QueueMission(Move)`, `Hunt` = `QueueMission(Hunt)`). So Hunt
  overwrote the queued Move and reserves hunted from place instead of
  rallying to the breach. Grade: STATIC VERIFIED (OpenTS primary for
  shared engine behavior per `AGENTS.md`, YRpp for the YR binding).
- **Fix.** One call site (`main.lua` rally block): pure `MoveTo`,
  `RALLY_BREACH` diag label drops `+Hunt`. Companion fix in the same
  pass: stood-down escorts are transparent to `isOfficerAssigned` —
  otherwise the assignment memory blocks for one scan the very rally
  the stand-down released the guards for (documented intent,
  violated in practice).
- **Harness.** Officer reworked to the proximity trigger (breach
  needs a hostile inside 10 cells — the old HP-only setup could never
  fire): **23/23** (was 19/4 stale), incl. new `T7 rally MoveTo
  without Hunt` (mission tracked in the stub) and resurrected
  T8 handoff `released guards rallied to new breach`. Arbiter T1
  updated to the new contract (winner's order is MoveTo only).
  No regressions: escort 11/11, raid 12/12, belief 14/14, defense
  140/140, group 8/8, difficulty 23/23, surrender 45/45.
- **Still open.** RUNTIME VERIFIED needs one live match: rally order
  → `GetMission()` readback must show `Move`, not `Hunt`.

## [Unreleased] — Bodyguard combat + V3 kite (anti rush on the escort)

- **Problem (user report).** A V3 escort died to 3–5 tanks without a
  fight: guards were idle-only (live idle pool is 0, so the detail
  starved), a guard was forgotten the moment it engaged (`g.idle`
  retention filter), guards only ever got `MoveTo` to the V3 (stood
  next to it while it died), and the V3 itself never moved.
- **Fix (`main.lua`, escort block only; no new bindings).**
  `ESCORT_N` 2 → 3; guards retained while alive + still ours (fighters
  stay in the detail); draft prefers idle, backfills from
  marching-not-fighting (`attacking == false`; fighting `true` and
  unknown `nil` never yanked — same strictness as march recall / raid);
  follow-refresh no longer `MoveTo`-pulls fighting guards; free guards
  focus-fire the nearest hostile within 15 cells (`ESCORT_INTERCEPT`,
  150f per-guard cooldown, arbiter-claimed); a V3 with a threat inside
  12 cells kites toward its base centroid (`ESCORT_KITE`, 300f
  cooldown, claims the V3 for the tick so rally yields).
- **Harness:** new `tools/tmp/smartai_escort_test.lua` **11/11**
  (draft, focus fire on nearest, kite steps away, cooldown silence +
  re-assert, marching backfill, fighter never yanked, unknown-state
  never drafted). **No regressions:** raid 12/12, belief 14/14,
  arbiter 6/6, defense 140/140, group 8/8, difficulty 23/23,
  surrender 45/45, officer 19/4 = baseline 19/4 (stale T7/T8, proven
  identical via `git stash`), capture crashes identically pre/post
  (stale harness line 154, not this change).
- Grade: IMPLEMENTED + HARNESS VERIFIED. **Live verification pending**
  (fresh skirmish: rush an escorted V3 with 3–5 tanks → expect
  `ESCORT_INTERCEPT` focus fire + `ESCORT_KITE` in `LuaAPI.log`).

## [Unreleased] — ARCHITECTURE DECISION: Constraint Inversion principle (2026-09-26)

- **Architecture / design decision. Not a feature, not a milestone.** No code
  changed, no behaviour changed, no new gate, no new criterion. This entry
  records a positioning principle so later feature work can be reviewed
  against it.
- **What was recorded.** A new top-level section "Smart AI Principle:
  Constraint Inversion" in `scripts/mods/smart_ai/ROADMAP.md`, placed
  immediately before the existing "Design Boundaries" section so the principle
  motivates the layer boundary. The principle: SmartAI does not compete with
  Ares/Phobos by adding more static AI configuration; it uses the limitations
  of engine-level AI customization as architectural opportunities. The target
  is a smarter AI through observation, memory, decision-making, coordination,
  feedback and adaptation — not a more powerful one through larger armies,
  stronger economy or more aggressive presets.
- **Rationale recorded with it.** The engine AI's reaction vocabulary is a
  fixed set of pre-authored trigger conditions (unit/tech ownership, enemy
  power, credits, superweapon charge) evaluated per frame. It has no
  primitive that carries a fact across frames. A weight can be retuned; a
  memory cannot be expressed as a weight. Hence the inversion: because the
  engine cannot remember, arbitrate, or observe a result, the runtime layer
  must.
- **Scope limit, deliberately permissive.** The principle defines the
  architectural differentiator; it does **not** forbid static AI mechanisms.
  A feature fully expressible through INI weights, TeamTypes or
  AITriggerTypes is not by itself a SmartAI differentiator, but such
  mechanisms remain legitimate inputs. Working example: adding a
  `ThreatValues` table is not Constraint Inversion; feeding a `ThreatValues`
  lookup *into* a runtime decision system is. Retuning severity constants
  against measured live evidence is a tuning task, not a violation.
- **Not graded.** No implementation evidence is claimed or implied by this
  entry. Existing grades for M1/M2 criteria are unchanged by it.

## [Unreleased] — Surrender no longer forces the engine defeat transition (2026-09-26)

- **Behaviour change.** A house that satisfies the surrender DoD
  (`no Barracks AND no War Factory AND no MCV/CY`) is no longer passed to
  `Engine.__SmartAILose` -> `HouseClass::Lose(false)`. New module flag
  `SmartAI.SURRENDER_ENGINE_CALL`, default **false**. The call is gated,
  not deleted; the C++ bridge is untouched and still compiles.
- **Why.** Forcing `Lose()` on an AI house made the engine leave the game
  main loop ~90 frames later, at `BorrowedTime` expiry: the whole match
  ended and the process exited (code 0) with every other house alive and
  undefeated. Reproduced 3/3 in 1-human + 3-AI matches. PRE/POST
  control-flow counters prove `Lose()` returns every time and the detour
  is then never entered again (classification `HOOK_NOT_ENTERED`), so this
  is the engine switching out of the game loop, not a crash and not our
  instrumentation dying. `ShortGame` excluded by a separate `shortgame=0`
  run that terminated identically. Engine function not identified — the
  repo holds no disassembly and no xrefs. Full record:
  `FSM/HOUSE_LOSE_FORCED.md`; evidence row in `FSM/VERIFICATION.md`.
- **Unchanged:** the DoD itself, the latch, per-house order silence, the
  base-liquidation pass (`Sell`), and the HUD notice. Surrender remains a
  SmartAI *decision*; defeat stays the engine's job — which is what this
  mod's architecture says it should be. `HouseClass::Lose` was never a
  documented API and is not in `API.md`.
- **New log tag** `SURRENDER_NO_ENGINE_CALL` (replaces
  `LOSE_FALSE_CALLED` while the flag is off) so a skip is never silent.
- Harness: `tools/tmp/smartai_surrender_isolation_test.lua` 43/43.
  S1-S5 opt back in via `SAI.SURRENDER_ENGINE_CALL = true` to keep
  covering the bridge path; new **S7** is the regression test for the
  default. Regressions green: officer 93, capture 28, defense 16,
  raid 12, belief 14, arbiter 6, group 8, difficulty 12.
- Grade: IMPLEMENTED + BUILT + HARNESS VERIFIED + **LIVE VERIFIED**
  (author-confirmed fix, 2026-09-26).

## [Unreleased] — FIXED: `econSeen` namespaced per house (was OPEN BUG 2026-09-26)

- The code fix (per-house `econSeen[aiHouse][id]` stores, prune per
  house, `BeliefInspect` documents nesting) had already landed in
  `main.lua` — but the entry stayed OPEN and no regression test
  pinned it. Both closed now.
- Regression: `tools/tmp/smartai_belief_test.lua` **T11** (15/15):
  second AI house R with its own economy + anchor, hostile bait
  nearby, nothing dies, no HP moves across many scans → total
  grudge across all houses must be zero (the old flat store fired
  false `type=killed` here by design: other houses' assets missing
  from a per-house `nowSeen`).
- Grade: IMPLEMENTED + HARNESS VERIFIED (15/15).

## [Unreleased] — SmartAI per-house difficulty (Beta M1 consumer, 2026-09-25)

- `SmartAI.DIFFICULTY = "auto"` (new default): each AI house resolves its
  preset from the lobby difficulty via `house:GetAIDifficulty()`
  (LuaAPI Beta M1; "normal" → medium preset). Old DLL (no binding), nil
  or unexpected read → per-house fallback (`SmartAI.DIFFICULTY_FALLBACK`,
  "medium"). Presets cached per house for the match; cleared on restart.
  Fixed "easy"/"medium"/"hard" values still override everything
  (harness/test path).
- Presets remain behavior/tempo only (scan cadence, radii, defender
  counts, cooldowns) — no production, credits, or spawns on any level:
  the difficulty split is intelligence, not economy.
- Mixed-lobby scan gate: the tick runs at the FASTEST present house's
  cadence (`tickScanEvery`, previous-scan cache); per-house order rates
  stay per-house via each layer's own cooldowns. Consequence: slower
  houses are detected at the faster cadence, but confirm/rate gates keep
  their own preset pacing (documented in `main.lua`).
- Layer guards added: houses on flag-off presets (easy) skip
  defense/raid blocks cleanly when another house's preset enabled them
  (mixed-lobby crash found by the new harness: easy house inside the
  global defense block hit nil numeric fields).
- Harness: `tools/tmp/smartai_difficulty_test.lua` 12/12 (mapping,
  cadence, easy rally-only, nil-read fallback, override beats auto,
  restart cache rebuild, mixed-lobby gate, restart determinism).
  Regressions green: officer 93/93, capture 28/28, defense 16/16,
  raid 12/12, belief 14/14, arbiter 6/6, group 8/8.
- Grade: IMPLEMENTED + HARNESS VERIFIED. Live verification pending
  (user match with mixed lobby difficulties: expect per-house pacing
  in `[SMARTAI][ORDER]` cadence + no easy-house defense orders).

## [Unreleased] — SmartAI M2: Decision System (declared 2026-09-24)

- M2 goal declared: SmartAI becomes a decision system (sense → belief →
  options → score → commit → reassess), not a reaction system.
  Contract + Ares/Phobos boundary table (L1–L5) + architecture + checks
  C1–C6 + falsifiable acceptance in `milestones/M2_DECISION_SYSTEM.md`.
- Reuses `framework/tactical.lua` + `force_group.lua` + target_reselect
  AA/route gate (ported, no new bindings in M2). No implementation yet.
  No behavior changes. No LuaAPI milestone created (proven-gap rule).
- C3 implemented (2026-09-24): raider force from idle surplus on quiet
  home (medium/hard), economy-value targeting + outmatched retreat;
  harness 8/8 (`tools/tmp/smartai_raid_test.lua`); officer 77/77 +
  capture 28/28 green. Live verification pending.
- Surrender futility gate (2026-09-25, user ruling: surrender ⟺ cannot
  recover AND fighting on is futile): anchors-gone latch now also
  requires remaining combat value < 0.25x strongest enemy's
  (`SURRENDER_FUTILITY_RATIO`); doomed-but-fighting houses keep full
  support + throttled `SURRENDER_DEFERRED`. Harness T26 (officer
  85/85). Live feel pending (tunable ratio).

## [Unreleased] — SmartAI M1: access parity with Vanilla AI (audit phase)

- M1 goal declared: SmartAI gets access on par with Vanilla AI. No
  decision-making upgrades, no new tactics yet.
- Access audit completed (read-only): SmartAI layer/SmartAI bindings
  inventoried against Vanilla AI mechanisms (YRpp headers + project
  research). Capability-gap table in `milestones/M1_PROGRESS.md`.
- Attribution instrumentation shipped earlier (separate work):
  `[SMARTAI][ORDER/READBACK/TARGET_DIVERGENCE/CENSUS]` diagnostics;
  harnesses 50/50 officer + 28/28 capture.
- M1-A implemented: `World.GetAITeams()` (read-only team visibility,
  `src/bindings_techno.cpp` + `API.md` docs); probe mod `teams_probe/`
  (inactive by default) + `teams_probe_test.lua` 5/5; suites re-green
  (50/50, 28/28). No SmartAI behavior changes. Live verification pending
  (autonomous boot blocked — see `M1_PROGRESS.md`; needs user-run match).
- User-run full match (medium bot): M1-A/B + harvest-read **live-verified**;
  live-found bug fixed (team `id` → scan-local `index`; `API.md` updated);
  rebuild + suites green (5/5, 50/50, 28/28). No SmartAI behavior changes.
- M1-C research (spec only): `M1C_ORDER_LEASE_RESEARCH.md` — vanilla order
  lifecycle, 6 order sources, graded overwrite cases P1–P5, DU-1 lease
  contract proposal, out-of-scope list. Zero runtime changes.
- Divergence-with-cause diagnostic (read-only): `TARGET_DIVERGENCE_CTX` with
  liveness/mission/reissue evidence; officer 53/53, capture 28/28. DU-1
  remains SPEC ONLY. No orders added, no behavior changed.
- Surrender Conditions research (read-only, no impl): defeat flags/transitions
  mapped in YRpp, evaluator unmapped, power/production excluded as defeat
  inputs, edge cases graded, missing `Defeated`-read recorded, experiment
  proposed. `smart_ai/main.lua` untouched.
- Surrender implementation plan (plan only, no impl):
  `SURRENDER_IMPLEMENTATION_PLAN.md` — contract, Lose()/FlagToDie() safety
  verdict (unproven, no binding), soft-surrender design, lifecycle, test
  plan A–F (defined, not implemented).
- Surrender Phase 1 implemented: anchor tables + per-house latch (restart
  clears) + 5 order-layer gates + one SURRENDER_DETECTED line; T24 A–I
  green (officer 70/70, capture 28/28 with MCV fixtures). No Lose(), no
  new bindings, no tactics/balance changes. Live verification pending
  (needs user-run match: destroy Barracks+WF+MCV, expect exactly one
  SURRENDER_DETECTED + order silence, game continues).

## History (pre-M1, condensed from `HOW_TO_USE.txt`)

- Point defense (tier-1 `Attack` + threat-sort) after live parked-army case.
- March recall (tier-2) + broadened idle recall (all combat vehicles).
- Threat-ordered intruders; escort draft radius 25; defense-first ordering.
- Rally per-spot memory (fresh reserves only).
- Snapshot + allied cache; pcall hardening; combat-only centroid.
- Difficulty presets easy/medium/hard (`SmartAI.DIFFICULTY`, global).
- Capture-aware valuables guard (APOC/HTNK vs MIND/YURIPR/YURI).
- V3 escort + pillbox garrison-screen (experimental).
- Flank-breach rally (`MoveTo+Hunt`, Hunt-voids-MoveTo suspect open).
