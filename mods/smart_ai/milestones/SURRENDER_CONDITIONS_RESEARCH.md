# Surrender Conditions Research (read-only)

> **Status:** RESEARCH. No behavior changes. No API changes. No SmartAI edits
> (`smart_ai/main.lua` untouched).
> Grades: PROVEN (code/header evidence) / LIVE REQUIRED (undecidable
> statically) / HYPOTHESIS (interpretation — never presented as fact).

## Vanilla AI mechanism

Defeat in YR is per-house **flag state**, not a score. YRpp-mapped holders
(`third_party/YRpp/HouseClass.h`):

- `bool Defeated` (:856), `IsGameOver` (:857), `IsWinner` (:858),
  `IsLoser` (:859), `IsResigner` (:876), `IsGiverUpper` (:877),
  `AllToHunt` (:878)
- `BorrowedTime` (`CDTimerClass`, :891)
- Transitions with pinned addresses: `FlagToDie()` (`0x4FC980` — flags the
  house defeated once borrowed time expires), `Win(bool)` (`0x4FC9E0`),
  `Lose(bool)` (`0x4FCBD0`)
- Helper: `CountOtherUndefeatedHumanHouses()` (`0x5E2BA0`)
- Separate power model: `UpdatePower()`, `HasFullPower()`,
  `HasLowPower()` (`PowerOutput >= PowerDrain` checks) — function, not fate
- Separate production query: `CanBuild()` — capability, not defeat
- Session: `ShortGame` flag (`SessionClass.h:20`), `GameMode`

The **evaluator** (who calls `Lose()`/`FlagToDie()` and on what asset
condition) is NOT in YRpp headers — unmapped engine code. LuaAPI has zero
defeat bindings (grep-verified in `src/`).

## Proven conditions

- **P1.** Defeat is set via `Lose()`/`FlagToDie()` into `Defeated`/`IsLoser`.
  PROVEN (headers + addresses above).
- **P2.** Defeat transitions take NO power/production arguments
  (`Lose(bool)`, `FlagToDie()`). Power/production are not inputs of the
  defeat API. PROVEN (signatures).
- **P3.** Power has its own model (`UpdatePower`, `HasLowPower`) with no
  defeat linkage anywhere in the headers. PROVEN-as-header-absence
  (headers only — not a full-binary proof).
- **P4.** `CanBuild()` (production capability) and `Defeated` (state) are
  distinct class members — separate concepts by layout. PROVEN (layout).
- **P5.** LuaAPI can already census every asset class a roster check needs:
  `World.GetBuildings/GetUnits/GetAllUnits`, `GetOwner/GetKind/GetTypeName`,
  `house:GetPowerOutput/Drain`, `GetCredits`. PROVEN (`API.md` + sources).

## Live-required conditions

- Exact evaluator trigger set (which asset combinations flip `Defeated`).
- `ShortGame` effect on defeat (flag exists; semantics unmapped).
- Runtime roles of `BorrowedTime`, `IsGiverUpper`, `AllToHunt`, `IsResigner`
  (AI resign path unknown — human dialog only?).
- Evaluation cadence (per-tick vs event-driven).

## Edge cases

All verdicts NEEDS LIVE TEST; structural notes are not verdicts.

- **A. Only MCV** — undeployed MCV is kind `unit` (asset present); deployed
  CY is a Building. Verdict: LIVE REQUIRED.
- **B. Only buildings / F. No units but buildings** — building assets
  present; production may rebuild. Verdict: LIVE REQUIRED (expect alive).
- **C. Only combat units / E. No production but units / G. No buildings
  but units** — unit assets present; classic comebacks live here. Verdict:
  LIVE REQUIRED (expect alive until units gone).
- **D. Single production building** — production capability ≠ defeat input
  (P2/P4). Verdict: LIVE REQUIRED.
- **H. No buildings + no units** — canonical elimination. HYPOTHESIS
  (strong community model): this is defeat. Needs live confirmation.
- **I–M (all power variants)** — power is not a defeat input (P2/P3), so
  each power case is HYPOTHESIZED to resolve exactly like its no-power
  twin. Needs live confirmation; power still affects production/defense
  function, which is a different question.

