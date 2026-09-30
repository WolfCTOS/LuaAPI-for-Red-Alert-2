# AGENTS.md

LuaAPI: a native x86 Lua 5.4 runtime injected into Yuri's Revenge 1.001 (`gamemd.exe`). C++ handles engine integration/safety; Lua controls gameplay.

Before touching hooks, engine integration, or combat systems, read:
- `docs/AGENTS.md`
- `docs/ENGINEERING_LESSONS.md`
- `API.md`

---

## Project methodology

The repository is the source of truth for the current implementation.

Do not invent or change project methodology unless explicitly instructed by the user.

### Gates

The project uses exactly THREE development Gates.

1. Implementation
2. Verification
3. Close

Do NOT:
- create additional Gates;
- split one Gate into multiple Gates;
- rename Gates;
- merge Gates;
- invent new Gate numbers;
- turn individual bugs, subsystems, checks, or implementation steps into Gates.

Technical subtasks remain inside the current Gate.

A Gate is a project-level completion boundary, not a checklist item.

If a task has many technical steps, organize them as subtasks/checks inside the current Gate rather than creating new Gates.

### Milestones

Milestones are defined by `PROJECT/ROADMAP.md`.

Do not invent, rename, split, merge, or advance Milestones without explicit instruction.

Do not modify the Roadmap merely to make the current implementation appear complete.

A Milestone or Gate may only be considered complete when its stated acceptance criteria are actually satisfied.

### Milestone Architecture

LuaAPI project milestones describe ONLY the LuaAPI/API framework.

Current namespace (see `PROJECT/MILESTONE_NAMESPACE.md`):

- Historical Alpha: Alpha M1–M16 (frozen; M15 reserved but never defined).
- Current Beta: Beta M1+ (independent counter; API/framework work only).

Mods and showcase projects (SmartAI, Bounty Hunter, Target Reselect, other
gameplay mods) are independent development projects. Each may have its own
milestones, CHANGELOG, status, and roadmap. Their milestones must NOT be
merged into the LuaAPI milestone namespace.

```text
LuaAPI
  ↓
Mod / Showcase
  ↓
Gameplay behavior
```

A mod problem does not automatically become a LuaAPI milestone. Decision rule:

```text
Mod problem
  ↓
Does the mod need an API/framework capability that does not exist?
  ├─ No  → keep it in the mod's own milestone/changelog.
  └─ Yes → investigate the API gap → proven gap → LuaAPI Beta milestone may be created.
```

Example: `SmartAI Mx: investigate offensive coordination` stays SmartAI work
unless the investigation proves a missing LuaAPI capability, which then
becomes a separate `Beta My` API item.

SmartAI is a consumer/reference implementation of LuaAPI, not part of the
LuaAPI milestone system: SmartAI bugs, balancing, tactics, research, and the
SmartAI Attribution Audit belong to SmartAI. Only actual API/framework
changes belong in LuaAPI milestones. Never create a LuaAPI Beta milestone
merely because a mod has a bug, limitation, or interesting idea.

Preserved rules: Alpha history is frozen (no renumbering); Beta starts at
independent Beta M1; Gates are a separate numbering system; historical names
such as M8 "Beta Hardening" remain historical and do not represent the
current Beta namespace.

---

## Evidence rules

Separate:

- IMPLEMENTED — present in source code;
- BUILT — successfully compiled;
- STATIC VERIFIED — behavior supported by code inspection;
- RUNTIME VERIFIED — behavior observed during an actual game run;
- DOCUMENTED — recorded in the repository.

Do not treat one category as another.

In particular:

Code existing does NOT prove runtime behavior.

Compilation does NOT prove gameplay behavior.

A previous runtime log does NOT verify code written after that log.

If runtime verification is required, use a fresh runtime log produced by the current build.

When evidence is insufficient, report:

`INCONCLUSIVE`

Do not infer PASS from missing errors alone when positive runtime evidence is required.

---

## Agent autonomy

The agent may:
- inspect the repository;
- inspect git history when useful;
- diagnose bugs;
- implement requested changes;
- build the project;
- run available verification procedures;
- inspect runtime logs;
- update documentation when implementation/status actually changes.

The agent must NOT:
- invent project methodology;
- create additional Gates;
- invent Milestones;
- silently redefine acceptance criteria;
- mark unverified behavior as verified;
- rewrite roadmap status to match assumptions;
- remove inconvenient findings from documentation;
- make architectural claims without inspecting the relevant implementation.

When methodology is ambiguous, preserve the existing methodology instead of creating a new one.

---

## Repo layout gotcha

The repo root IS a full Yuri's Revenge 1.001 install (`gamemd.exe`, `.mix`, `.mmx`, etc.).

Those game assets are gitignored; don't confuse them with source.

