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

### Проблема

При хуке `UnitClass::Active_Click_With` для управления целью spawner-юнитов (Dreadnought, Aircraft Carrier) игра крашится при атаке зданий. Краш происходит внутри оригинального `ActiveClickWith` или сразу после его возврата.

### Что мы пробовали

1. **Блокировать оригинальный ActiveClickWith** → игра крашится, потому что `SpawnManager` не инициализируется, и на следующем тике движок падает при попытке прочитать `Owner->Target` (который `nullptr`).

2. **Вызывать оригинал для всех случаев** → игра крашится при атаке зданий (но работает для юнитов). Нативная логика `QueueMission(Attack)` падает внутри движка.

3. **Вручную устанавливать `pShip->Target` перед вызовом оригинала** → всё равно крашится. Проблема не в `nullptr`, а в самой логике `QueueMission`.

4. **Отключать `QueueMission(Attack)` в `ManagePrimaryAttackTarget` для spawner-юнитов** → краш всё равно происходит, потому что оригинальный `ActiveClickWith` сам вызывает `QueueMission` внутри.

### Корень проблемы

Движок RA2/YR 1.001 не может корректно обработать приказ атаки здания для spawner-юнитов через `ActiveClickWith`. Это фундаментальная проблема нативной логики, а не нашего кода.

### Решение: Ручной ракетный залп

Для spawner-атак по зданиям мы **не вызываем** оригинальный `ActiveClickWith`. Вместо этого:

1. **Блокируем оригинал** в хуке для spawner-юнитов при атаке зданий
2. **Устанавливаем `pShip->Target`** вручную (без `QueueMission`)
3. **Создаём ракеты вручную** через `BulletClass::Create` или через прямой вызов спавна с правильными параметрами
4. **Перехватываем ракеты** в `ProcessSpawnedMissiles` (уже реализовано) и перенаправляем их на кэшированную цель

Этот подход обходит нативную логику `ActiveClickWith` и `QueueMission`, которые крашатся для spawner-атак по зданиям.

### Пример кода

```cpp
// В хуке ActiveClickWith для spawner-атак по зданиям:
if (isSpawner && IsBuilding(pTargetTechno)) {
    // НЕ вызываем оригинал
    pShip->Target = pTargetTechno;  // только удерживаем цель
    g_PlayerTargetOverride[pShip] = static_cast<AbstractClass*>(pTarget);
    
    // Создаём ракеты вручную (следующий шаг)
    // BulletClass::Create(...);
    return;
}

// Для обычных юнитов и spawner-атак по юнитам — вызываем оригинал
if (g_pOriginalActiveClickWith) {
    g_pOriginalActiveClickWith(pThis, action, pTarget);
}
```

---

## 9. Manual Missile Spawning: Exhausting All ActiveClickWith Hook Approaches

### The problem persists

After implementing the "manual target setting" approach from section 8, the crash pattern remains unchanged. Regardless of what we do in the hook, the game crashes immediately after `ActiveClickWith` returns — never producing a single `Update` tick in the log.

### All five attempts through the hook (and why each failed)

1. **Block original, no manual `pShip->Target` set**
   - Result: crash immediately after `return` (zero `Update` ticks)
   - Cause: `SpawnManager` uninitialized, engine crashes reading `Owner->Target`

2. **Call original for all cases**
   - Result: crash inside original or immediately after return
   - Cause: native `QueueMission(Attack)` logic fails for spawner+building

3. **Manually set `pShip->Target` before calling original**
   - Result: crash inside original
   - Cause: issue not with `pShip->Target`, but with `QueueMission` logic itself

4. **Call original once to initialize `SpawnManager`**
   - Result: original returns successfully, but crash immediately after
   - Cause: problem not in original call, but in subsequent engine expectations

5. **Block original completely (no actions at all)**
   - Result: crash immediately after `return` (zero `Update` ticks)
   - Cause: engine expects initialization from `ActiveClickWith`; without it, the engine crashes

### The root cause

The RA2/YR 1.001 engine **cannot correctly handle** attack-building orders for spawner units through `ActiveClickWith`. This is a fundamental issue in the native engine logic, not our code. The hook cannot work around it because the crash doesn't occur in the hook itself, but in the engine's expectations after the hook returns.

### Solution: Manual missile creation

For spawner attacks on buildings, we must **completely bypass** `ActiveClickWith`:

1. **In the hook**: block original, write target to `g_PlayerTargetOverride` cache, `return`
2. **In `UpdateAll`**: for each ship in cache, check if it has `SpawnManager` and target is building
3. **Create missiles manually** via `ObjectTypeClass::CreateObject` or `BulletClass::Create`
4. **Attach missiles to target** via `pMissile->SetTarget`
5. **Launch missiles** via `pMissile->QueueMission(Mission::Attack, false)` + `NextMission()`

### Planned code example

```cpp
// In UpdateAll after processing sub-turrets:
for (auto it = g_PlayerTargetOverride.begin(); it != g_PlayerTargetOverride.end();) {
    TechnoClass* pShip = it->first;
    AbstractClass* pCachedTarget = it->second;

    if (!IsValidTechno(pShip) || !IsValidTechno(static_cast<TechnoClass*>(pCachedTarget))) {
        it = g_PlayerTargetOverride.erase(it);
        continue;
    }

    SafeHoldTarget(pShip, pCachedTarget);

    __try {
        if (pShip->SpawnManager && pCachedTarget->WhatAmI() == AbstractType::Building) {
            BulletTypeClass* pBulletType = BulletTypeClass::Find("DMISL");
            if (pBulletType) {
                CoordStruct shipPos = pShip->GetCoords();
                CoordStruct targetPos = static_cast<TechnoClass*>(pCachedTarget)->GetCoords();

                ObjectClass* pMissileObj = ObjectTypeClass::CreateObject(
                    pBulletType, shipPos, pShip->Owner);

                if (pMissileObj) {
                    TechnoClass* pMissile = static_cast<TechnoClass*>(pMissileObj);
                    pMissile->SetTarget(pCachedTarget);
                    pMissile->QueueMission(Mission::Attack, false);
                    pMissile->NextMission();

                    LUA_LOG_INFO("[ManualSpawn] Missile created for '{}' -> '{}'",
                                 SafeTechnoId(pShip), SafeTechnoId(pCachedTarget));
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    ++it;
}
```

### Related lessons

- Section 8: Spawner Attack Orders: The ActiveClickWith Crash for Buildings (initial diagnosis)
- Trap #4 (AI_CONTEXT.md): SpawnManager umbilical cord lock — native SpawnManagerClass::AI() overwrites missile target every frame; must decouple via pMissile->SpawnOwner = nullptr
- Trap #8 (AI_CONTEXT.md): RocketLocomotor pre-computed ballistic spline — RocketLocomotor computes trajectory at creation; use Force_Immediate_Destination for in-flight redirection

### Next steps

1. Find `ObjectTypeClass::CreateObject` or `BulletClass::Create` signature in YRpp headers
2. Implement manual missile spawning in `UpdateAll`
3. Test: game should not crash, missiles should fly to target
4. Add interception in `ProcessSpawnedMissiles` for redirection if needed
