# Decisions

> Lightweight architectural decision log. Record a decision only once it is actually made.
> Do not duplicate decisions already documented in `PROJECT/ROADMAP.md`, `PROJECT/CHANGELOG.md`, or `docs/ENGINEERING_LESSONS.md` — reference them instead.

## Decision format

### [DATE] — [TITLE]

Status: PROPOSED / ACCEPTED / REJECTED / SUPERSEDED

Context:
Why the decision was considered.

Decision:
What was decided.

Reason:
Why.

Alternatives considered:
What else was considered.

Consequences:
What this decision changes or constrains.

---

### 2026-09-20 — Repository is the source of truth; feature freeze until Gate 1 items close

Status: ACCEPTED

Context:
Several recent results existed only in conversational context (assistant chat
history), not in repository documents. The project workflow
(`idea → documentation → gate → source audit → implementation → harness →
live verification → evidence → showcase`) requires the repository itself to
answer what is implemented, verified, experimental, blocked, and gated. A
project-level Gate 1-3 plan did not exist anywhere in the tree.

Decision:
1. Temporary feature freeze: no new features until Gate 1 documentation
   open items in `PROJECT/GATES.md` are closed.
2. `PROJECT/GATES.md` is created as the single source of truth for the
   Gate 1 (Foundation/Reliability) → Gate 2 (Runtime Capability) →
   Gate 3 (Alpha Showcase) lifecycle, with pass/fail criteria and honest
   current statuses. Milestone sub-gates remain in `PROJECT/ROADMAP.md`;
   experiment gates stay with their experiments.
3. The next research direction — Dynamic Unit Behavior (existing units,
   runtime decisions, no new intrinsic abilities) — is documented as a
   hypothesis with a stress-tested candidate matrix and DU-1..DU-4 research
   gates in `FSM/DYNAMIC_UNIT_BEHAVIOR.md`. No implementation before the
   DU-1 capability audit.
4. Conversation-only findings must be moved into the appropriate repository
   document at session end (standing rule; first instance: the 20–30-Rhino
   attribution recorded in `FSM/VERIFICATION.md`).

Reason:
Evidence discipline degraded when the operative plan lived in chat context:
statuses could not be re-derived from the tree, and the next session could
not distinguish verified claims from in-flight ones.

Alternatives considered:
- Continue as-is with assistant context as the plan (rejected: not durable,
  not reviewable, not shareable).