Only these directories contain project source/configuration:
- `src/`
- `include/`
- `scripts/`
- `docs/`
- `PROJECT/`
- `tools/`
- `third_party/`

The CMake build auto-deploys `LuaAPI.dll` + `injector.exe` to the repo root (the game directory).

A fresh build overwrites the DLL/injector already in the game folder.

If the `injector` deploy step fails with "Permission denied", a launcher
(`injector.exe`) is still running and holding the file. Stop it
(`Get-Process injector | Stop-Process -Force`) and rebuild — do not ask the
user to close it.

---

## Build

- MSVC only.
- Configured `build/` uses VS generator, `Win32` (32-bit x86), C++20, static `/MT`.
- Build:

`cmake --build build --config Release`

Initialize git submodules first:

`git submodule update --init --recursive`

MinHook IS used and linked. Ignore the stale "no longer linked, retained for reference" comment in `CMakeLists.txt`.

There is no unit test framework.

Primary verification is in-game:
- run `injector.exe`;
- launch Yuri's Revenge;
- inspect `LuaAPI.log`.

`LuaAPI.log` is written next to the DLL in the repo root.

Trace logging:
- `LUAAPI_TRACE=1`
- or `LUA_TRACE_FRAMES`

Default log level is `info`.

Do not commit generated binaries/DLL/injector.

---

## Run / iterate

Lua changes do not require a rebuild, but they take effect only in a FRESH
game process: scripts load ONCE per process (`std::call_once` in
`src/lua_engine.cpp`) — there is no per-match script reload, and
`ResetSession()` has no callers. Restart the game to pick up Lua edits;
expect cross-match staleness in one process (Gate 1 item 3).

C++ changes require:
- rebuild;
- relaunch/reinject as appropriate.

`injector.exe` must attach to `gamemd.exe`.

`RA2MD.exe` is a launcher stub.

CnCNet launches `gamemd-spawn.exe`.

---

## Orchestrate the engine; do not re-create it

**Don't recreate an engine capability if you can orchestrate the engine to
perform it natively.**

The engine already implements its own features. If a unit, weapon, warhead or
timed event can be made to do the work, make the engine do it — do not build a
parallel implementation of the object in LuaAPI.

Why, in this repo specifically: a hand-built `RadSiteClass` produced by
`World.RadSiteCreate` renders correctly for one frame and then faults at
`0x71C9E0AA`. That address is **Phobos.dll** (RVA `0x6E0AA`), not the engine:
gamemd spans `0x00400000`..`0x00B93000`. The faulting walk evaluates
`P1 = elem->obj` (null-checked) then `P2 = P1->+0x18` and `P3 = P2->+0x10`
(both unchecked) and finally reads `P3->+0x90`; the observed fault address
`0x90` with `EDI=0` pins the null to `P3`. So the object was the right class
and the right colour — the engine paints its own sites vanilla green, confirmed
with no tint written at all. What failed was a third-party consumer of the
object, not the way it was built.

Two practical consequences:

- A disarmed run never faults. Creating no site produces no crash, so the
  fault genuinely requires our object to exist.
- `greenCap=1` still faults, so it is not a volume problem. One site is enough.

Verify module ownership before blaming the engine: `LogModuleMap` in
`src/dllmain.cpp` logs base/size/end for every loaded module, and a fault
address can be attributed against it. SyringeEx owns the unhandled-exception
filter, so our own `CrashFilter` does not run and its dumps carry no module
table.

Order of preference:

1. **Orchestrate** — spawn/order engine objects so the engine's own code path
   produces the effect. (`STRIKE`: spawn a Desolator, order it to fire; the
   engine builds the RadSite and paints the green itself.)
2. **Parameterise an engine object** — if orchestration is blocked, set fields
   on an object the engine already owns rather than constructing one. Identify
   such objects by verifying a signature (a vtable), not by a fixed offset.
3. **Only then** consider building the object, and only with live evidence of
   what is missing.

Before choosing (2) or (3), check whether (1) is blocked by a *condition*
rather than by design — e.g. `House.SpawnUnit` refusing a type the player's
house cannot own is a side-selection problem, not an architecture problem.

If a native path is genuinely unreachable, say so plainly and name the missing
API. Do not accumulate speculative workarounds in its place.

---

## Hard MSVC constraint

A function containing `__try/__except` cannot also contain C++ objects with destructors because of C2712.

Keep `__try` in tiny helper functions.

Never let a C++ exception cross an SEH frame.

See:
- `logger.hpp`
- `event_hook.cpp`

Validate engine objects before dereference:
- `nullptr`
- `WhatAmI()` RTTI
- `IsAlive`
- `Health > 0`
- `InLimbo`
- SEH where required

The engine frees objects aggressively. Cached pointers may become invalid within a few frames.

