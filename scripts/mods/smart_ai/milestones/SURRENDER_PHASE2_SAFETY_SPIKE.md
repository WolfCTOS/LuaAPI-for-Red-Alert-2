# Surrender Phase 2 — Safety Spike (research only, no runtime changes)

> **OUTCOME 2026-09-26: `Lose(bool)` is REJECTED as a surrender mechanism.**
> The reasoning in this document was sound but the decisive evidence was
> missing: everything here was derived from headers and comments, and the
> only runtime data available at the time was 1-vs-1, where "match ends
> when the AI is defeated" is the correct behaviour anyway and therefore
> cannot distinguish the hypotheses.
>
> Multi-house runtime evidence settled it: forcing `Lose(false)` on a
> non-player house makes the engine leave the game main loop ~90 frames
> later, at `BorrowedTime` expiry, ending the whole match and process
> (code 0) with every other house alive and undefeated. Reproduced 3/3.
> `ShortGame` excluded by a separate `shortgame=0` run.
>
> Two specific cautions from this document are now vindicated and should
> be preserved as guidance:
>   * §1 `Lose(bool)` "Semantics: HYPOTHESIS ... name inference ONLY" —
>     the bool was never reverse-engineered. Still true.
>   * §2 "`DestroyAll*()` standalone ... zombie-house risk" — the general
>     lesson (do not force engine state transitions from outside their own
>     evaluator) is exactly what bit here.
>
> Also note §5 listed `FlagToDie` as preferred over `Lose` "pending B".
> That preference was never tested and is now moot: neither
> `FlagToDie` nor `Lose` should be called from Lua. Do not "fix" this by
> trying `FlagToDie` — that is the same class of external state forcing.
>
> The document body below is preserved as written. Full record:
> `FSM/HOUSE_LOSE_FORCED.md`; evidence row in `FSM/VERIFICATION.md`.
> SmartAI default is now `SmartAI.SURRENDER_ENGINE_CALL = false`; defeat
> is left to the engine.
> Phase 1 (detect + latch + order silence) is LIVE VERIFIED and stays
> untouched by this document. This spike investigates ONLY the engine
> defeat mechanism for a possible future Phase 2. Nothing implemented,
> nothing called, no bindings created.
> Grades: PROVEN / UNVERIFIED / HYPOTHESIS / NOT TESTED.

## 1. Engine mechanism

All references: `third_party/YRpp/HouseClass.h` (JMP_THIS = game-thread
thiscall wrappers; addresses are gamemd 1.001 pins).

### `Lose(bool)` — `0x4FCBD0`

- Reference: `HouseClass.h:307`.
- Callers: UNKNOWN from repo (engine binary only; no source, no PDB, no
  Ares/Phobos sources in tree).
- Semantics: HYPOTHESIS — full house-defeat transition (sets
  `Defeated`/`IsLoser`, asset teardown, score/trigger cascade, game-over
  participation). The `bool bSavourSomething` likely controls whether
  remaining assets are preserved vs destroyed (name inference ONLY —
  must be verified, never assumed).
- Side effects: UNVERIFIED (teardown set, EVA/score, triggers, MP sync —
  none observable from repo).
- Confidence: LOW (address + signature PROVEN; everything else open).

### `FlagToDie()` — `0x4FC980`

- Reference: `HouseClass.h:300-303` + YRpp comment: "flags the house to be
  defeated once its borrowed time expires, unless already flagged to
  win, lose or die".
- Callers: UNKNOWN from repo.
- Semantics: HYPOTHESIS — delayed/gentle defeat (flag now, engine
  completes later via `BorrowedTime`). Gentlest candidate ON PAPER ONLY.
- Side effects: UNVERIFIED.
- Confidence: LOW (comment + signature PROVEN).

### `Win(bool)` — `0x4FC9E0`

- Reference: `HouseClass.h:305`.
- Semantics: declares the house WINNER — wrong direction for surrender.
- Verdict: do NOT use for Phase 2. (Listed so nobody "discovers" it later.)

### Adjacent calls (mapped, same caveat class)

- `AcceptDefeat()` `0x4FC0B0` — HYPOTHESIS: resign-pipeline entry (what the
  engine itself calls on resign). Interesting precisely because it may be
  the engine's own safe path — UNVERIFIED, needs the same spike treatment
  before any use.
