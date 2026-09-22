# 🚦 Project Gates — Lifecycle, Criteria, Status

> **Last updated:** 2026-09-20 (Gate 1 closure audit — items 1–6 resolved or explicitly gated; see item table)
> **Purpose:** single source of truth for the project-level gate model. Per-milestone
> sub-gates (Gate 10.1, 14.8, …) remain historical records in `PROJECT/ROADMAP.md`;
> experiment-specific gates live with their experiments
> (`PROJECT/SHOWCASES/adaptive_ai_gate*.md`, `FSM/QUEUEUNIT_GATE.md`). This document
> is the **project lifecycle** the workflow refers to:
>
> ```text
> idea → documentation → gate → source audit → implementation → harness
>      → live verification → evidence → showcase
> ```
>
> Evidence grades used everywhere in this file (never mixed, never upgraded by
> enthusiasm): `SOURCE VERIFIED` / `HARNESS VERIFIED` / `LIVE VERIFIED` /
> `PARTIAL` / `UNKNOWN` / `BLOCKED` / `ARCHIVED`. Full ledger: `FSM/VERIFICATION.md`.
>
> **Current mode: FEATURE FREEZE.** No new features until Gate 1 open items are
> closed (decision log: `PROJECT/DECISIONS.md`, 2026-09-20).

---

## Gate 1 — Foundation / Reliability

**Question:** is the documented project consistent, honest, and reproducible?

### Pass criteria

| # | Criterion | Pass condition |
|---|---|---|
| 1.1 | Docs vs source | Zero known contradictions between `API.md` / `README.md` / `PROJECT/CAPABILITIES.md` recipes and `src/bindings_*.cpp`. |
| 1.2 | Known active modules | Every entry in `scripts/active_mods.txt` either functions on the current build or is documented as inert with a decision (fix / drop). |
| 1.3 | Lifecycle/session risks | Stale house-userdata cache across matches (`g_houseCache`, dead `ResetSession`) resolved in code **or** disclosed with a tested workaround. |
| 1.4 | Known crashes | Intermittent `0xC0000005` retested on the recommended launch path; result documented either way. |
| 1.5 | Dead experiments identified | Every archived/removed experiment recorded in `FSM/MODS.md` with status. |
| 1.6 | Honest verification | Evidence ledger current: `FSM/VERIFICATION.md` rows match source/live reality. |
| 1.7 | Reproducibility | Harnesses in `tools/tmp/` re-runnable; live-verification protocol (CnCNet spawner path) documented. |
| 1.8 | Unsupported capabilities marked | Hard limits (no damage interception, no radar/fog, no superweapon API, …) listed publicly — `FSM/CAPABILITIES.md` UNKNOWN section. |

### Current status: 🟡 **PARTIAL** (2026-09-20 closure audit)

Closed by prior work: 1.5, 1.6, 1.8 (FSM archive); `API.md` `Attack`/
`GetMission`/`SetHealthRatio`/`OnPreDamage` corrections (see
`FSM/QUEUEUNIT_GATE.md` → Documentation Changes).

### Gate 1 item resolution (2026-09-20)

| Item | Previous | Evidence found (source-level, this audit) | Action | Final status |
|---|---|---|---|---|
 | 1. README honesty | OPEN — `v1.0.0 Production Release` / `Milestone 11` / live `OnPreDamage` / mod-table event callbacks advertised | Confirmed all four claims in the header, Key Features, damage section, Event Model; all contradict `FSM/CAPABILITIES.md` conflict note + Alpha reality | README rewritten in place: Alpha stage (v1.0 = historical tag), `OnPreDamage` marked NOT WIRED with intended-contract block, callback model corrected to global-lookup, `OnUnitDestroyed` never-dispatched status disclosed, gate/ledger pointers added. History preserved (no retraction of the M4/M6 records themselves) | ✅ **CLOSED** |
