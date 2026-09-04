# 🚀 LuaAPI for Red Alert 2 — Yuri's Revenge

> **Lua scripting API for Command & Conquer: Red Alert 2 — Yuri's Revenge 1.001**

LuaAPI is a native x86 Lua 5.4 runtime injected into `gamemd.exe`. It exposes selected Red Alert 2 engine functionality to Lua so modders can build gameplay systems without implementing every mechanic directly in C++.

The project follows three core principles:

- 🧠 **C++ handles engine integration, runtime state, and safety**
- 🎮 **Lua controls gameplay behavior**
- 🛡️ **Only implemented and tested functionality is documented as verified**

> **Current Release:** `v1.0.0` — Production Release  
> **Current Development:** Milestone 11  
> **Development API Line:** `1.1.0`  
> **Target:** Yuri's Revenge `1.001`  
> **Compatibility:** Singleplayer, Skirmish, and CnCNet environments

---

## ✨ Key Features

- 💥 **Sub-Frame Damage Interception** — modify or cancel incoming damage through `OnPreDamage`.
- 📡 **Mod-Table Event Callbacks** — lifecycle and gameplay callbacks are methods on the table returned by `main.lua`.
- 👤 **House & Player API** — query players and access supported house state such as credits.
- 🌍 **World Queries** — inspect units and buildings and perform spatial queries.
- 🚜 **Runtime Unit Spawning** — create units directly from Lua.
- 🔫 **Multi-Turret Systems** — add runtime turret state and control split-target salvos from Lua.
- 📨 **Engine Messaging** — display messages through the game's message system.
- 🛡️ **Pointer & Lifecycle Safety** — C++ protects Lua-facing engine access against invalid runtime objects where supported.
- 🌐 **CnCNet Support** — injection and hook handling account for the CnCNet process environment.
- ⏱️ **Logical-Frame Callbacks** — gameplay logic can be tied to logical game frames rather than render FPS.

---

## 📦 Project Version

| Property | Value |
|---|---|
| **Current Release** | `v1.0.0` |
| **Development Milestone** | `11` |
| **Development API Line** | `1.1.0` |
| **Game** | Yuri's Revenge `1.001` |
| **Primary Process** | `gamemd.exe` |
| **CnCNet Process** | `gamemd-spawn.exe` |
| **Lua Runtime** | Lua 5.4 |
| **Architecture** | Native x86 |
| **Hooking** | MinHook |

> ⚠️ LuaAPI is actively developed. Always use the documentation matching the current API version.

---

## 🧩 Architecture

```text
┌─────────────────────────────────────┐
│              Lua Mods               │
│                                     │
│   Gameplay logic / AI / mechanics   │
└──────────────────┬──────────────────┘
                   │
                   ▼
┌─────────────────────────────────────┐
│             LuaAPI SDK              │
│                                     │
│  House / World / Engine / Game      │
│  Events / Object access / Safety    │
└──────────────────┬──────────────────┘
                   │
                   ▼
┌─────────────────────────────────────┐
│          C++ Engine Layer           │
│                                     │
│ Hooks / pointers / runtime state    │
│ Lifecycle / MinHook integration     │
└──────────────────┬──────────────────┘
                   │
                   ▼
┌─────────────────────────────────────┐
│       Red Alert 2 / YR Engine      │
│             gamemd.exe              │
└─────────────────────────────────────┘
```

### 🧠 Design Principle

> **C++ manages engine state. Lua decides gameplay behavior.**

C++ provides the bridge to the Westwood engine and handles unsafe or engine-specific operations. Lua determines what the mod actually does.

---

## 📁 Installation Structure

A typical LuaAPI installation looks like:

```text
Yuri's Revenge/
├── gamemd.exe
├── LuaAPI.dll
├── injector.exe
│
└── scripts/
    ├── init.lua
    ├── active_mods.txt
    │
    └── mods/
        ├── my_first_mod/
        │   └── main.lua
        │
        ├── shield_overload/
        │   └── main.lua
        │
        └── bounty_hunter/
            └── main.lua
```

---

## 🚀 Quick Start

### 1. Install LuaAPI

Extract the release into your Yuri's Revenge directory.

The installation should contain:

```text
LuaAPI.dll
injector.exe
scripts/
```

### 2. Enable a mod

Edit:

```text
scripts/active_mods.txt
```

Add one mod ID per line:

```text
shield_overload
bounty_hunter
```

Lines beginning with `#` are comments.

The loader reads the active mod list and loads each module as:

```text
mods.<mod_id>.main
```

### 3. Create a mod

Create:

```text
scripts/mods/my_first_mod/main.lua
```

A standard LuaAPI mod returns a table:

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    Engine.PrintMessage("My mod loaded!", 1)
end

function MyMod.Update(frame)
    -- Gameplay logic.
end