- `DestroyAll()` `0x4FC6D0` ("every matching object takes damage and
  explodes"), `DestroyAllBuildings()` `0x4FC790`, NonBuildings(Non)Naval
  variants — asset destruction WITHOUT proven state transition. Using them
  standalone risks "dead house that is not defeated" (worst outcome:
  zombie state). UNVERIFIED as defeat path.
- `ForceEnd()` `0x4FCDC0` — name suggests match/script end; semantics
  UNKNOWN; do not touch without dedicated research. UNVERIFIED.
- `CountOtherUndefeatedHumanHouses()` `0x5E2BA0` — read-only helper,
  plausibly part of the game-over check. Safe-looking READ candidate for
  a future spike (still needs verification it has no side effects).
- Flags `Defeated/IsGameOver/IsWinner/IsLoser/IsResigner/IsGiverUpper/
  AllToHunt`, timer `BorrowedTime` (`:856-892`) — state exists, unreadable
  from Lua today (recorded missing reads).

## 2. Safety analysis

| Candidate | Rating | Evidence |
|---|---|---|
| `Lose(bool)` direct call | UNVERIFIED | Address+signature PROVEN; callers, bool meaning, teardown set, MP safety all open |
| `FlagToDie()` direct call | UNVERIFIED | Comment+suggests delayed path (gentlest HYPOTHESIS); same opens as Lose |
| `Win(bool)` | UNSAFE (for surrender) | Wrong semantics by definition |
| `AcceptDefeat()` | UNVERIFIED | Possibly the engine's own resign path (HYPOTHESIS); needs equal spike |
| `DestroyAll*()` standalone | UNVERIFIED, suspected harmful | No state transition proven; zombie-house risk |
| `ForceEnd()` | UNVERIFIED, do not touch | Semantics unknown |
| TakeDamage-driven natural defeat | CONDITIONALLY SAFE (as experiment) | `ReceiveDamage` pipeline is bound + live-grade; deterministic given same inputs; destructive by design (test matches only) |

Calling-convention/object-lifetime/SEH/MainLoop-context: all PROVEN-safe
patterns already exist in `src/` (thiscall wrappers, array-membership
validation, tiny `__try` helpers, game-thread dispatch). What is NOT proven:
reentrancy of the defeat cascade (object teardown under Lua's feet —
after such a call Lua must touch NO cached userdata), teardown ordering vs
the dispatch guard (`ENGINEERING_LESSONS.md` §5), determinism across CnCNet
clients/observers, interaction with Ares/Phobos hooks on the defeat path.

## 3. Recommended experiment (designed, NOT performed)

Order: A first; B only if A cannot answer.

**Experiment A — natural defeat via existing `TakeDamage` (zero new code).**
Disposable TEST match (never a real one): temp probe mod (removed after)
issues massive `TakeDamage` to every asset of ONE AI house (snapshot roster
from `[SMARTAI][BASE]` + `[SMARTAI][CENSUS]` as the target list, refreshed
per scan until empty), then observes: roster→zero, teams→gone, score/EVA
behavior, game continuation, no crash, human player untouched (AI-only
targeting; match outcome affected BY DESIGN — that is the observation).
Answers whether the natural path completes and what it looks like from
Lua-visible state. Cannot answer flag-level questions (no read bindings).

**Experiment B — temp Lose/FlagToDie hook (code, described not implemented).**
IF A is inconclusive: test-build-only `Debug_HouseLose(houseIdx, mode)`
behind an explicit debug flag + temp read-back of
`Defeated/IsLoser/IsWinner/IsGameOver/IsResigner/IsGiverUpper/BorrowedTime`
in the SAME hook (read-only ints). Call ONCE on a Phase-1-latched AI house
in a disposable match. Full before/after matrix per the observation list.
Remove the hook after the verdict. Do NOT wire to SmartAI, do NOT ship.

## 4. Runtime result

NOT PERFORMED. No results fabricated. This section fills only after A/B run.

## 5. LuaAPI integration recommendation

```text
Should LuaAPI expose a surrender/defeat binding? NOT YET.
```

Minimum API surface IF ever approved (post-spike): `house:Surrender()`
(no args; FlagToDie-path preferred over Lose pending B) + read-only
`house:IsDefeated()`. No `Win()`, no bool params, no asset-destruction
primitives. Blocked on: Experiment A/B verdict + MP-safety proof +
teardown-ordering review.

## 6. Phase 2 implementation boundary

- PROVEN: signatures/addresses/flags above; TakeDamage pipeline live-grade;
  Lua per-frame determinism model; Phase-1 latch (unchanged, LIVE VERIFIED).
- UNVERIFIED: all side effects/callers/bool semantics of
  Lose/FlagToDie/AcceptDefeat; MP/CnCNet safety; teardown ordering;
  Ares/Phobos interaction; `ShortGame`/mode dependence.
- NOT TESTED: everything runtime (no experiment performed in this spike).