| 2. `bounty_hunter` disposition | OPEN — inert mod in default stack | Triple-dead confirmed: (a) mod-table `OnPreDamage` never dispatched (global lookup only), (b) `OnPreDamage` itself not wired, (c) independently, `house_AddCredits(...)` matches no binding and `.Owner` is not Lua-exposed — `Update` is a no-op; syntax re-validated with lua_check after the header edit | **Decision recorded** (`PROJECT/DECISIONS.md`): removed from `scripts/active_mods.txt`; kept in tree as historical reference mod with an inert-status header (logic untouched); repair = candidate future cleanup, not done in a doc-only reset | ✅ **CLOSED** (config + decision; no behavior change — it had none) |
| 3. Stale house cache / dead `ResetSession` | OPEN — Critical Blocker 1 | Re-verified in source: `ResetSession` defined (`src/lua_engine.cpp:1214`, clears `g_houseCache` via `ClearHouseCache`, closes VM) with **zero callers** in `src/` — the only references are comments (`bindings_house.cpp:46`, `event_hook.h:38`). Risk class unchanged | Documentation-only: status restated as a **runtime verification dependency** (see the honest statement below the table); not fixed (runtime work out of scope), not downgraded, not hidden | 🔴 **OPEN — RUNTIME-GATED** (criterion 1.3 unmet: neither resolved-in-code nor disclosed-with-a-*tested*-workaround) |
| 4. `0xC0000005` retest | OPEN — pending | Crash reproductions are non-repo artifacts recorded in-tree: 2 Syringe logs (`syringe.log.session183{2,4}.bak`, exit C0000005) + a third in `docs/decisions/SUPPLY_TANK_AMMO_GATE1_PLAN.md`, which also records **one crash with no LuaAPI injected** (not proven LuaAPI-caused; root UNKNOWN). Recommended CnCNet-spawner path: **zero crash evidence** across every documented live session 2026-09-09→20 + the 18k-frame benchmark | Evidence row added to `FSM/VERIFICATION.md` (Launch-path stability). Criterion 1.4 ("retested on the recommended path; result documented") closed **as scoped**: recommended path clean on record; Syringe path disclosed as an open unknown (root-cause hunt NOT a Gate-1 item) | ✅ **CLOSED (scoped)** — reopen if a crash ever reproduces on the recommended path |
| 5. Undocumented extras | OPEN — 4+ known undocumented bindings | Full sweep: `Engine.WeaponExists` (`lua_engine.cpp:394`), `Engine.SetHudMuted/IsHudMuted` (`:355/368`), global `WeaponOverride` Set/Get/Clear + `GetPrimaryWeapon` hook (`weapon_override.cpp:163–286`), `World.GetAircraft` (`bindings_techno.cpp:1541`), `World.GetSelectedUnits/GetSelectedTechnos` (`:1574/1602`); `Game.GetDebugHudText` already documented. Only `GetSelectedUnits` has a live consumer on record (Gate 12.2 showcase) | **Decision recorded** (`PROJECT/DECISIONS.md`): bounded list added to `API.md` as an "Implemented Extras" section; graded implemented + source-verified, UNVERIFIED LIVE unless a consumer exists; dev/diagnostic framing; sweep declared closed with this list | ✅ **CLOSED** |
 | 6. TUTORIAL review | OPEN — correctness unreviewed | Stale claims confirmed: table-method `OnScenarioStart` presented as working (never dispatched — same staleness class as damaged_fleet); live `OnPreDamage` examples; `OnUnitDestroyed` example implying a dispatched payload | Fixed in place (no rewrite): Step 2/4 use the reliably-dispatched `Update`; events section rewritten (global forms, NOT-WIRED `OnPreDamage`, never-dispatched `OnUnitDestroyed` + workable alternatives); Pitfall 2 + Quick Reference corrected. Tutorial examples now match `API.md` | ✅ **CLOSED** |
| 7. Conversation→repo standing rule | done for Rhino attribution | — | Standing rule enforced at session hand-off | ✅ ongoing |

> **Addendum 2026-09-21 (item 2 STALE, history preserved):** the working tree
> now contains a rewritten `scripts/mods/bounty_hunter/main.lua` (v2: ID-diff
> death polling, `MarkBounty`/`GetCost`/`GetVeterancy`, correct `AddCredits` —
> no `OnPreDamage`, no `house_AddCredits`), and `scripts/active_mods.txt`
> re-adds `bounty_hunter` (uncommitted `+bounty_hunter` vs `origin/main`).
> The "triple-dead / inert" verdict above describes the pre-rewrite mod and
> stands as history; the disposition needs a re-audit before Gate 1 can be
> re-closed. Likewise `PROJECT/GATES.md` itself is untracked vs
> `origin/main` (working-tree governance file), and `scripts/mods_archive/`
> (referenced by older records) is absent from disk — archive references
> remain historical until re-verified.