return MyMod
```

### 4. Launch the game

Run `injector.exe` and start Yuri's Revenge.

Check the LuaAPI log for initialization and mod-loading messages.

---

## 🧱 Mod Anatomy

Each mod lives under `scripts/mods/<mod_id>/`.

```text
scripts/mods/bounty_hunter/
└── main.lua
```

A mod may also contain additional files as required by the implementation.

The important contract is that `main.lua` returns a Lua table containing the callbacks and state used by the mod.

---

## 📡 Event Model

LuaAPI currently supports two callback mechanisms.

### Lifecycle callbacks — mod-table methods

Callbacks such as `Update`, `OnScenarioStart`, `OnPreDamage`, and `OnUnitDestroyed` are defined on the table returned by the mod.

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    -- Scenario initialization.
end

function MyMod.Update(frame)
    -- Logical-frame gameplay logic.
end

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return nil
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- React to destruction.
end

return MyMod
```

### Global debug callback

`OnDebugCommand(text)` is a global callback rather than a mod-table method.

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text, 1)
end
```

Only one active definition should normally provide this global callback.

---

## 💥 Sub-Frame Damage Interception

`OnPreDamage` can modify incoming damage before it is finally applied.

```lua
local ShieldMod = {}

function ShieldMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    if dmgType == "energy" or dmgType == "explosive" then
        return damage * 0.5
    end

    return nil
end

return ShieldMod
```

Return values:

| Return value | Result |
|---|---|
| `nil` | Original damage is preserved |
| `number` | Incoming damage is replaced |
| `0` | Damage is cancelled |

Do not return negative damage values.

---

## 👤 House & Player API

Query the current player through the `House` namespace:

```lua
local player = House.GetPlayer()

if player then
    local name = player:GetName()
    local credits = player:GetCredits()
end
```

Supported house operations can also modify game state:

```lua
player:AddCredits(500)
```

---

## 🌍 World Queries

Query units and buildings through the `World` namespace:

```lua
local units = World.GetUnits()
local buildings = World.GetBuildings()
```

Spatial queries are also available:

```lua
local nearby = World.GetUnitsInRadius(x, y, radius)
```

Example unit inspection:

```lua
for _, unit in ipairs(World.GetUnits()) do
    if unit:IsAlive() then
        local typeName = unit:GetTypeName()
        local owner = unit:GetOwner()
        local health = unit:GetHealth()
        local position = unit:GetPosition()
    end
end
```

---

## 🚜 Runtime Unit Spawning

House objects can spawn units through Lua:

```lua
local count = player:SpawnUnit(
    "APOC",
    5,
    100,
    100,
    0,
    false,
    "hunt"
)

Engine.PrintMessage(
    "Spawned " .. count .. " APOC",
    1
)
```

The return value is the number of units actually created.

---

## 🔫 Multi-Turret Systems

LuaAPI can expose additional runtime turret state through the multi-turret system.

```lua
unit:AddSubTurret(1, 40, 0, 15, 12, 90)
unit:AddSubTurret(2, -40, 0, 15, 12, 90)
```

Target allocation and firing remain Lua-controlled:

```lua
unit:SetSplitTargets(targets)
unit:FireSplitSalvo()
```

The intended architecture is:

```text
C++
 ↓
Runtime turret state
 ↓
Lua
 ↓
Target selection
 ↓
FireSplitSalvo()
```

The C++ state layer should not autonomously decide when a unit fires.

---

## 📨 Engine Messaging

Display messages through the `Engine` namespace:

```lua
Engine.PrintMessage("Hello, Commander!", 1)
```

This can be used for HUD feedback, development tools, and gameplay notifications.

---

## 🌐 CnCNet Compatibility

CnCNet may launch the game through:

```text
gamemd-spawn.exe
```

rather than the standard:

```text
gamemd.exe
```

LuaAPI's injector must therefore resolve the actual game module from the running process rather than assuming a fixed executable name.

LuaAPI may also encounter an existing hook at a function already modified by Ares, Phobos, or another engine modification. A signature mismatch should be treated as a condition requiring evaluation, not automatically as an injection failure.

---

## ⏱️ Logical Frames & Multiplayer

Gameplay logic should be tied to logical game frames rather than render FPS.

```lua
function MyMod.Update(frame)
    if frame % 300 ~= 0 then
        return
    end

    -- Execute every 300 logical frames.
end
```

Avoid using wall-clock functions for deterministic gameplay decisions:

```lua
os.time()
os.clock()
```

Different clients may have different wall-clock timing and render rates.

---

## 🛡️ Engine Safety

Red Alert 2 relies heavily on raw engine pointers. Objects may become invalid after destruction, map transitions, scenario changes, or other lifecycle events.

Never assume that an engine-backed object remains valid indefinitely.

Prefer validity checks before accessing object methods:

```lua
if unit and unit:IsAlive() then
    local position = unit:GetPosition()