## The "No Power + No MCV + No WF + No Barracks" case

This state removes **production capability**, not assets. Per P2/P4 there is
no mapped path from production loss to `Defeated`. What keeps the AI
playing: any surviving units (combat, harvesters, infantry) and any
remaining buildings. **Surrender does NOT follow from production loss**
(HYPOTHESIS on PROVEN premises — inference chain explicit).
If surrender was observed in such a state, the likely explanations are:
(a) remaining assets were actually zero (army died unnoticed — roster
census decides this), or (b) a resign/borrowed-time path fired.
Both NEEDS LIVE TEST. Do NOT treat "AI kept playing without factories" as
surprising — under the mapped mechanism it is the default.

## Unknowns

- Evaluator site + exact trigger set + cadence.
- `ShortGame` defeat semantics.
- `BorrowedTime` / `IsGiverUpper` / `AllToHunt` / `IsResigner` runtime roles.
- Whether AI ever resigns vs fights to the last asset.
- Whether any Ares/Phobos hook alters defeat (out of repo scope).

## Evidence

| Claim | Source | Grade |
|---|---|---|
| Defeat = per-house flags + Lose/FlagToDie/Win with addresses | `YRpp/HouseClass.h:300-312,856-892` | PROVEN |
| No power/production args in defeat API | same, signatures | PROVEN |
| Power model separate, no defeat linkage in headers | same + `:339-350` | PROVEN (headers) |
| CanBuild distinct from Defeated | layout | PROVEN |
| LuaAPI has zero defeat bindings | `src/` grep | PROVEN |
| LuaAPI can census all asset classes | `API.md` + bindings | PROVEN |
| Evaluator triggers / ShortGame / timers' roles | — (unmapped) | LIVE REQUIRED |
| H-nothing = defeat; production loss ≠ surrender | community model + P2/P4 | HYPOTHESIS |

## Candidate SmartAI implications (facts only, no implementation)

1. Defeat-*detection* (stop ordering houses with no assets; end-of-match
   handling) is expressible with existing API via per-house roster census —
   no new bindings needed for the census itself.
2. Reading `Defeated`/`IsLoser` directly needs a tiny read-only binding —
   recorded as missing, NOT implemented here.
3. Production loss and power loss must NEVER be treated as game-over by
   SmartAI layers (recall/rally keep working with the standing army) —
   follows from P2–P4.
4. M1-A team snapshots complement the census ("can it still attack?" vs
   "does it still exist?").

## Live evidence (session 00:19–00:29, user-run, Germans vs Russians)

`[SMARTAI][BASE]` tracked a full base kill live: Russians 3→34 buildings,
then collapse 34 (f27000) → 29 (f28800, first sub-100% HP: CY 998, refinery
899, laser 68) → 23 (f34200: CY/Barracks gone) → **11 buildings + 2 units,
power 0/825** (f36000). Production (CY/Barracks/Refineries/WF) gone,
power zero — game continued (log ends with match running, no game-over).
Remaining: derrick, flaks, lasers, Teslas (one 556/600), Tech Center, Iron
Curtain, missile silo + 2 units; teams declined 5→1 over the collapse.
Zero Lua errors; `target_reselect` silent; 1 SmartAI order after f28800.

Reads: (a) production loss + zero power did NOT end the game while 11
buildings + 2 units stood — live confirmation that production/power are
not defeat inputs (supports P2/P3); (b) teams kept existing until the
end (attack capability outlived production); (c) defeat flags still
unobserved (no bindings) — the H-state (zero assets) was never reached,
so the exact trigger remains LIVE REQUIRED.

## Out of scope

SmartAI surrender behavior, tactics, production logic/writes, API work of
any kind, roadmap/milestone changes, balance, Ares/Phobos, Vanilla AI
changes. Read-only experiment below is a proposal, not an implementation.

## Minimal read-only experiment (proposal only)

Per-AI-house roster census already exists (`[SMARTAI][CENSUS]`:
id/owner/type/kind) — extend NOTHING. Protocol: skirmish, reduce one AI
house through edge states A–M (by play, never by mod orders), log roster +
power + match end. Correlate zero-asset moments with match outcome.
`Defeated`-flag read stays missing (see implication 2). Not implemented.
