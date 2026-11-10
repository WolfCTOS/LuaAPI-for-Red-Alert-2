# Current Task

> Single source of truth for the task currently being investigated.

## Goal

Align `PROJECT/CURRENT_TASK.md`, `PROJECT/DECISIONS.md`, and
`PROJECT/ROADMAP.md` with the SemVer model already used by the LuaAPI
implementation and project history. Documentation-only; preserve the existing
project workflow, Gates, and Milestone architecture.

## Scope

- Establish the canonical current project/API version from the implementation:
  `2.0.0` in the in-game watermark and `Engine.version`
  (`src/lua_engine.cpp`).
- Separate the public SemVer version from the project lifecycle label
  (Beta), LuaAPI milestone counters, historical Alpha milestones, and
  independent mod versions.
- Record the versioning decision in the existing lightweight decision log.
- Correct the Roadmap's version description and explain the existing
  version/milestone relationship without renumbering milestones or changing
  their status.

## Out of Scope

- No C++ or Lua implementation changes.
- No new Gates, Milestones, trackers, workflows, or project-management
  documents.
- No release creation, tag creation, or claim that `2.0.0` has been published
  as a GitHub Release.
- No edits to README.md, API.md, or CHANGELOG.md in this task, even though
  their existing version statements require a separate consistency pass.

## Version Model

- Public project/API version: `MAJOR.MINOR.PATCH`.
- Current implementation version: `2.0.0`.
- Project lifecycle stage: Beta. This label is tracked separately from the
  numeric version and does not change the version string.
- Alpha M1–M16 are frozen historical milestones.
- Beta M1+ is the independent current LuaAPI/API-framework milestone
  namespace.
- Milestone numbers do not automatically increment SemVer.
- Mod/showcase versions and milestones remain independent from LuaAPI.

## Expected Result

The three named project documents describe one consistent version model.
Historical release and milestone records remain intact. No project workflow or
scope expansion is introduced.

## Actual Result

Completed on 2026-11-10 in branch `docs/semver-alignment`.

- `CURRENT_TASK.md` now records this documentation task and its boundaries.
- `DECISIONS.md` records the canonical version and separation of SemVer,
  lifecycle stage, milestone namespaces, and mod versions.
- `ROADMAP.md` identifies `2.0.0` as the current implementation version,
  distinguishes Beta stage from SemVer, and preserves historical milestone
  numbering.

## Evidence

- `src/lua_engine.cpp`: in-game watermark and `Engine.version` both resolve
  to `2.0.0`.
- `PROJECT/CHANGELOG.md`: the 2026-09-22 entry records the project version
  change to `2.0.0` and says version references were synchronized.
- `AGENTS.md`: Alpha M1–M16 are frozen; Beta M1+ is an independent
  API/framework milestone counter; mod milestones are separate.
- The three edited files were fetched from the repository before editing and
  updated on this branch.

## Analysis Status

`CLOSED`

## Next Action

No new implementation task is selected by this documentation-only change.
Continue with the project's already-selected active task when resuming work.
