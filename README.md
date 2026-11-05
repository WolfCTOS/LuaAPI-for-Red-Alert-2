# LuaAPI for Red Alert 2: Yuri's Revenge

CnCNet development is currently paused. Singleplayer and Skirmish remain the primary development targets, and further CnCNet work will resume only if adoption grows enough to justify the extra development and testing effort. I test my mods in CnCNet and they have worked there each time, so you can play them there too.

LuaAPI is a Lua scripting API for Command & Conquer: Red Alert 2: Yuri's Revenge 1.001. It is a native x86 Lua 5.4 runtime injected into gamemd.exe. It exposes selected Red Alert 2 engine functionality to Lua so modders can build gameplay systems without implementing every mechanic directly in C++.

Three principles guide the project:

- C++ handles engine integration, runtime state, and safety.
- Lua controls gameplay behavior.
- Only implemented and tested functionality is documented as verified.

## For players

Download, extract, and play in about five minutes.

1. Download the release archive from one of these places:
   - ModDB: https://www.moddb.com/mods/luaapi-for-red-alert-2-yuris-revenge
   - GitHub Releases: https://github.com/WolfCTOS/LuaAPI-for-Red-Alert-2/releases
2. Extract the archive into your Yuri's Revenge directory (where gamemd.exe lives). You should end up with LuaAPI.dll, injector.exe and a scripts/ folder next to it.
3. Open scripts/active_mods.txt and make sure it lists the mod you want, one ID per line. A line starting with # is a comment. The default stack is:

   ```
   bounty_hunter
   smart_ai
   radiation
   ```

4. Run injector.exe and press Launch. It starts the game with LuaAPI loaded. The injector is a small launcher that starts the game and loads LuaAPI into it. LuaAPI does not load itself, so the game must go through the injector. If the game is already running, the injector picks up gamemd.exe (or gamemd-spawn.exe under CnCNet) instead.
5. Open LuaAPI.log (next to LuaAPI.dll). You should see mod-loading lines such as `[LuaAPI] [+] Mod active: 'smart_ai'`. If a mod's Update throws, the error lands in the same log with the mod name, so you do not need a debugger.

If you use Ares, Phobos, Syringe or CnCNet, see the compatibility sections below. The standard setup needs no extra steps.

## Project version

| Property | Value |
|---|---|
| Project stage | Beta (0.2.0) |
| Current release tag | v0.2.0 (Beta) |
| Current focus | Gate reset, see PROJECT/GATES.md; research on a Dynamic Unit Behavior Development API |
| Development API line | 0.2.0 |
| Game | Yuri's Revenge 1.001 |
| Primary targets | Singleplayer and Skirmish |
| Primary process | gamemd.exe |
| CnCNet process | gamemd-spawn.exe |
| CnCNet | Compatible and tested in practice; active development paused |
| Lua runtime | Lua 5.4 |
| Architecture | Native x86 |
| Hooking | MinHook |

v0.1.0 was a historical milestone tag, not a production claim. The alpha history is in FSM/MODDB_ALPHA_RELEASE.md.

## Key features

- Unit control and tactical AI: issue runtime orders (MoveTo, Attack, Stop) and observe missions and targets. Verified in live tactical showcases.
- House and player API: query players and access supported house state such as credits.
- World queries: inspect units, buildings, and aircraft, with spatial and selection queries.
- Runtime unit spawning: create units directly from Lua.
- Input and selected units: build hotkey-driven mods with Input.WasKeyPressed and World.GetSelectedUnits.
- Engine messaging: display messages through the game's message system.
- Pointer and lifecycle safety: the C++ layer protects Lua-facing engine access against invalid runtime objects where supported.
- CnCNet environment support: injection and hook handling account for the CnCNet process environment. Active CnCNet development is paused.
- Logical-frame callbacks: tie gameplay logic to logical game frames rather than render FPS.

Damage interception (OnPreDamage) is documented as a contract only and is not wired in the current build. See API.md (Callback Model) before building anything damage-reactive.

