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

Lua changes do not require a rebuild when the current runtime reloads scripts at match start.

C++ changes require:
- rebuild;
- relaunch/reinject as appropriate.

`injector.exe` must attach to `gamemd.exe`.

`RA2MD.exe` is a launcher stub.

CnCNet launches `gamemd-spawn.exe`.

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