> **Addendum 2026-09-22 (item 4 REOPENED by its own rule, history preserved):**
> a `0xC0000005` reproduced ON the recommended path — CnCNet client →
> SyringeEx (`gamemd-spawn.exe` + Ares + Phobos + spawner, 2761 hooks) →
> injector OK → ~3 min match → write-AV `0xC0000005 at 0x007BA745`
> (second-chance `0x007BC806`), exit C0000005. Lua layer healthy to the
> end (no `OnTick`/SEH/CRITICAL lines; last `LuaAPI.log` 12:30:35, crash
> 12:30:37). Prime suspect (correlated, NOT proven): bounty-overlay draw
> path — faulting `EBX (0x0D5A3570)` equals the `surfP` of the active
> bounty mark (id `1047757`, coords jumping incl. negatives); our
> minidump wrote 0 bytes (dumper failure, secondary issue). The "zero
> crash evidence on the recommended path" claim above stands as history
> up to 2026-09-20; item 4 returns to OPEN until the draw path is
> exonerated or fixed and a clean session is recorded.
>
> **Resolution 2026-09-22 p.m. (item 4 re-CLOSED, scoped):** user
> live-tested the fixed build — clean session, no crash, one rectangle
> as predicted. Fix: Composite-only paint + once-per-frame guard
> (`src/barrel_pitch.cpp`, CHANGELOG entry). Session details
> (duration/map) not formally recorded — user-reported verification,
> not a protocol run. Any new `0xC0000005` reopens this item again.

### Why Gate 1 is not PASSED (honest statement)

Exactly one criterion remains unmet: **1.3**. The stale house-userdata cache
across matches (`g_houseCache` survives match 2+ in one process; `ResetSession`
has zero callers) is neither resolved in code nor covered by a *tested*
workaround. Both closure paths require a runtime session:

> **Runtime verification dependency (Gate 1 → PASS):** in one game process,
> play match 1 → exit to menu → start match 2, then either
> (a) revive `ResetSession`/per-match invalidation in code and verify no stale
> house access (code change — needs its own mini-gate), or
> (b) adopt and TEST the disclosure workaround — restart the game between
> matches — and record the test evidence. Only then does 1.3 close.

Carried note (not a Gate-1 criterion): README Installation/Quick-Start
step-by-step correctness remains flagged in `FSM/MODDB_ALPHA_RELEASE.md`
(Installation/Documentation Gate territory, Gate 3).

Everything else Gate 1 requires now holds: docs match source for the known
contradiction classes, the default stack is functional-or-decided, crashes and
hard limits are documented with evidence, dead experiments are archived, the
ledger is current, and harnesses/live protocol are reproducible.

---

## Gate 2 — Runtime Capability

**Question:** can Lua reliably observe, remember, decide, command, coordinate,
and adapt — on the live engine?

### Capability table (evidence-graded)

| Capability | Status | Evidence |
|---|---|---|
| Observe world state (queries, ownership, alliance, position, health, type) | ✅ LIVE VERIFIED | CA/RCA probes; `FSM/CAPABILITIES.md` FSM-VERIFIED section |
| Observe combat (mission/target read-back, post-hoc damage/HP-drop, disappearance diffing) | ✅ LIVE VERIFIED | `target_reselect` live 2026-09-10 (8 redirects, read-back); CA combat scans |
| Maintain runtime state across a match | ✅ LIVE VERIFIED | CA `S.*` ledger + frame-backwards restart guard; caveat: savegame-restore UNKNOWN |
| Issue unit orders (`Attack/MoveTo/Stop/Hunt`) | ✅ LIVE VERIFIED | RCA probe (`Attack`→damage→`Stop`); `SpawnUnit action="hunt"` |
| Order persistence vs vanilla AI re-selection | 🟡 PARTIAL | Redirects held across evaluation windows live 2026-09-10; the AI re-selects eventually — permanent override/lease does NOT exist. Open boundary: `PROJECT/RUNTIME_BOUNDARY.md` |
| Multi-unit coordination | ✅ LIVE VERIFIED (small scale) | Gate 14.8 showcase (4 sessions: 245 recruits, per-squad independent decisions); CA Director frontline reinforcement |
| Feedback loops (economy → decision → effect → reassess) | ✅ LIVE VERIFIED | CA directives/retaliation timing, Director income/spend, live `DIRECTOR:` lines |
| React to battlefield changes mid-frame-window | ✅ LIVE VERIFIED | Victim-centric detection (HP drop → scan → redirect), 150f cadence |

### Known capability gaps (blocking certain system classes)