## Architecture
<img width="1294" height="215" alt="{60F086AA-8579-4119-BA04-CD717FD78D78}" src="https://github.com/user-attachments/assets/d81b793b-b83b-4b64-afb9-2c6b3bca4e6f" />





C++ manages engine state and provides the bridge to the Westwood engine, including the unsafe or engine-specific operations. Lua decides what the mod actually does.

## Why LuaAPI?

LuaAPI works alongside Ares and Phobos. They provide engine-level capabilities, and LuaAPI makes the gameplay logic around those capabilities programmable. A modder can take an existing mechanic, unit, or engine capability and rework how it behaves at runtime:

```
Existing mechanic
      ↓
Ares / Phobos / Engine capabilities
      ↓
LuaAPI runtime logic
      ↓
Custom rules, state, targets, timers, conditions
      ↓
New gameplay behavior
```

For example, a basic bounty mechanic can reward the player for destroying an enemy unit. With LuaAPI the same idea can grow into a larger system: a hidden target, a specific hunter faction, a time limit, kill streaks, changing rewards, temporary buffs, or a reinforcement delivered to the hunter.

The same principle guides API development. A new LuaAPI primitive should provide a meaningful runtime capability and should not duplicate an existing engine, Ares, or Phobos feature.

## Installation structure

A typical installation looks like this:

```
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
        ├── smart_ai/
        ├── radiation/
        ├── bounty_hunter/
        ├── double_harvester_deploy/
        └── dynamic_fow/
```

The default active stack is bounty_hunter, smart_ai, radiation (see scripts/active_mods.txt). double_harvester_deploy and dynamic_fow ship in scripts/mods/ but are off by default. Add their IDs to active_mods.txt to try them.

## Creating a mod

Install LuaAPI and enable a mod as described in the player steps above. The loader reads the active mod list and loads each entry as `mods.<mod_id>.main`. Add mod IDs that exist under scripts/mods/, one per line.

To create a mod, add scripts/mods/my_first_mod/main.lua. A standard LuaAPI mod returns a table:

```lua
local MyMod = {}

function MyMod.Update(frame)
    -- Gameplay logic. Update is the one reliably dispatched mod-table method.
end

return MyMod
```

Engine event callbacks (OnScenarioStart, OnPreDamage, OnUnitDestroyed) are globals, not mod-table methods. Defining MyMod.OnScenarioStart never fires. See the event model below.

Launch the game through injector.exe, as in step 4 above. LuaAPI loads only through the injector, never on a plain game start. Check LuaAPI.log (written next to LuaAPI.dll) for initialization and mod-loading messages. If a mod's Update throws, the error is logged as `[LuaAPI] Mod '<name>' Update error: ...` and the other mods keep running.

### Mod anatomy

Each mod lives under scripts/mods/<mod_id>/. The smallest mod is a single main.lua:

```
scripts/mods/my_first_mod/
└── main.lua
```

A mod may contain additional files as its implementation requires. The main.lua file must return a Lua table with the callbacks and state the mod uses.

## Event model

LuaAPI currently supports two callback mechanisms.

### Lifecycle callbacks

Callbacks such as Update are methods on the table the mod returns. Engine event callbacks (OnScenarioStart, OnPreDamage, OnUnitDestroyed) are looked up as globals instead, see API.md, Callback Model. OnPreDamage is also not wired in the current build: it is collected but never invoked.

```lua
local MyMod = {}

-- Engine event callbacks are looked up as GLOBALS in the current build.
-- Defining them as mod-table methods does NOT wire them.
function OnScenarioStart()
    -- Scenario initialization.
end

function MyMod.Update(frame)
    -- Per-logical-frame logic (the one reliably dispatched mod-table method).
end

function OnUnitDestroyed(victim, killer)
    -- ⚠️ Never dispatched in the current build (contract without invocation).
    -- Mods must use ID-diff death detection instead; see API.md.
end

return MyMod
```

### Global debug callback

OnDebugCommand(text) is a global callback, not a mod-table method.

```lua
function OnDebugCommand(text)
    Engine.PrintMessage("Command: " .. text)
end
```