---

## Hooks & combat traps

`FootClass::Active_Click_With` at `0x004D74E0` is hard-disabled (`kDisableActiveClickHook = true`).

Do not re-enable it without a new safe interception point.

See `docs/ENGINEERING_LESSONS.md` §9.

Verified addresses for gamemd 1.001:
- MainLoop `0x0055D360`
- StringTable::LoadString `0x00734E60`

Byte-signature checks are non-blocking. A mismatch is WARN only; hook installation still proceeds unless `MH_CreateHook` fails.

SpawnManager rewrites spawned-missile targets each frame.

RocketLocomotor precomputes its ballistic spline at spawn.

Use `Force_Immediate_Destination` for in-flight redirect.

---

## Multiplayer determinism

MainLoop can execute multiple times per logical frame.

Gameplay logic is gated by `Unsorted::CurrentFrame` and must execute once per logical frame.

Do not use:
- `os.time()`
- `os.clock()`

for gameplay decisions.

Coordinates:
- 1 cell = 256 leptons;
- squared distances can overflow signed 32-bit;
- use 64-bit arithmetic where required.

---

## Mods

Mods live in:

`scripts/mods/<id>/main.lua`

Active IDs are listed one per line in:

`scripts/active_mods.txt`

`#` starts a comment.

Load order matters.

For same-frame writes, later-loaded mods can overwrite earlier writes.

Do not assume two mods are independent when they operate on the same unit pool or engine state.

Before changing mod behavior, inspect the active mod list and relevant neighboring mods.

---

## Runtime verification

When a task explicitly requests live-match verification:

1. Build/reload the current implementation as required.
2. Start a fresh game/match.
3. Produce a fresh runtime log.
4. Verify the requested runtime behavior against that log.
5. Report each acceptance criterion as:
   - PASS
   - FAIL
   - INCONCLUSIVE
6. Include concrete log evidence.

Do not use an old log to verify code changed after that log was produced.

For Smart AI specifically, distinguish:
- heartbeat;
- decisions;
- deaths;
- ForceGroup state;
- Force Lost;
- EventBus activity;
- Timer execution;
- Query execution;
- `Framework.update`;
- framework errors.

---

## Truth sources

For implementation:
- actual source code is authoritative.

For project status:
- `PROJECT/ROADMAP.md`
- `PROJECT/CHANGELOG.md`

For API behavior:
- `API.md`
- `PROJECT/CAPABILITIES.md`

For engine behavior (how `gamemd.exe` works internally):
- OpenTS (`OpenTS-Developers/OpenTS`, TS 2.03 reconstruction) is the ONLY
  valid primary reference. Before any manual byte-level RE of engine
  mechanics, check OpenTS source first — names, tables, formulas and the
  render path are there (`code/cell.cpp`, `code/display.cpp`,
  `code/map.cpp`, `code/tactical.cpp`, `code/techno.cpp`). Proven 2026-09-28:
  a full day of manual RE re-derived `Cell_Shadow`, `Encroach_Shadow` and the
  sight formula that were all present in OpenTS (`docs/research/SHROUD_RCA.md`
  §9). Manual disassembly is only for the YR-fork delta (what RA2 changed:
  counters replacing per-house sets, `arg8`, dead regions like `0x577C88`),
  never for re-deriving shared engine behavior from zero. YRpp headers remain
  an address cross-check for YR-specific bindings, not a behavior authority.

Do not rely on stale status sections in secondary documentation when they conflict with the Roadmap/Changelog or current source.

When documentation conflicts with source, report the discrepancy instead of silently choosing whichever is convenient.

---

## Documentation discipline

Update documentation only when the implementation or verified project status actually changed.

Do not rewrite historical claims merely because they are inconvenient.

When runtime verification disproves a documented assumption, preserve the finding and update the documentation to distinguish:
- previous assumption;
- actual implementation;
- runtime evidence.

Never modify documentation solely to make a Gate or Milestone appear complete.

---

## API surface discipline

For LuaAPI — No consumer + no unique capability + existing engine alternative = candidate for removal.

A binding (or module) becomes a removal candidate only when all three hold:

1. **No consumer** — zero live users in `scripts/` (verified by repo-wide grep, not by memory);
2. **No unique capability** — it unlocks no runtime decision that INI/Ares/Phobos cannot express (see `PROJECT/RUNTIME_BOUNDARY.md`; DU-1/M14.2 classification);
3. **Existing engine alternative** — the same outcome is reachable via engine verbs, INI mechanics, or remaining bindings.

Candidate ≠ deleted: removal still requires the standard pipeline (hypothesis → source audit → harness/live check for regressions → evidence → docs banners with history preserved). Precedent: Milestone 10 removal (2026-09-21).