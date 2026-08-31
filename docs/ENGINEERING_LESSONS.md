# Engineering Lessons — Building a Scripting Runtime for Yuri's Revenge 1.001

A technical retrospective on LuaAPI: what the YR engine actually is, which
approaches failed, why they failed, and what the final architecture looks like.
Written for future maintainers and anyone extending similar 2000-era Win32 games.

> **Evidence policy:** Historical hypotheses that were later disproven are explicitly marked as such. The final verified conclusion always takes precedence over an earlier failed hypothesis.

---

## 1. The Engine: Yuri's Revenge 1.001 (`gamemd.exe`)

- A closed-source 32-bit x86 binary (Westwood, 2001). **No plugin API, no
  scripting runtime, no official extension points.** All classic "modding" is
  INI data expansion (`rulesmd.ini`) interpreted by hardcoded game logic.
- Community engine extensions ([Ares](https://ares.strategy-x.com/),
  [Phobos](https://github.com/Phobos-developers/Phobos)) work exclusively by
  **binary injection**: a loader (Syringe) starts the game under a debugger,
  intercepts first-chance breakpoints at the entry point, and rewrites call
  sites listed in the DLL's `.syhks00` section to redirect into DLL code
  ("hooks"). Each hook record is `{address, dll, exported_function,
  bytes_overridden}`.
- Ground truth for structures/addresses is
  [YRpp](https://github.com/Phobos-developers/YRpp) — header-only C++ wrappers
  with `JMP_THIS`/`JMP_STD` thunks pinned to known addresses. **Trust YRpp
  addresses over folklore**; several "well-known" addresses circulating in
  guides are wrong or build-specific.

### Addresses we validated empirically (gamemd.exe 1.001)

| Address | Symbol | Notes |
|---|---|---|
| `0x55D360` | `Unsorted::MainLoop` | Per-frame executable loop; YRpp-documented |
| `0x685650` | *(data, NOT code)* | Circulates as "ScenarioClass::Update"; MinHook rejects it: `MH_ERROR_NOT_EXECUTABLE`. Lesson: verify every address against YRpp headers before hooking. |
| `0x734E60` | `StringTable::LoadString` | Every CSF label lookup; `__fastcall`, declared `__stdcall` in some guides |
| `0xA8ED84` | `Unsorted::CurrentFrame` | Global frame counter |
| `0xA83D4C` | `ScenarioClass::Instance` (pointer) | Null outside battle |
| `0xA8B230` | `ScenarioClass::Instance` alt reference site | — |

---

## 2. Warhead Mechanics: Why `Fire` Didn't Work

`TechnoClass::ReceiveDamage(int* dmg, int dist, WarheadTypeClass* wh,
ObjectClass* attacker, bool ignoreDefenses, bool preventSelfDefend,
HouseClass* attackingHouse)` is the engine's full damage pipeline. Calling it
directly gives you animations, `InfDeath` sequences, sounds and kill credit for
free — vastly better than writing `Health -= n`.

**But the warhead decides everything.** Two stock warheads look interchangeable
and are not:

| Warhead | `AffectsAllies` | vs Heavy Armor | Effect |
|---|---|---|---|
| `[Fire]` | no | **0%** | Looks right in docs; deals literally zero damage in most test scenarios |
| `[TerrorBombWH]` (Terrorist / Oil Derrick blast) | **yes** | normal | `InfDeath=4` flaming-soldier death; damages everyone |

**Lesson:** when a scripted damage source reports "0 units hit" while the code
path provably runs, suspect warhead modifiers (`AffectsAllies`, versus-armor
percentages, `Rock=true`) before suspecting your math. Resolve warheads by name
via `WarheadTypeClass::Find` and keep a fallback chain
(`named → TerrorBombWH → DemobombWH → Rules->C4Warhead`).

Also pass `IgnoreDefenses=true, PreventSelfDefend=true` from scripted AoE
sources, or defensive modifiers will silently eat the damage.

---

## 3. Safe Object Handles: Liveness Validation

YR++ handles are raw pointers into the game's heap. Objects are destroyed
aggressively (deaths, sell-offs, **match victory teardown**) and any cached
pointer becomes dangling within frames.

Crash we shipped to production: a delayed-explosion registry held
`BuildingClass*` across its death; on timer expiry we touched `ptr->field`
→ `EXCEPTION_ACCESS_VIOLATION` at `0x452433` reading `[eax+0x16BE]`,
`eax = nullptr`.

**The validation ladder that works**, applied *before every dereference*:

1. `ptr != nullptr`
2. **Array membership** — compare the pointer by address against all active
   engine arrays (`BuildingClass::Array`, `UnitClass::Array`,
   `InfantryClass::Array`, `AircraftClass::Array`). The engine removes dead
   objects from these arrays; a pointer absent from all of them is freed.
   Address comparison only — never dereference during the check.
3. `ptr->Health > 0` (and `!InLimbo` where relevant)

If any step fails, drop the record silently. Never "restore state" on a dead
object — that write is the crash.

**Identity:** use `AbstractClass::UniqueID` (engine-generated, stable) as the
Lua-visible key, never `type + position` — moving units change position every
frame and produce phantom "death" events.

---

## 4. MinHook on Win32: Protection & Convention Nuances

- **`MH_ERROR_NOT_EXECUTABLE (7)`** means MinHook's `VirtualQuery` check found
  the target page non-executable — usually meaning *the address isn't mapped in
  this process at all*. Our occurrence was self-inflicted: the injector had
  attached to `RA2MD.exe` (an 84 KB launcher stub) instead of `gamemd.exe`.
  `VirtualProtect` there returns `ERROR_INVALID_ADDRESS (487)` — if your
  pre-flight `VirtualProtect` fails with 487, you're in the wrong process;
  fix targeting, don't fight permissions.
- Validate targets strictly: match process **name** AND confirm the base
  module via module enumeration before injecting. Launcher stubs love sharing
  names with the real thing.
- **Calling conventions matter even for hooks that "install fine".**
  `StringTable::LoadString` is `__fastcall` (`pLabel` in ECX,
  `pOutExtraData` in EDX). We wrote an `__stdcall` detour: the hook installed
  `MH_OK`, ran for 5 seconds, then crashed inside the original function
  (write to `0x200`) because arguments were read from the wrong side of the
  stack. Rule: read YRpp's declaration for the *exact* convention; a zero-arg
  function is the only case where conventions are interchangeable.
- Prefer documented addresses with correct signatures over guessed ones with
  convenient signatures.

---

## 5. Threading & Lifecycle

- **Never do work in `DllMain`** beyond `DisableThreadLibraryCalls` +
  spawning one bootstrap thread. File I/O under the loader lock is how you get
  startup deadlocks.
- The Lua state must live on the **main game thread** (game internals are not
  thread-safe). We initialize lazily inside the first hook tick via
  `std::call_once` — no races, no menu-time interference.
- Guard all tick dispatch behind an in-match check
  (`ScenarioClass::Instance && HouseClass::CurrentPlayer &&
  CurrentFrame > 0`). Without it, calling game APIs from the main menu freezes
  input handling.
- Loggers: rotating sink, mutex-protected, **flush per line** — a buffered log
  lost to a crash is a log you never had. This single change cut our
  mean-time-to-diagnosis dramatically.
- Match guards also protect against post-victory teardown ordering issues:
  once the scenario dies, stop dispatching immediately.

## 6. Process & Tooling Lessons

- **Build your own tooling when binaries aren't published.** Syringe has no
  official releases; building from source (patching `v141_xp` toolset, dropping
  `/Gm`) took minutes and gave us flush-per-write logging upstream lacks.
- **Re-run CMake configure after adding source files** when using `file(GLOB)`;
  glob results are captured at configure time only.
- New MSVC (19.5x) vs older ecosystems: expect `/utf-8` requirements,
  `FMT_CONSTEVAL` consteval strictness (C7595), C++20 demands from YRpp, and
  `/Gm` removals. Pin fixes centrally via `add_compile_options` /
  `add_compile_definitions`.
- Keep a default-deny `.gitignore` when the repo lives inside a game
  directory — thousands of `.mix`/`.exe` files stay untracked by construction.

---

## 7. The Final Architecture

```
injector/GUI ──► gamemd.exe (CREATE_SUSPENDED)
                     │  LoadLibraryA("LuaAPI.dll")   ← before first frame
                     ▼
             MinHook: Unsorted::MainLoop (per-frame, in-match guarded)
             MinHook: StringTable::LoadString (menu watermark)
                     │
        Lua state (main thread, lazy init) ◄── scripts/init.lua (ModLoader)
                     │
        scripts/mods/<name>/main.lua   ← pure Lua gameplay modules
```

Frozen C++ host, hot-swappable pure-Lua gameplay, `pcall` isolation between
mods, and liveness-checked bindings as the only bridge between the two worlds.
Everything above exists because something crashed, silently did nothing, or
dealt zero damage first. Keep the lessons, skip the crashes.

---

## 8. Spawner Attack Orders: Historical Failed Hypothesis (FALSIFIED)

> **Status: FALSIFIED.** This section is retained only as debugging history.
> The original engine-limitation hypothesis was disproven by the test described
> in Section 9 and must not be treated as an architectural constraint.

### Initial hypothesis

Early testing appeared to show that spawner units such as Dreadnought and
Aircraft Carrier crashed when attacking buildings through
`UnitClass::Active_Click_With`. The working hypothesis was that the native
RA2/YR 1.001 attack-order path could not safely handle building targets for
spawner units.

### What was tried

1. Block the original `ActiveClickWith`.
2. Call the original for all cases.
3. Manually set `pShip->Target` before calling the original.
4. Disable `QueueMission(Attack)` in `ManagePrimaryAttackTarget` for spawner units.

These approaches did not isolate the fault. They therefore could not establish
an engine limitation.

### Superseded solution

The previous proposal to bypass native `ActiveClickWith` and manually create
missiles was based on the failed engine-limitation hypothesis. It is **not** the
current architectural solution and should not be copied as a verified pattern.

For the actual verified diagnosis and current status, see Section 9.

---

## 9. The ActiveClickWith Detour Crash: MinHook Mechanism Bug, Not an Engine Limitation

### Final diagnosis

The decisive test was to **completely disable the MinHook detour on
`ActiveClickWith`** at `0x4D74E0` and run the game using native logic only.

Result: **DRED, APOC, spawner units, and non-spawner units could attack buildings
and units without crashing.**

This falsified the earlier claim that RA2/YR itself could not process building
attack orders for spawner units. The evidence instead points to the **detour
mechanism** as the fault domain.

### Current hook status

The `ActiveClickWith` hook remains **disabled** (`kDisableActiveClickHook=true`).
The diagnosis has been corrected, but no replacement interception point has
been verified and enabled yet.

Therefore:

- Native player and AI attack orders remain functional.
- Programmatic interception of `ActiveClickWith` is currently unavailable.
- `ProcessSpawnedMissiles` remains the verified mechanism for missile-side
  redirection after launch.
- Any claim that an alternative `SetTarget`, `QueueMission`, mouse-input, or
  Ares/Phobos interception point is already implemented is **UNVERIFIED**.

### Why the detour is suspected

When the hook is active, even a trivial detour that immediately returns without
reading arguments or calling the original still crashes the game:

```cpp
void __fastcall Hooked_ActiveClickWith(FootClass* pThis, void* /*edx*/, int action, void* pTarget) {
    if (action == 0x5) {  // Attack
        LUA_LOG_WARN("[EventHook] ATTACK action detected, returning WITHOUT calling original");
        LUA_FLUSH_LOG();
        return;
    }
}
```

The observed pattern is consistent with a MinHook trampoline/prologue/address
problem. Candidate causes include:

- wrong or non-entry hook address;
- SEH frame interference;
- incompatible prologue relocation.

These are **possible causes, not individually proven root causes**. The proven
fact is narrower: disabling the detour restores native attack behavior.

### Diagnostic matrix

| Test | Result | Interpretation |
|------|--------|----------------|
| Block original, no state writes | Crash after return | Not a state-management issue |
| Call original with passthrough | Crash after return | Detour remains implicated |
| Set `pShip->Target` before original | Crash inside original | Target write alone does not solve it |
| Call original once for SpawnManager init | Crash on next tick | SpawnManager initialization does not solve it |
| Disable hook entirely | Works | Native engine path is functional; detour is the fault domain |

### Why `ActiveClickWith` remains unresolved

The YR++ headers do not provide a canonical declaration for
`UnitClass::Active_Click_With`. The project therefore cannot currently treat
its exact ABI/signature or a safe MinHook interception point as fully verified.

The strategic decision is to keep the hook disabled until an alternative
interception method is experimentally validated.

### Strategic decision

For the current milestone, native attack orders are preserved rather than
risking a process-wide crash for programmatic target interception.

The next investigation belongs to a future milestone. Candidate approaches
include:

- `SetTarget` interception;
- `QueueMission` interception;
- UI/mouse input interception;
- integration with existing Ares/Phobos extension points.

These candidates are **research targets, not current capabilities**.

### Lesson learned

When a hook crashes immediately after a trivial return, disable the hook
completely before attributing the behavior to the engine. A working native path
with the detour removed is strong evidence against an engine limitation, but it
does not by itself prove which internal MinHook mechanism is defective.

---