end
```

The C++ layer is responsible for providing safe access to engine-backed state wherever possible.

---

## 🧹 Runtime State Management

Systems that maintain references to engine objects must handle object destruction correctly.

```text
Object created
      ↓
Runtime state registered
      ↓
Object used
      ↓
Object destroyed
      ↓
Reference invalidated
      ↓
Runtime state cleaned up
```

When maintaining C++ containers, avoid erasing entries in a way that invalidates the active iterator. Deferred cleanup is preferred.

---

## 📊 Performance

LuaAPI is designed to keep the engine-facing layer lightweight, but performance claims should be evaluated against the current build and workload.

For empirical measurements and reproduction methodology, see [`BENCHMARK.md`](BENCHMARK.md).

Do not interpret benchmark results from an older release as a permanent guarantee for every future build or mod workload.

---

## 📚 Documentation

### 📖 API Reference

[`API.md`](API.md) — Complete LuaAPI reference, namespaces, objects, methods, callbacks, and contracts.

### 🎓 Tutorial

[`TUTORIAL.md`](docs/TUTORIAL.md) — Beginner guide for creating and testing a LuaAPI mod.

### 💡 Capabilities & Cookbook

[`CAPABILITIES.md`](PROJECT/CAPABILITIES.md) — Verified capabilities, case studies, reusable recipes, and engineering lessons.

### 🔧 Project Documentation

[`PROJECT/`](PROJECT/) — Architecture, roadmap, milestones, and engineering documentation.

### 📊 Benchmark

[`BENCHMARK.md`](BENCHMARK.md) — Performance measurements and benchmark methodology.

---

## 🧪 Verification Policy

LuaAPI distinguishes between theoretical engine possibilities and functionality that has actually been implemented and tested.

A capability should only be described as **VERIFIED** when it has been tested against the current LuaAPI implementation.

If an API name, callback contract, or behavior changes, the related documentation should be updated together.

---

## 🗺️ Development Workflow

```text
Identify engine capability
          ↓
Research engine behavior
          ↓
Implement C++ integration
          ↓
Expose safe Lua interface
          ↓
Build Lua prototype
          ↓
Test in-game
          ↓
Test lifecycle & edge cases
          ↓
Verify behavior
          ↓
Document the proven capability
```

The goal is not to expose every engine feature immediately. The goal is to expose useful functionality incrementally while keeping the boundary between unsafe engine internals and Lua gameplay logic well defined.

---

## 🤝 Contributing

When developing or contributing to LuaAPI:

1. Reproduce the behavior.
2. Identify the actual engine boundary.
3. Keep unsafe engine work inside C++.
4. Expose the smallest useful Lua interface.
5. Test destruction and lifecycle edge cases.
6. Test savegame behavior where relevant.
7. Consider multiplayer determinism.
8. Document only verified behavior.

---

## ⚠️ Project Status

LuaAPI is under active development.

The API and internal architecture may change as engine integration becomes safer and more complete.

Use the documentation corresponding to the current API version.

---

## 📌 Minimal Example

A minimal LuaAPI mod:

```lua
local MyMod = {}

function MyMod.OnScenarioStart()
    local player = House.GetPlayer()

    if not player then
        return
    end

    Engine.PrintMessage(
        "LuaAPI mod initialized for " .. player:GetName(),
        1
    )
end

function MyMod.Update(frame)
    if frame % 300 ~= 0 then
        return
    end

    local player = House.GetPlayer()

    if not player then
        return
    end

    player:AddCredits(100)

    Engine.PrintMessage("+$100", 1)
end

return MyMod
```

This demonstrates the core LuaAPI model:

```text
Lua Mod
   ↓
Returned Mod Table
   ↓
Lifecycle Callback
   ↓
LuaAPI Namespace / Object
   ↓
Red Alert 2 Engine
```

---

## 🎓 Where to Start

If you are new to LuaAPI:

**1.** Read [`TUTORIAL.md`](docs/TUTORIAL.md)  
**2.** Use [`API.md`](API.md) as the technical reference  
**3.** Study [`CAPABILITIES.md`](PROJECT/CAPABILITIES.md) for verified examples  
**4.** Explore the sample mods under `scripts/mods/`

> 🛠️ **Build small. Test frequently. Verify before documenting.**

---

## 📜 License & Credits

LuaAPI is licensed under the **MIT License**.

Copyright (c) 2026 NiTeMind

See [`LICENSE`](LICENSE) for the full license text.

Third-party components distirbuted with or used by LuaAPI are distributed under their respective licenses. See the relevant license files and notices for details.

Red Alert 2 and Yuri's Revenge are proprietary software and trademarks of Electronic Arts. LuaAPI is an independent community project and is not affiliated with or endorsed by Electronic Arts.

Developed for the C&C modding community.

For engineering history and debugging notes, see [`ENGINEERING_LESSONS.md`](docs/ENGINEERING_LESSONS.md).