Only one active definition should normally provide this global callback.

## Damage interception: OnPreDamage (not wired)

In the current build the engine collects the global OnPreDamage reference every frame but never invokes it with damage arguments, because no ReceiveDamage hook is installed. Live engine damage does not reach Lua. The contract below is the intended design, kept for when interception is wired. Do not build damage-reactive mechanics on it yet.

The intended contract (a global callback, not a mod-table method):

```lua
function OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    if dmgType == "energy" or dmgType == "explosive" then
        return damage * 0.5
    end

    return nil
end
```

Intended return values:

| Return value | Result |
|---|---|
| nil | Original damage is preserved |
| number | Incoming damage is replaced |
| 0 | Damage is cancelled |

Do not return negative damage values. Authoritative status: API.md, Callback Model. Evidence: the conflict note in FSM/CAPABILITIES.md.

## House and player API

Query the current player through the House namespace:

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

## World queries

Query units and buildings through the World namespace:

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

## Runtime unit spawning

House objects can spawn units from Lua:

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

Engine.PrintMessage("Spawned " .. count .. " APOC")
```

## Engine messaging

Display messages through the Engine namespace:

```lua
Engine.PrintMessage("Hello, Commander!")
```

Use it for HUD feedback, development tools, and gameplay notifications.

## CnCNet compatibility

CnCNet may launch the game through gamemd-spawn.exe instead of gamemd.exe. The injector therefore resolves the actual game module from the running process and does not assume a fixed executable name.

LuaAPI may also meet an existing hook on a function that Ares, Phobos, or another engine modification has already changed. A signature mismatch is a condition to evaluate and not automatically an injection failure.

## Ares, Phobos, Syringe and CnCNet compatibility

The game folder can ship alongside the usual community stack:

- Syringe.exe, Ares.dll and Phobos.dll: the Syringe/Ares/Phobos extension chain that many mods and the CnCNet client rely on.
- run_syringe_game.bat and run_syringe_cncnet.bat: launch the game through Syringe (`Syringe.exe gamemd.exe CnCNet-Spawner.dll`).

LuaAPI works next to this stack. The injector looks for the live game process (gamemd.exe, then gamemd-spawn.exe), so both the plain start and the CnCNet start are covered. The order of operations verified in practice:

1. Run injector.exe and press Launch, which starts the game with LuaAPI. Alternatively, start the game your usual way (plain shortcut, Syringe batch file, or the CnCNet client) and let the injector pick up the running process.
2. Confirm that LuaAPI.log shows the DLL loaded and your mods active.

If Ares or Phobos already hook a function that LuaAPI also hooks, the log records a signature note. That is diagnostic information and not an automatic failure. Tested configurations are singleplayer and skirmish. CnCNet play works in practice but is not under active development (see the note at the top).

## Logical frames and multiplayer

Tie gameplay logic to logical game frames, not render FPS:

```lua
function MyMod.Update(frame)
    if frame % 300 ~= 0 then
        return
    end

    -- Execute every 300 logical frames.
end
```

Avoid wall-clock functions for deterministic gameplay decisions, because clients can differ in timing and render rate:

```
os.time()
os.clock()
```

## Engine safety

Red Alert 2 relies heavily on raw engine pointers. Objects can become invalid after destruction, map transitions, scenario changes, or other lifecycle events, so never assume that an engine-backed object stays valid. Check validity before calling object methods:

```lua
if unit and unit:IsAlive() then
    local position = unit:GetPosition()
end
```

The C++ layer provides safe access to engine-backed state wherever possible.

## Runtime state management

Systems that keep references to engine objects must handle object destruction:

```
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

## Session lifecycle

Each match starts with a fresh Lua state. When you return to the menu, the session resets (mod state, house cache, timers, and marks are cleared) and the next match initializes everything again. Mods do not need restart guards for match transitions. Savegame loads follow a different lifecycle, see the limitations below.

## Performance

LuaAPI keeps the engine-facing layer lightweight, but performance claims should be checked against the current build and workload. BENCHMARK.md has the measurements and the reproduction methodology. Results from an older release do not guarantee performance for a newer build or a different mod workload.

