# Surrender Phase 2 — Experiment B (isolated native defeat probe)

> **RESOLVED 2026-09-26 — the recommended mechanism below is REJECTED.**
> The full Runs B/C/D record below is preserved exactly as written; it is
> the 1-vs-1 evidence that was available at the time and it could not have
> distinguished the real behaviour, because in 1v1 "match ends when the AI
> is defeated" is correct anyway.
>
> What multi-house runs added: forcing `Lose(false)` on an AI house makes
> the engine leave the game main loop ~90 frames later, at `BorrowedTime`
> expiry, and end the whole match and process (code 0) with every other
> house alive and undefeated. Reproduced 3/3. PRE/POST control-flow
> counters prove `Lose()` returns every time and the detour is then never
> entered again — classification `HOOK_NOT_ENTERED`. `ShortGame` excluded
> by a separate `shortgame=0` run that terminated identically.
>
> Consequence for Run C/D below: "Game-over with live assets + IsLoser=1 +
> expired BorrowedTime" was read as the engine ending the match on the
> timer path. That reading was directionally right and mechanistically
> wrong — the timer path is a consequence of the forced transition, not
> the cause of the match ending.
>
> `Lose` is not called by SmartAI any more
> (`SmartAI.SURRENDER_ENGINE_CALL = false`). Defeat belongs to the engine.
> Full record: `FSM/HOUSE_LOSE_FORCED.md`; evidence row in
> `FSM/VERIFICATION.md`. Run A/B/C/D status lines below are left as
> recorded.
>
> Status of the original probe: probe BUILT, runtime NOT PERFORMED (this
> environment cannot boot the game unattended — established 2026-09-23).
> NOT integrated with SmartAI. Phase 1 untouched. No permanent binding.
> Removal checklist in §6.

## 1. Static verification

All references: `third_party/YRpp/HouseClass.h` (JMP_THIS = game-thread
`__thiscall` thunk — the same pattern every bound engine call uses).

- `Lose(bool)` — `HouseClass.h:307`, `BYTE Lose(bool bSavourSomething)
  { JMP_THIS(0x4FCBD0); }`. Address PROVEN, signature PROVEN, convention
  PROVEN (`__thiscall`). Bool meaning: HYPOTHESIS ONLY ("savour" name
  inference — preserve-vs-destroy assets?). Callers: UNKNOWN (binary
  only). Side effects: UNVERIFIED.
- `FlagToDie()` — `HouseClass.h:300-303`, `{ JMP_THIS(0x4FC980); }` +
  comment "flags the house to be defeated once its borrowed time
  expires". Delayed-path HYPOTHESIS. Callers/side effects: UNVERIFIED.
- `AcceptDefeat()` — `HouseClass.h:282-283`, `{ JMP_THIS(0x4FC0B0); }`.
  Possibly the engine's own resign path (HYPOTHESIS). Callers/side
  effects: UNVERIFIED.
- Reads verified for snapshots: `Defeated/IsGameOver/IsWinner/IsLoser/
  IsResigner/IsGiverUpper/AllToHunt` plain bools (`:856-878`);
  `BorrowedTime.GetTimeLeft()` const (`Timer.h:63`, `CDTimerClass =
  TimerStruct<FrameTimer>` at `Timer.h:117`); counts via
  `BuildingClass/TechnoClass/TeamClass::Array` + `Owner` compare (patterns
  already live in `bindings_techno.cpp`). `Win/DestroyAll*/ForceEnd`
  excluded per task (no static need demonstrated).

## 2. Test matrix

| Candidate | Result | House defeated | Game continued | Side effects | Crash | Confidence |
| --------- | ------ | -------------- | -------------- | ------------ | ----- | ---------- |
| Lose(false) | AUTO-FIRED frame=900 (Run B 19:38–19:39) | IsLoser=1, BorrowedTime 90→60, Defeated stayed 0 in-window | match ended (see below), clean exit 0 | none logged | NO (reclassified, see Run B) | MEDIUM |
| FlagToDie() | NOT PERFORMED | — | — | — | — | UNVERIFIED |
| AcceptDefeat() | NOT PERFORMED | — | — | — | — | UNVERIFIED |

No fields guessed. Matrix fills only after a user-run match.

## 2b. Run B timeline (19:38:51–19:39:14, auto-fire, no F9)

BEFORE f900 (all-zero, 5 blds/16 units) → `calling Lose(false)` (no SEH) →
AFTER+0 IsLoser=1/BorrowedTime=90/assets intact/Defeated=0 → AFTER+1 (89) →
AFTER+30 (60) → Lua silence → Syringe `Done with exit code 0` at +6 s.
No AFTER+300/+900 (match over before frame 1200), no dump, no events,
no Lua errors. Countdown math: 90→0 lands ~frame 990, inside the silent
window — consistent with defeat processed at expiry, game-over screen,
user closed the finished match (user confirms: AI surrendered correctly).
Prior Run A (identical frames/values/silence) reclassified the same way:
no crash evidence in either run, only log cessation + (Run B) exit 0.