- Invent a new generic process model (rejected: the workflow above is the
  project's real lifecycle; gates must represent it, not an abstract one).

Consequences:
- Gate statuses are now checkable in-tree; PASS states must not be invented.
- New experiments (including DU-2+) enter only after the Gate 1 documentation
  reset plus their own gate artifacts.
- Per-candidate Ares/Phobos stress tests are mandatory before any DU
  implementation selection.

---

### 2026-09-20 — Gate 1 item 2: bounty_hunter removed from the default active stack; archived in place as a historical reference mod

Status: ACCEPTED

Context:
`bounty_hunter` ships in `scripts/active_mods.txt` but cannot function in the
current build, for three independent reasons (all source-verified):
(1) it defines `OnPreDamage` as a mod-table method, while the current build
dispatches engine event callbacks via global lookup only (`API.md`, Callback
Model); (2) `OnPreDamage` itself is not wired (collected, never invoked —
`FSM/CAPABILITIES.md` conflict note); (3) its credit call
`house_AddCredits(ownerHouse, 50)` matches no binding (the API is the method
`house:AddCredits`), and `lastDamagedTechno.Owner` is not a Lua-exposed field.
The mod is silent in-game while being presented in-tree as the combat-economy
demo (the `+$50` README/Case-Study-2 recipe derives from it).

Decision:
1. Remove `bounty_hunter` from `scripts/active_mods.txt` (config-only; no
   behavior change — it had none).
2. Keep the mod source in the tree, marked INERT via a header disclaimer that
   records all three failure reasons and the disposition. No logic edit, no
   rewrite, no deletion.
3. Repairing it is a candidate future cleanup task (global callback +
   aftermath-polling bounty, per the proven Command Authority pattern) —
   explicitly NOT done during the Gate 1 documentation reset, since the fix
   is a code change and the reset is documentation-only.

Reason:
Criterion 1.2 (every default-stack entry functions or is documented inert with
a decision). Keeping it enabled presented a non-working mod as a feature;
keeping it in the tree preserves initial-release history without pretending it
works.

Alternatives considered:
- Fix the mod now (rejected: code change during a documentation-only reset;
  also `OnPreDamage` remains unwired, so only the aftermath-polling variant
  would work — that is a new feature, not a doc fix).
- Delete the mod (rejected: destroys the historical record the FSM archive
  references; the mod documents an era of the callback model).
- Leave enabled with only a header (rejected: criterion 1.2 wants the default
  stack to be functional or explicitly decided; a silently loaded inert mod
  fails that).

Consequences:
- The default stack is now `barrel_elevation_diag`, `command_authority`,
  `target_reselect` — all with live evidence on record.
- Case Study 2 (`PROJECT/CAPABILITIES.md`) already carries the inert/stale
  caveat; the recipe remains marked non-functional.
- If `OnPreDamage` wiring lands (post-Gate-3 decision), revisit this mod
  before any shield/bounty showcase work.

---

### 2026-09-20 — Gate 1 item 5: undocumented extras documented in API.md as an "Implemented Extras" section, graded UNVERIFIED LIVE where no consumer exists

Status: ACCEPTED

Context:
Source sweep found implemented capabilities absent from `API.md`:
`Engine.WeaponExists` (`src/lua_engine.cpp:394`), `Engine.SetHudMuted`/
`Engine.IsHudMuted` (`src/lua_engine.cpp:355/368`), the global `WeaponOverride`
table with Set/Get/Clear plus the `GetPrimaryWeapon` hook
(`src/weapon_override.cpp:163-286`), `World.GetAircraft` and
`World.GetSelectedUnits`/`GetSelectedTechnos` (`src/bindings_techno.cpp:1541/
1574/1602`). Only `World.GetSelectedUnits` has a live consumer on record
(`dynamic_objective_defense`, Gate 12.2). `Game.GetDebugHudText` was already
documented. Per the no-fake-verification rule, existence in source does not
make a capability VERIFIED.

Decision:
1. Add an "Implemented Extras (undocumented until 2026-09-20)" section to
   `API.md` covering exactly the bounded list above — the sweep is closed with
   this list, not left open-ended.
2. Grade each entry: implemented + source-verified; UNVERIFIED LIVE unless a
   live consumer exists on record; flagged as dev/diagnostic helpers first.
3. Do not promote any of them to the marketing feature list; do not build
   showcases on them before live verification.

Reason:
Criterion 1.1 (docs vs source: undocumented-but-implemented surface is a
contradiction class). A bounded, honestly-graded section closes the known gap
without pretending unknown behavior is known.

Alternatives considered:
- Mark them dev-only and leave undocumented (rejected: the contradiction
  remains — a binding that exists but is invisible is worse than one that is
  documented with an honest grade).
- Full per-binding live verification now (rejected: runtime work, out of
  scope for the doc reset; each needs its own live probe like the QueueUnit
  gate).

Consequences:
- `API.md` now matches the binding surface for the known-extras class.
- Any future consumer of `WeaponOverride`/HUD-mute/`GetAircraft` must first
  produce live evidence before claiming behavior.
- The extras section carries a "record, don't rely silently" note for future
  undocumented bindings.


---

### 2026-11-10 — Gate 1 item 1.2 reopened: reconcile the current active module stack

Status: ACCEPTED

Context:

The 2026-09-20 decision below removed `bounty_hunter` from the default stack
and recorded `barrel_elevation_diag`, `command_authority`, and
`target_reselect` as the active set. The current repository no longer matches
that state. `scripts/active_mods.txt` now contains `bounty_hunter`,
`smart_ai`, and `radiation`; `scripts/init.lua` reads this file directly,
and the root `README.md` lists the same three modules. The Bounty Hunter
source is now v3, not the inert pre-rewrite implementation described by the
2026-09-20 decision.

Decision:

1. Preserve the 2026-09-20 decision as historical context; do not delete or
   silently rewrite it.
2. Treat the current `scripts/active_mods.txt` as the source of truth for
   configured load order.
3. Reopen Gate 1 item 1.2 until all three currently active modules are checked
   against current source, harness, and live evidence.
4. Keep harness, source, and live evidence grades distinct.
5. Do not modify active module configuration as part of this documentation
   reconciliation.

Reason:

The old decision and current configuration conflict. A stale module catalog
cannot establish what the loader currently loads, and a configuration entry
alone cannot establish runtime functionality.

Consequences:

- Gate 1 is PARTIAL for the current audit state; the 2026-09-22 PASS remains
  recorded as a historical snapshot.
- `FSM/MODS.md`, `FSM/VERIFICATION.md`, and
  `scripts/mods/README.md` require synchronization with the current stack.
- FSM archive cleanup resumes only after this bounded reconciliation closes.


---

### 2026-11-10 — Remove obsolete gate-based project tracking

Status: ACCEPTED

Context:

The project has moved to SemVer-based release management. The temporary
Gate 1-3 tracking framework and `PROJECT/GATES.md` no longer represent the
project's current release model.

Decision:

1. Delete `PROJECT/GATES.md`.
2. Do not use Gate 1/2/3 as the active project lifecycle or release-control
   framework.
3. Preserve older Gate-related decisions and verification notes as historical
   records where they explain past experiments or evidence.
4. Track current work in `PROJECT/CURRENT_TASK.md`; use the project's
   SemVer/release documentation and changelog for release state and history.
5. Do not infer release readiness from the former Gate statuses.

Consequences:

The 2026-11-10 active-module reconciliation remains a documentation task,
but it is no longer a Gate 1 item. Its completion is determined by reconciling
the current configured modules with their source, harness, and live evidence.
