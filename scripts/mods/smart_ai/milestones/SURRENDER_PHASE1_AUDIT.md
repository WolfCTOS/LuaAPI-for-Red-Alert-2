# Surrender Phase 1 — Read-Only Audit

> **Scope:** `scripts/mods/smart_ai/main.lua` (1154 lines, current tree) +
> harnesses. No files modified by this audit (new file excepted).
> Contract under audit: no-Barracks ∧ no-WF ∧ no-MCV/CY → `surrendered[house]`,
> latched till restart, zero new gameplay orders while latched. This is a
> SmartAI design contract; Vanilla behavior intentionally out of comparison.

## 1. Verdict: `PASS WITH FINDINGS`

No BLOCKERs. The latch mechanism is structurally correct: detection reads
only the live snapshot, the latch is write-once, all five order layers are
gated, reset clears everything. Findings below are test-coverage gaps,
documented limitations, and one inherited cross-match risk — none prevent a
careful live verification run (see §12).

## 2. Detection audit (`main.lua:422-456`)

Path: per-tick `units`/`buildings` snapshot (`:338-357`, `IsAlive`-filtered
in `snapOf`) → per-AI-house counters `nB/nW/nM` → latch + one diagnostic
line + HUD alert.

1. Barracks (`ANCHOR_BARRACKS`, :120): buildings-loop only, `e.oh` match,
   no kind check needed (source array is buildings-only). ✓
2. War Factory (`ANCHOR_WF`, :126): same. ✓
3. MCV vehicles (`ANCHOR_MCV`, :132): units-loop, `k=="unit"` required
   (:439). ✓
4. Deployed CY: buildings-loop via the same `ANCHOR_MCV` table (:435). ✓
5. MCV/CY via `buildings`: YES (:435). 6. via `units`: YES, kind-gated
   (:439). Both branches present.
7. Dead objects: excluded — `snapOf` requires `IsAlive`, and both loops
   read only the current tick's snapshot. Same-tick atomicity (single
   thread, no yields between scan and check). ✓
8. Enemy-owned: `e.oh == aiHouse` required on every count (:432, :439);
   nil-owner never equals. ✓
9. Neutral: neutral houses never enter `aiHouses` (`util.is_neutral_house`,
   :383); neutral-owned assets fail the `e.oh` match. ✓
10. Cross-house: identity (`==` on house objects, mirror-country safe);
    each house counted against its own assets only. ✓
- Fail-safe handling: tables are deliberately inclusive — an unverified ID
  that matches a non-anchor building only DELAYS surrender; a missing ID
  could cause a false one. Direction verified correct. Residual: only
  NAHAND/NAWEAP/NACNST/GACNST/GAWEAP are live-verified; the rest
  ([UNVERIFIED]) can only suppress latch, never force it — safe by
  construction (see §12 protocol constraint for non-Soviet AI).

## 3. Latch audit (`surrendered`, :139)

1. Exactly once: `if not surrendered[aiHouse]` gate (:429) wraps set + log.
2. No repeats: same gate; verified live-pattern in harness (T24d counter).
3. Cannot disappear mid-match: only writer besides set is `officerReset`
   (:195), reachable solely via frame-backwards (:357). Grep-complete:
   no other writes to `surrendered` exist.
4. Cannot be overwritten: set is inside the `not`-gate; frame value is
   informational only.
5. No-anchors-from-start: latches on the first scan — contract-conformant
   (capture AM2 case demonstrated this live in harness: anchorless house
   went silent and stayed silent). Not a bug; documented contract behavior.
6. Cross-match IDs: keyed by house *object*; `PushHouse` registry is
   per-process. Same-process new match reuses objects BUT frame-backwards
   fires `officerReset` first (see §7). Save-load-backwards also resets
   (correct: earlier state may have anchors again).
