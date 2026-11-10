# Current Task

> Single source of truth for the task currently being investigated.
> Keep this file current: evidence → review → implementation → verification → update.

## Goal

Reconcile the current active-module configuration with the SemVer release
model, project decisions, FSM catalog, and verification records.

## Scope

- Treat `scripts/active_mods.txt` as the active module list consumed by
  `scripts/init.lua`.
- Verify the current active modules in load order: `bounty_hunter`,
  `smart_ai`, `radiation`.
- For each module, compare current source, current harness, and live evidence.
  Keep those evidence grades separate.
- Reconcile `PROJECT/DECISIONS.md`, `FSM/MODS.md`,
  `FSM/VERIFICATION.md`, and `scripts/mods/README.md` with the current
  SemVer release model.
- Preserve historical decisions and test results as history; add explicit
  superseding records rather than silently rewriting them.
- Only after the current module inventory is consistent, resume FSM archive
  cleanup.

## Out of Scope

- Do not change `scripts/active_mods.txt` or module load order.
- No gameplay/code changes.
- No new LuaAPI functionality.
- Do not promote harness evidence to live verification.
- Do not infer current behavior from stale documentation.

## Findings

- `scripts/active_mods.txt` currently contains `bounty_hunter`,
  `smart_ai`, `radiation`.
- `scripts/init.lua` reads that file and loads each listed module.
- Root `README.md` agrees with the same three-module default stack.
- `PROJECT/DECISIONS.md` contains a 2026-09-20 decision removing
  `bounty_hunter`, now superseded by the current configuration and source.
- `FSM/MODS.md` and `FSM/VERIFICATION.md` still call Bounty Hunter inert.
- `scripts/mods/README.md` has a stale module list and describes Bounty
  Hunter as v2; current source identifies itself as v3.
- Radiation has a mod-local runtime findings record. Bounty Hunter's
  available harness identifies itself as v2, not the current v3 source.
  SmartAI's current behavior also needs evidence-grade reconciliation.

## Current Hypothesis

The active configuration is authoritative for what the loader attempts to
load. It does not by itself prove that every listed module works. Gate 1
item 1.2 must remain open until each current active module has a source,
harness, and live-evidence status that agree with the current implementation.

## Expected Result

- The three active modules are documented accurately and in load order.
- Historical decisions remain visible but are explicitly superseded where
  applicable.
- Each current behavior has an honest evidence grade.
- Gate 1.2 closes only when every active module is functional on the current
  build or has an explicit inert disposition and decision.

## Actual Result

Audit in progress. The active-module reconciliation was opened on
2026-11-10 and is tracked here with supporting evidence in the module records.

## Analysis Status

`INVESTIGATING`

<!-- Valid values: OPEN / INVESTIGATING / READY FOR IMPLEMENTATION / BLOCKED / CLOSED -->

## Next Action

Audit `bounty_hunter` v3 against its live records and determine whether a
current-version harness exists; then reconcile SmartAI and Radiation records.