| Gap | Impact | Record |
|---|---|---|
| No live damage-event interception (`OnPreDamage` never invoked) | No reactive-armor/shield class systems | `FSM/CAPABILITIES.md` conflict note |
| Production control fails live (`AI.QueueUnit` accepted-but-no-output, 2 runs) | No production-director systems | `FSM/QUEUEUNIT_GATE.md` — **BLOCKED** |
| No order-lease/override primitive | Lua orders compete with vanilla AI re-selection | `PROJECT/RUNTIME_BOUNDARY.md` "second half" |
| No production-complete / death-payload events (`OnUnitDestroyed` never dispatched) | Polling-only architectures | `FSM/FEASIBILITY_TRIAGE.md` audit |
| No radar/fog/map-extents/superweapon/alliance-switch APIs | Sector/vision/recon designs must avoid them | `FSM/FEASIBILITY_TRIAGE.md` audit |
| Savegame restore of Lua state unverified | Long-session systems | `FSM/CAPABILITIES.md` UNKNOWN |

### Current status: 🟡 **PARTIAL**

The core loop — observe → state → decide → order → read-back → adapt — is LIVE
VERIFIED in at least three independent systems (target_reselect, Command
Authority, multi_force squads — archived mod, sessions on record). The gaps above define what Gate-2 research may not
assume. Any new capability claim must land in the table above with its evidence
grade or it does not exist for planning purposes.

---

## Gate 3 — Alpha Showcase

**Question:** can an external modder install, reproduce, and see the flagship work?

### Pass criteria

Adopted verbatim from `FSM/MODDB_ALPHA_RELEASE.md` (Technical / Documentation /
Showcase / Packaging / Installation gates + zero open Critical Blockers).
Highlights:

- Flagship + 2–4 small showcases, each with honest evidence grades; screenshots +
  ≥1 gameplay video; "Why LuaAPI?" page; clean-machine install test; public claims
  match `FSM/VERIFICATION.md` (no harness→live inflation).
- **Do not invent PASS states.** The authoritative checklist lives in
  `FSM/MODDB_ALPHA_RELEASE.md` and is checked there, with evidence.

### Current status: ❌ **NOT PASSED** (components ready, gate not passed)

- Flagship **Command Authority**: implemented ✓ / harness-verified (44+36+30) ✓ /
  live-tested (two analyzed sessions + placement session) ✓ → SHOWCASE-READY
  content-wise, with documented scope notes (MP gate harness-only; Neutral
  exclusion silent-by-construction).
- Small showcases: `barrel_elevation_diag` (M16, live heartbeat; visual effect
  awaits a separate-barrel-voxel asset — `PROJECT/MILESTONE_16.md`),
  `target_reselect` (live-observed; ship as experimental).
- Missing entirely: screenshots/video, clean-machine install test, packaging
  allowlist run, README/TUTORIAL review — and all four Critical Blockers
  (`FSM/MODDB_ALPHA_RELEASE.md`) are open.

---

## Research track: Dynamic Unit Behavior (DU gates)

The active research direction. Full definition, boundary, and stress-tested
matrix: **`FSM/DYNAMIC_UNIT_BEHAVIOR.md`**. Reconciliation with the lifecycle:

```text
Gate 1 (this file)          ← prerequisite for any new implementation work
    └── DU-1 Capability audit        (research-only; may start now)
Gate 2 (this file)
    └── DU-2 One unit + one runtime behavior   (implementation experiment)
    └── DU-3 Multi-unit coordination / feedback (implementation experiment)
Gate 3 (this file)
    └── DU-4 Showcase                    (feeds the Gate 3 checklist)
```

- **DU-1 → Gate 2:** the audit output (primitive confirmation + Ares/Phobos
  classification per candidate) is exactly the Gate-2 gap analysis for this
  direction. No code.
- **DU-2/DU-3 entry condition:** Gate 1 documentation items closed (items 1–6
  resolved 2026-09-20; the runtime-gated house-cache item 3 does not block
  research — see its entry) **and** DU-1 has selected the implementation target
  from evidence. Standard per-experiment pipeline applies: hypothesis → gate →
  source audit → implementation → harness → live → evidence.
- **DU-4:** a DU-2/DU-3 success becomes a Gate-3 showcase candidate and must pass
  the same checklist as everything else.

---

## Status summary (2026-09-20, post Gate-1 closure audit)

| Gate | Status | One-line reason |
|---|---|---|
| Gate 1 | 🟡 PARTIAL | Items 1, 2, 4, 5, 6 closed (docs/config/decision); item 3 (house cache) runtime-gated — see "Why Gate 1 is not PASSED" |
| Gate 2 | 🟡 PARTIAL | Core loop live-verified; lease/production/damage-event gaps open |
| Gate 3 | ❌ NOT PASSED | Flagship ready; release checklist + media + blockers open |
| DU direction | READY FOR RESEARCH | `FSM/DYNAMIC_UNIT_BEHAVIOR.md`; next task = DU-1 audit |