## Documentation

- API.md: complete reference for namespaces, objects, methods, callbacks, and contracts.
- TUTORIAL.md: beginner guide to creating and testing a LuaAPI mod.
- CAPABILITIES.md: verified capabilities, case studies, reusable recipes, and engineering lessons.
- PROJECT/: architecture, roadmap, milestones, and engineering documentation.
- BENCHMARK.md: performance measurements and benchmark methodology.

Use the documentation that matches the current API version.

## Verification policy

LuaAPI separates theoretical engine possibilities from functionality that has been implemented and tested. A capability is described as VERIFIED only when it has been tested against the current LuaAPI implementation. If an API name, callback contract, or behavior changes, the related documentation is updated together with it.

## Development workflow

```
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

LuaAPI exposes useful functionality incrementally and keeps a clear boundary between unsafe engine internals and Lua gameplay logic.

## Building from source

This section is only for building LuaAPI itself. The build is MSVC-only and 32-bit x86:

```
build_release.bat
```

The equivalent is `cmake --build build --config Release`. The build deploys LuaAPI.dll and injector.exe next to the game automatically. Full instructions, submodule setup and the iteration workflow are in AGENTS.md.

## Contributing

1. Reproduce the behavior.
2. Identify the actual engine boundary.
3. Keep unsafe engine work inside C++.
4. Expose the smallest useful Lua interface.
5. Test destruction and lifecycle edge cases.
6. Test savegame behavior where relevant.
7. Consider multiplayer determinism.
8. Document only verified behavior.

## Current limitations

- No live damage interception. OnPreDamage is collected but never invoked (no ReceiveDamage hook), so there are no reactive-armor or shield mechanics.
- No death event with a payload. OnUnitDestroyed is never dispatched. Death detection is ID-diff polling (see Command Authority).
- No production-complete events. Factory queues expose counts only, and AI.QueueUnit requests are accepted but produce no attributable output live (blocked for production-director designs).
- No radar or fog control, alliance switching, or superweapon API.
- Multiplayer: powers lock with 2+ humans by design. Two-client behavior is otherwise unverified. Never use wall-clock time for gameplay decisions.
- Savegame loads do not fire OnScenarioStart, and Lua-state restore across loads is unverified. Add restart guards for your runtime state.
- No order lease. Lua orders compete with vanilla AI re-selection, and a permanent override primitive does not exist.

Evidence grades for every claim are in PROJECT/GATES.md and FSM/VERIFICATION.md.

## Project status

LuaAPI is a beta-stage project under active development. The API and internal architecture may change as engine integration becomes safer and more complete. Project gates and their current statuses are in PROJECT/GATES.md. The evidence ledger (what is verified, and at which level) is in FSM/VERIFICATION.md.

## Minimal example

```lua
local MyMod = {}

function MyMod.Update(frame)
    if frame % 300 ~= 0 then
        return
    end

    local player = House.GetPlayer()

    if not player then
        return
    end

    player:AddCredits(100)

    Engine.PrintMessage("+$100")
end

return MyMod
```

The example shows the core LuaAPI model:

```
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

## Where to start

1. Read TUTORIAL.md.
2. Use API.md as the technical reference.
3. Study CAPABILITIES.md for verified examples.
4. Explore the sample mods under scripts/mods/.

The API classification (Stable, Experimental, Internal, Unverified, Blocked) is in PROJECT/API_FREEZE_AUDIT.md. Build new mods on the Stable list and treat everything else as unsupported until proven otherwise.

## License and credits

LuaAPI is licensed under the MIT License. Copyright (c) 2026 NiTeMind. See LICENSE for the full text.

Third-party components distributed with or used by LuaAPI are distributed under their respective licenses. See the relevant license files and notices.

Red Alert 2 and Yuri's Revenge are proprietary software and trademarks of Electronic Arts. LuaAPI is an independent community project and is not affiliated with or endorsed by Electronic Arts.

For engineering history and debugging notes, see ENGINEERING_LESSONS.md.
