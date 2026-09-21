# Current Task

> Single source of truth for the task currently being investigated.
> Keep this file current: human observation → here → review → implementation → game test → update here.

## Goal

CLOSE GATE 1 (2026-09-20): resolve the six documented Gate 1 open items from
`PROJECT/GATES.md` using repository evidence only — documentation, config, and
decisions; no code, no gameplay changes.

## Scope

- `README.md` honesty pass: Alpha stage (v1.0 = historical tag), `OnPreDamage`
  marked NOT WIRED, callback model corrected to global lookup,
  `OnUnitDestroyed` `(nil,nil)` placeholder disclosed, gate/ledger pointers.
- `bounty_hunter` disposition (decision recorded): removed from
  `scripts/active_mods.txt`; kept in tree as historical reference mod with an
  inert-status header (three documented failure reasons; logic untouched;
  syntax re-validated).
- Undocumented extras (decision recorded): bounded sweep
  (`Engine.WeaponExists`, `Engine.SetHudMuted/IsHudMuted`, global
  `WeaponOverride` Set/Get/Clear, `World.GetAircraft`,
  `World.GetSelectedUnits/GetSelectedTechnos`) documented in a new
  `API.md` "Implemented Extras" section, graded UNVERIFIED LIVE where no
  consumer exists.
- `docs/TUTORIAL.md`: stale claims fixed (global callback forms; NOT-WIRED
  `OnPreDamage`; `OnUnitDestroyed` placeholder + workable alternatives;
  reliably-dispatched `Update` used for the first-mod walkthrough).
- `0xC0000005`: evidence row added to `FSM/VERIFICATION.md` (Launch-path
  stability). Criterion 1.4 closed as scoped: recommended CnCNet-spawner path
  has zero crash evidence across the documented session record; direct-Syringe
  crashes disclosed (2 logs in tree + 1 in the Gate1-plan decision doc; one
  crash without LuaAPI — root UNKNOWN, not proven LuaAPI-caused).
- Stale house cache / dead `ResetSession`: re-verified in source
  (definition `src/lua_engine.cpp:1214`, zero callers); left OPEN as a
  runtime-gated item with a concrete closure dependency written into
  `PROJECT/GATES.md`.
- `FSM/FEASIBILITY_TRIAGE.md` + `FSM/MODDB_ALPHA_RELEASE.md`: status lines
  reconciled with the above (no history rewritten; checkmarks annotated).

## Out of Scope

- No C++ changes (house-cache fix, `ResetSession` revival — runtime work,
  explicitly deferred).
- No SmartAI / CA / QueueUnit / DU changes.
- No new LuaAPI functionality.
- DU-1 audit NOT started here — it is the next task (see Next Action).

## Current Hypothesis

Not an experiment task — a governance task. After the closure audit, Gate 1
holds every criterion except 1.3 (house cache): documentation claims now match
source, the default stack is functional-or-decided, crashes/hard limits are
evidence-documented, dead experiments archived, ledger current.

## Test Scenario

1. `PROJECT/GATES.md` item table: each item shows previous status → evidence →
   action → final status, derivable from the tree.
2. Greps that reproduce the original findings now show the fixed state:
   - README: no `Production Release` claim, `OnPreDamage` section titled
     NOT WIRED;
   - `scripts/active_mods.txt`: no `bounty_hunter` line;
   - `API.md`: `WeaponExists`/`SetHudMuted`/`WeaponOverride` present;
   - `docs/TUTORIAL.md`: `OnPreDamage` sections carry NOT-WIRED warnings.
3. `ResetSession` grep still shows zero callers — honest open item, documented
   as such.
4. `lua_check` syntax pass on the edited `bounty_hunter/main.lua`.

## Expected Result

Gate 1 = PARTIAL with exactly one runtime-gated open item (1.3), everything
else closed with evidence; next task unambiguously DU-1.

## Actual Result

Matches Expected: Gate 1 🟡 PARTIAL (2026-09-20 closure audit). Items 1, 2, 4,
5, 6 closed; item 3 runtime-gated with its closure dependency recorded in
`PROJECT/GATES.md` ("Runtime verification dependency (Gate 1 → PASS)").

## Evidence

- Source checks this session: `grep SpawnUnit|QueueUnit|AddCredits
  scripts/mods/smart_ai/main.lua` → zero (prior attribution row);
  `bounty_hunter/main.lua` read in full (three failure reasons verified);
  `ResetSession` repo-wide grep → definition + 2 comments, zero callers;
  extras sweep via targeted greps of `src/lua_engine.cpp`,
  `src/bindings_techno.cpp`, `src/weapon_override.cpp`; `API.md` grep
  confirmed the extras were absent before the edit.
- Crash evidence: `syringe.log.session183{2,4}.bak` (exit C0000005),
  `docs/decisions/SUPPLY_TANK_AMMO_GATE1_PLAN.md` (3rd repro + no-LuaAPI
  crash note), `BENCHMARK.md` (0 crashes, 18k frames).
- Syntax: `tools/tmp/luabuild/lua_check.exe` → `bounty_hunter/main.lua`
  SYNTAX OK after header edit.
- Full diff list in `git status` / per-file diffs (documentation-only).

## Analysis Status

`CLOSED`

<!-- Valid values: OPEN / INVESTIGATING / READY FOR IMPLEMENTATION / BLOCKED / CLOSED -->

## Next Action

`DU-1: Dynamic Unit Behavior feasibility audit` — per
`FSM/DYNAMIC_UNIT_BEHAVIOR.md`: resolve UNKNOWN Ares/Phobos-overlap cells
against authoritative sources, confirm primitive mapping, apply the
7-question stress test to shortlisted candidates, and output one selected
unit + behavior + falsifiable success criteria. Research only; no code.