7. `officerReset` clears `surrendered` (:195) alongside all 9 other states.
8. Frame-backwards sufficiency: proven from control flow (:356-358) for
   every path that lowers the counter (restart/scenario-swap/back-load).
   Residual inherited risk: a same-process new match whose counter does
   NOT go backwards would keep the latch (and all other layers' state) —
   pre-existing Gate 1 item 3 pattern (`ResetSession` dead), not introduced
   here. MEDIUM, shared with all layers.

## 4. Complete order-path inventory

Exhaustive grep for order-capable calls
(`MoveTo/Attack/Hunt/Stop/Scatter/Deploy/Unload/TakeDamage/Disable/...`,
plus `UnitController/ForceGroup/Task/Timer/EventBus`): the ONLY engine
writes in the file are `orderMove` (`MoveTo`), `orderAttack` (`Attack`),
and one direct `Hunt` (rally, paired with its `MoveTo`). No
UnitController/ForceGroup/Task/Timer/EventBus usage (only
`framework.util` is required). No Deploy/Stop/Scatter/Unload/damage calls.

```text
guard pullback MoveTo        -> gated (next_guard, :508)
point-defense Attack (tier-1)-> gated (next_defense, :582)
march-recall MoveTo (tier-2) -> gated (next_defense, :582)
escort assign/refresh MoveTo -> gated (next_officer, :766)
garrison MoveTo              -> gated (next_officer, :766)
rally MoveTo+Hunt            -> gated (next_rally, :931)
idle-recall MoveTo           -> gated (next_recall, :994)
```

No other reachable order-producing path exists. No finding: full coverage.

## 5. Same-tick ordering analysis

Order in `Update`: snapshot (:338) → aiHouses → **surrender check (:428)**
→ recon → guard → defense → officer → rally → recall → hygiene. Detection
runs strictly before every order layer every tick, so the race
(snapshot → order → evaluate) is structurally impossible; the implemented
order is snapshot → evaluate → orders. A unit that dies/loses its anchor
mid-tick cannot exist (single thread). Cross-tick lag (anchor dies after
our scan → one more tick of orders) is inherent to polling and acceptable
under the contract — INFO, not a bug. Severity: none.

## 6. State interaction analysis

- `cmdState=nil` at latch (:445): rally (the only writer, gated loop)
  cannot resurrect it for that house; escort `standDown` reads only its
  own house's entry and that escort is gated too. No stale breach priority.
- Target tracking (`seenOwner`): observation continues; drives no orders.
- Officer/defense/rally/recall memories: stale entries for a surrendered
  house persist but every refresh/re-issue site sits inside a gated loop
  (escort refresh :834+, rally re-rally, recall caps, guard refresh), so
  none can emit. Cross-house drafting is impossible (all pickers filter
  `u.oh == aiHouse`).
- In-flight orders issued BEFORE the latch stay live under vanilla (no
  revocation — `Stop` would itself be a new order). Contract requires
  silence on NEW orders only; this caveat is by design — INFO.
- Divergence/census/BASE: read-only, continue — intended (diagnostics
  must not lie after surrender).
- EventBus/Timer/Query/ForceGroup/Task: unused. Cached unit state: none
  (per-tick snapshot only).

## 7. Reset/restart analysis

`new match → surrendered = clean → orders resume` PROVEN end-to-end:
frame-backwards (:356-358) → `guardReset()` + `officerReset()` (clears all
10 states incl. `surrendered`, :185-196) → next scans re-evaluate from
zero. Harness T24i proves recovery (rally resumes post-restart). Scenario
swap == restart (same counter behavior). Back-load resets (correct:
earlier state). Forward-load impossible. New-process reload starts clean
(file-scope locals). New-House-object concern dissolved: keys are objects
and reset precedes any same-process reuse. Only residual is §3.8.

## 8. Test coverage analysis (T24a–T24i, capture 28/28)

| Test | Proves | Does NOT prove |
|---|---|---|
| T24a | no-latch with anchors; scan runs (defense order) | anchor *counts* (outcome only) |
| T24b/c | single-anchor survival (barracks, then WF) | MCV-vehicle vs CY-building equivalence |
| T24d | latch once, exact fields, latch holds | latch *frame* semantics |
| T24e/f | defense+garrison silence with pillbox/infantry/tank present | rally/escort/guard/recall silence (no breach/threat/V3/hostiles staged) |
| T24g | power ignored | — |
| T24h | credits ignored | — |
| T24i | reset clears; rally resumes | clearing of other tables (covered by older T6/T14) |
| capture MCV fixtures | A/AM2 never falsely latch; AM2 case proves latch→silence live in harness | — |

False-positive review: `surrCount` uses bracket form (echo-proof, fixed
mid-work); mock snapshots are ideal (no nil-owner/missing-pos paths
exercised — LOW generic gap); threat-sort tie-break by lowest id is
exercised implicitly (documented, deterministic).
Missing (do not implement here — report only):
- **(MEDIUM)** rally-silence under latch never staged (no breach while
  latched in any test; gate verified by inspection only).
- **(MEDIUM)** deployed-CY anchor branch (`:435`) has zero coverage
  (only SMCV-unit path exercised).
- **(LOW)** anchor-count unit test (helper inlined; values unasserted).

## 9. Live verification protocol (minimum)

Precondition: Soviet AI (all anchor IDs live-verified; otherwise confirm
Allied/Yuri IDs from `[BASE]` lines first). Match: normal skirmish.
Required log evidence per point: (1–4) `[SMARTAI][BASE]` shows
NAHAND→gone, NAWEAP→gone, NACNST-or-SMCV→gone (ids vanish; last HP if
caught); bounty `CLAIMED` corroborates unit kills only. (5) grep
`[SMARTAI][SURRENDER_DETECTED]` count == 1 with
`house/barracks=0/war_factory=0/mcv=0`. (6) grep `[SMARTAI][ORDER]` with
`owner=<house>` after latch frame → expect none (ORDER lines carry
`owner=`; recall/escort alerts carry house names). (7) CENSUS/BASE/
DIVERGENCE lines persist. (8) timing lines continue, user keeps playing
(no game-over claim possible — defeat flags unreadable). (9) no Lua
tracebacks, no crash. Impossible today: defeat-flag correlation (no
bindings); sub-scan death timing; MCV-identity beyond type strings.
Smallest diagnostic-only addition IF needed later: nothing identified —
current lines suffice for all 9 points.

## 10. Findings

- **No BLOCKERs.**
- **MEDIUM:** rally-silence-under-latch untested (inspection-only gate).
- **MEDIUM:** deployed-CY anchor branch untested.
- **MEDIUM:** same-process match without frame reset keeps latch (inherited
  Gate-1-item-3 pattern; impact here is total order silence, hence MEDIUM
  not LOW).
- **MEDIUM (protocol):** live PASS requires Soviet AI or prior ID
  confirmation — unverified IDs fail safe (no latch) but would make a live
  PASS unobservable on Allied/Yuri.
- **LOW:** mock-ideal snapshots (nil-owner/missing-pos paths untested);
  anchor counts asserted only via outcomes.
- **INFO:** in-flight pre-latch orders persist under vanilla (by design);
  anchorless-from-start houses latch immediately (contract-conformant,
  capture AM2 precedent); one-tick ordering lag inherent to polling.

## 11. File/line references

Detection `:422-456`; tables `:120-137`; state `:139`, reset `:195`;
gates `:508/:569`, `:582/:758`, `:766/:919`, `:931/:983`, `:994/:1044`;
reset path `:356-361`; order sites: guard `:549`, tier-1 `:665`,
tier-2 `:705`, escort `:834/:850`, garrison `:899`, rally `:955`+Hunt,
recall `:1025`; tests `tools/tmp/smartai_officer_test.lua` T24 (`:433+`),
`smartai_capture_test.lua` MCV fixtures; contract
`HOW_TO_USE.txt` §7; history `milestones/{M1_PROGRESS,CHANGELOG}.md`.

## 12. Ready for live verification?

```text
YES — Is Phase 1 technically ready for live verification? YES,
with the Soviet-AI protocol constraint (§9/§10-MEDIUM).
```

- Detection/latch/gates/reset verified by inspection + 70/70 + 28/28.
- Missing coverage (rally-silence, CY-branch) is test debt, not code risk:
  both gates/branches are 1–3 lines of the same proven pattern.
- No mechanism exists by which a surrendered house emits an order; the
  only open live question is anchor-ID truth for non-Soviet houses, which
  the protocol controls for.
- Unchanged since implementation: no Vanilla/target_reselect/bounty/core
  impact (diff footprint: `smart_ai/main.lua` + tests + mod docs only).