## 3. Runtime timeline

Not performed. The probe logs `[PHASE2][BEFORE]` (flags + blds/units/teams),
calls once, logs `[PHASE2][AFTER+0]`, then the Lua probe records
`[PHASE2][AFTER+1/+30/+300/+900]` via `Engine.__Phase2Read`. If the game
dies before +900, the surviving prefix lines still bracket the failure.

## 4. Safety classification

- Lose(false): UNVERIFIED. FlagToDie(): UNVERIFIED. AcceptDefeat():
  UNVERIFIED. (Never `SAFE FOR PRODUCTION` — not a production path.)
- Experiment harness itself (probe mod + temp bindings): built, compiles
  clean, harness-tested (8/8 trigger/format/latch-unavailable paths),
  inactive by default, SmartAI untouched, officer suite still 70/70.

## 5. Recommendation

Which mechanism, if any, for SmartAI Phase 2? **Undecided — TEST A
(`Lose(false)`) FIRST**, one candidate per fresh match (B/C only after A
is judged; STOP on crash/hang/unexpected game-over/human impact per the
critical rule).

Should LuaAPI expose House:Surrender() now? **NOT YET** — pending at
minimum one clean TEST A timeline with no crash and 있는 game continuation.

## 6. Cleanup verification (pending the run)

Temp footprint (complete removal = delete these, nothing else):
1. `src/bindings_techno.cpp`: `PHASE2-SPIKE-REMOVE-ME` block (helpers +
   `Phase2_Fire`/`Phase2_Read`) before `RegisterTechnoBindings`.
2. `src/lua_engine.cpp`: fwd-decl block + 6 registration lines in
   `CreateEngine` (grep `PHASE2-SPIKE-REMOVE-ME`).
3. `scripts/mods/phase2_probe/` (mod + mod.json; never added to
   `active_mods.txt` by default).
4. `tools/tmp/phase2_probe_test.lua`.
5. Rebuild Release + rerun suites after removal.
NOT YET REMOVED — removal happens after the user-run verdict, in a
separate cleanup step. Phase 1 files verified unchanged (no SmartAI edits
in this turn; `git status` below).

Final status:

```text
PHASE 2 EXPERIMENT B: PASS WITH FINDINGS (Run B: auto-fire clean, IsLoser=1,
countdown observed, match ended, clean exit 0, user-confirmed surrender.
Defeated=1 never observed; no dump (registry never applied).
Run A reclassified: same signature, no crash evidence either.)
```

## 7. Run C — full countdown to zero (20:53, probe-only mods)

Dense timeline (+1/+30/+60/+90, all observed): BEFORE all-zero (4/16) →
call clean → IsLoser=1 + BorrowedTime=90 → 89 → 60 → 30 → **0 at +90**,
assets intact throughout (4 blds/16 units at +90 — Lose(false) does NOT
destroy assets), Defeated stayed false in-window, credits ticking,
power normal. Match ended at expiry (~frame 990, ~18 s wall; user confirms
AI lost). No crash traces, no dump (registry still unapplied), Lua errors
zero. Countdown rate ≈1/frame. Game-over with live assets + IsLoser=1 +
expired BorrowedTime, Defeated never observed=1 — engine ends the match on
the timer path, not on a Defeated-flag flip (within observed window).

User-run protocol: add `phase2_probe` to `scripts/active_mods.txt` →
skirmish vs AI → press F9 ONCE mid-game (MODE=0) → play ~2 min → send
`LuaAPI.log` → remove the mod line. Fresh match per candidate; STOP rules
apply. Single-player only (MP = UNVERIFIED).

## 8. Run D — player-winner diagnostic (21:21–21:22, target_reselect + probe)

Question: does `IsWinner` ever flip on the player while the AI counts down
(missing-splash hypothesis)? Answer: NO. All 5 PLAYER snapshots
(BEFORE/+1/+30/+60/+90): `IsWinner=false`, every other flag false,
`BorrowedTime=0`, Germans economy alive (4 blds/12 units, credits ticking).
AI side identical to Run C (IsLoser=1, 90→0, assets intact, Defeated=0).
Match ended at expiry (~frame 990, ~20 s wall: boot 21:21:55 → silence
21:22:14); Syringe reports clean exit code 0 at +3 s; no dump (registry
still unapplied), no events, no Lua errors, no crash traces of any kind.
Verdict on the hypothesis: CONFIRMED as far as logs can show — the Lose()
path never sets the player winner flag, so no `AnnounceWin` taunt/splash
is ordered by the engine. Remaining UI tail (exact end-screen identity)
is unobservable from logs by construction.
