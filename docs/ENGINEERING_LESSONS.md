# Engineering Lessons — Building a Scripting Runtime for Yuri's Revenge 1.001

A technical retrospective on LuaPI: what the YR engine actually is, which
approaches failed, why they failed, and what the final architecture looks like.
Written for future maintainers and anyone extending similar 2000-era Win32 games.

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

## 8. Spawner Attack Orders: The ActiveClickWith Crash for Buildings

### The problem

When hooking `UnitClass::Active_Click_With` to control the target of spawner units (Dreadnought, Aircraft Carrier), the game crashes when attacking buildings. The crash happens inside the original `ActiveClickWith` or immediately after it returns.

### What we tried

1. **Block the original ActiveClickWith** → the game crashes because `SpawnManager` is not initialized, and on the next tick the engine crashes when trying to read `Owner->Target` (which is `nullptr`).

2. **Call the original for all cases** → the game crashes when attacking buildings (but works for units). The native `QueueMission(Attack)` logic crashes inside the engine.

3. **Manually set `pShip->Target` before calling the original** → still crashes. The problem is not `nullptr`, but the `QueueMission` logic itself.

4. **Disable `QueueMission(Attack)` in `ManagePrimaryAttackTarget` for spawner units** → the crash still happens because the original `ActiveClickWith` itself calls `QueueMission` internally.

### The root cause

The RA2/YR 1.001 engine cannot correctly handle an attack-building order for spawner units through `ActiveClickWith`. This is a fundamental problem in the native engine logic, not our code.

### Solution: Manual missile salvo

For spawner attacks on buildings we **do not call** the original `ActiveClickWith`. Instead:

1. **Block the original** in the hook for spawner units when attacking buildings
2. **Manually set `pShip->Target`** (without `QueueMission`)
3. **Create missiles manually** via `BulletClass::Create` or a direct spawn call with the correct parameters
4. **Intercept the missiles** in `ProcessSpawnedMissiles` (already implemented) and redirect them to the cached target

This approach bypasses the native `ActiveClickWith` and `QueueMission` logic, which crash for spawner attacks on buildings.

### Code example

```cpp
// In the ActiveClickWith hook for spawner attacks on buildings:
if (isSpawner && IsBuilding(pTargetTechno)) {
    // Do NOT call the original
    pShip->Target = pTargetTechno;  // only hold the target
    g_PlayerTargetOverride[pShip] = static_cast<AbstractClass*>(pTarget);
    
    // Create missiles manually (next step)
    // BulletClass::Create(...);
    return;
}

// For normal units and spawner attacks on units — call the original
if (g_pOriginalActiveClickWith) {
    g_pOriginalActiveClickWith(pThis, action, pTarget);
}
```

---

## 9. Manual Missile Spawning: Bypassing Native ActiveClickWith for Spawner+Building Attacks

### Final diagnosis (replaces "The problem persists")

After exhaustive testing, the crash is NOT specific to buildings. The trigger is any attack order on a TechnoClass target (building OR unit) issued to a spawner unit (SpawnManager != nullptr). Attacking terrain (CellClass) works fine.

Test matrix:
- Spawner attacks terrain: works
- Spawner attacks unit: crashes
- Spawner attacks building: crashes

This means the RA2/YR 1.001 engine cannot process ANY TechnoClass combat order for spawner units through ActiveClickWith. The crash occurs immediately after the hook returns, before a single Update tick, regardless of whether we call the original, block it, or manipulate pShip->Target.

### Why all six hook approaches failed

1. Block original, no Target set: crash after return (engine expects initialization)
2. Call original for all cases: crash inside original or right after
3. Set pShip->Target before original: crash inside original
4. Call original once to init SpawnManager: original returns, crash on next tick
5. Block original completely: crash after return
6. Block original + skip SafeHoldTarget in UpdateAll: crash after return (UpdateAll never runs)

Conclusion: the crash is not in our code. It is in the engine's expectation that ActiveClickWith for spawners performs internal initialization that our detour cannot replicate.

### Strategic decision

For Gate 10.4 we stop hooking ActiveClickWith for spawner units. Spawner combat orders go through the native UI path. The LuaAPI showcase mod (multi_turret_battleship) works with native orders plus our sub-turret and split-salvo systems.

Revisit spawner order interception in Milestone 11 with a different hook point (SetTarget or QueueMission detour) or via Ares/Phobos integration.
