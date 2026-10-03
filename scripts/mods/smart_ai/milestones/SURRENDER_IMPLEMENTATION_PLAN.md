# SmartAI Surrender Implementation Plan (plan only — NOT implemented)

> **Contract (SmartAI design contract, NOT a Vanilla AI claim):**
> `No Barracks AND No War Factory AND No MCV/Construction Yard → SmartAI surrender.`
> Power, Refinery, Credits, defenses, Tech, existing units: EXCLUDED.
> Naval Yard / Airfield: excluded unless proven necessary (not proven).
> **Status:** plan only. No code, no bindings, no behavior changes.

## Contract

Production anchors = Barracks + War Factory + MCV-or-deployed-Construction-Yard.
Surrender condition (all three absent for an AI house). This contract says
when *SmartAI* gives up on a house — it does not assert Vanilla AI uses the
same criteria (research file: production loss ≠ engine defeat).

Anchor type table (TO-VERIFY live via `[SMARTAI][BASE]` lines before
implementation — do NOT hardcode from memory):
Barracks ≈ NABRCK / GAPILE? / YABRCK? (NAHAND observed live = Soviet
Barracks); War Factory ≈ NAWEAP / GAWEAP? / YAWEAP? (NAWEAP observed);
MCV ≈ AMCV / SMCV / YMCV (units) OR deployed CY ≈ NACNST / GACNST? /
YACNST? (NACNST, GACNST observed). "hasMCV" = MCV unit OR deployed CY alive.

## Existing Engine/API Mechanism

YRpp-mapped, all read-only knowledge (no calls made from LuaAPI):

- `HouseClass::Lose(bool)` (`0x4FC9E0`), `Win(bool)` (`0x4FC9E0+`),
  `FlagToDie()` (`0x4FC980`, defeat after borrowed time),
  flags `Defeated/IsLoser/IsResigner/IsGiverUpper/AllToHunt`,
  `BorrowedTime` (`HouseClass.h:300-312,856-892`).
- `src/` contains ZERO defeat-related calls (grep-verified 2026-09-24;
  hits were `lua_close`/comments only).
- Ares/Phobos ship as DLLs with no API surface in this repo — unusable
  from LuaAPI by construction.
- Existing hooks (`MainLoop/LoadString/DrawAsVXL/GetPrimaryWeapon`) give
  no defeat path; `Active_Click_With` stays disabled.

Can a specific AI house be finished through an existing safe mechanism?
**Not proven.** `Lose()` exists in headers but was never called, never
hooked, never live-tested from LuaAPI. Direct-call safety is UNKNOWN
(teardown ordering, EVA/trigger cascade, multiplayer determinism,
post-victory teardown per `ENGINEERING_LESSONS.md` §5).

## Current LuaAPI Capability

- Full roster census available: buildings by type + HP, units by type,
  owner/kind/pos (`[SMARTAI][BASE]` + `[SMARTAI][CENSUS]`), power,
  credits — the surrender CONDITION is already evaluable with zero new
  reads (anchor check = filter over the existing snapshot).
- No defeat bindings. No resign path. No `Defeated`-flag read.

## Missing Capability

```text
MISSING CAPABILITY:
A safe, proven engine path that retires one AI house on SmartAI request
(hard defeat), OR a verdict that soft-surrender (below) is the contract
implementation and no engine call is needed.

WHY NEEDED:
The contract's action ("SmartAI surrender") is undefined without it:
stop-ordering is implementable today; actual house defeat is not proven.

SAFE CANDIDATE:
1) Soft surrender first: latch per house + suppress ALL SmartAI orders for
   it (uses existing isOfficerAssigned-style gating; zero new bindings,
   zero engine risk).
2) Hard defeat via HouseClass::Lose() ONLY after a dedicated safety
   spike: call convention review, teardown-ordering analysis, determinism
   argument (all clients evaluate the same roster → same frame), single-house
   vs last-house cases, BorrowedTime vs immediate semantics.

RISKS (hard path):
- Teardown while Lua iterates (dangling userdata; post-victory ordering).
- Match-end cascade mid-tick (scenario teardown vs dispatch guard).
- Desync if any client evaluates differently (observer-only today, but
  must be proven, not assumed).
- EVA/trigger side effects (IsResigner dialog paths are human flows).
- Precedent: detour fault domain (§9) — new native surface needs isolation
  runs (A-E style) before trust.
```

No new `House:Lose()` binding is created by this plan. Separate decision required.

## Proposed Implementation (when approved — NOT now)

1. Anchor filter over the existing snapshot (no new reads): per AI house,
   `hasBarracks/hasWF/hasMCV` from alive building/unit types (verified IDs).
2. Latch `surrendered[house] = {frame}` on first all-absent scan.
3. While latched: skip the house in EVERY order layer (single gate check,
   same pattern as `isOfficerAssigned`); keep observing (census continues).
4. Unlatch ONLY if an anchor reappears (rebuild/recapture/MCV redeploy)
   AND `unlatchGrace` frames pass without re-loss (hysteresis against
   build-flicker); log latch/unlatch with frame + missing set.
5. Hard-defeat call: only the MISSING-CAPABILITY spike above, separate go-ahead.

## Lifecycle / State

- Check cadence: existing scan tick (30f medium) — no new timer.
- First trigger: latch + one log line (`[SMARTAI][SURRENDER] house=… missing=…`).
- No re-surrender: latched houses skip order layers entirely (idempotent).
- Match reset: latch cleared in the existing restart path (`officerReset`
  family — same place that clears defense/rally/recall state).
- Anchors return: unlatch with grace (see §4 above); orders resume.
- Post-surrender: zero SmartAI orders for that house (observation continues
  so unlatch stays possible). No building/unit writes (no selling, no
  suiciding, no Stop spam — surrender is silence, not sabotage).

## Safety

- Soft path: no engine writes beyond the already-used order suppression
  (which is the ABSENCE of orders); cannot crash, cannot desync (pure Lua
  predicate over deterministic snapshot).
- Anchor IDs verified from live `[BASE]` lines before coding (never from
  memory); unknown-house fallback = no surrender (fail-safe: instransitive
  houses and Special/Neutral stay excluded as today).
- Hard path: blocked on the safety spike; default OFF even if researched.

## Test Plan (defined now, NOT implemented)

Harness (stub roster per case, deterministic):
A. Barracks gone, WF + MCV live → no surrender.
B. WF gone, Barracks + MCV live → no surrender.
C. MCV gone, Barracks + WF live → no surrender.
D. All three absent → surrender (latch + order silence + log line).
E. No power, all three live → no surrender.
F. Production absent, defenses/tech standing → surrender (contract: anchors
   only; defenses/tech excluded by design).
Live protocol (later): skirmish, strip one AI house to D-state by play,
expect latch line + order silence for that house + continued orders for
other houses; then rebuild one anchor → unlatch line.

## Out of Scope

Tactics, balance, re-issue/lease logic, production writes, formation API,
Vanilla/Ares changes, resign-dialog flows, superweapons, navy/airfield
criteria, roadmap/milestone changes, DU-1 implementation.
