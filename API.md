# 📚 LuaAPI for Red Alert 2: API Reference Manual

> **Version:** 1.1.0 (Milestone 11)
> **Target:** `gamemd.exe` (Yuri's Revenge 1.001)
> **Last Updated:** 2026-08-31
> **Safety:** Protected via RTTI (`WhatAmI()`) validation & SEH against `0xC0000005`.

---

## 🏗️ API Architecture

LuaAPI uses a **namespace-based architecture** for clean separation of concerns:

- **`House`** — Player and house management
- **`World`** — Global queries (units, buildings, spatial)
- **`Engine`** — Game engine functions (messages, system)
- **`Game`** — Game state and frame information
- **`AI`** — AI control functions (planned)

All methods include **automatic pointer validation** and **SEH protection** against access violations.

---

## 🛡️ Security Architecture & Pointer Safety

In bare C++ modding, referencing a destroyed or deallocated unit causes `0xC0000005` (Access Violation) crashes. LuaAPI solves this through defensive boundary validation:

- **`ValidateTechno(TechnoClass* ptr)`**: Every binding checks `nullptr`, verifies object RTTI type via `ptr->WhatAmI()`, and validates lifecycle flags (`IsAlive`, `Health > 0`).
- **Graceful Failure**: If a script references a dead unit or invalid pointer, the function returns `nil` and writes a warning to `LuaAPI.log` without crashing the game.
- **Session Lifecycle (`ResetSession`)**: All Lua callback references and VM states are cleanly reset on scenario load, restart, and exit.

---

## 🎖️ Unit Methods (TechnoClass)

Safe object methods called on valid unit/building pointers:

### Basic Properties
- **`unit:GetOwner()`** → `HouseClass*` (returns the owner house)
- **`unit:GetTypeName()`** → `string` (returns INI type identifier, e.g., `"HTNK"`, `"E1"`, `"DRED"`)
- **`unit:GetHealth()`** → `number` (returns current HP)
- **`unit:GetMaxHealth()`** → `number` (returns maximum HP)
- **`unit:IsAlive()`** → `boolean` (checks if unit is alive and valid)
- **`unit:GetPosition()`** → `{x=number, y=number}` (returns cell coordinates)
- **`unit:GetDistanceTo(otherUnit)`** → `number` (Euclidean distance in cells)

### Combat State
- **`unit:IsAttacking()`** → `boolean` (true if unit is in Attack mission)
- **`unit:TakeDamage(amount, warhead)`** → `boolean` (applies damage with optional warhead)
- **`unit:Disable(frames)`** → `boolean` (temporarily disables unit)

### Sub-Turret API
- **`unit:AddSubTurret(section, offX, offY, offZ, rot, rof)`** → `boolean`
  - Adds a sub-turret to the unit with specified voxel section and offset
  - `section`: Voxel section index (0-7)
  - `offX, offY, offZ`: 3D offset in leptons from unit center
  - `rot`: Rotation speed (0-255, 0 = instant)
  - `rof`: Rate of fire in frames (e.g., 90 = 3 seconds)

- **`unit:SetSplitTargets(targets)`** → `boolean`
  - Assigns targets to sub-turrets from a table of TechnoClass pointers
  - Turret 0 locks onto player's native target, remaining turrets get autonomous targets

- **`unit:FireSplitSalvo()`** → `boolean`
  - Fires all sub-turrets at their assigned targets
  - Spawns tracer bullets with appropriate warheads

### Particle Effects
- **`unit:SetHealthRatio(ratio)`** → `boolean` (sets unit health, e.g., `0.35` for 35% HP)
- **`unit:AttachParticleSystem(sysName)`** → `boolean` (attaches `"DamageSmokeSys"`, `"DamageFireSys"`, etc.)

---

## 💰 House Methods (HouseClass)

### Static Functions
- **`House.GetPlayer()`** → `HouseClass*` (returns the local human player)
- **`House.GetCount()`** → `number` (returns total number of houses)
- **`House.GetByIndex(index)`** → `HouseClass*` (returns house at specified index)

### Instance Methods
- **`house:GetName()`** → `string` (returns house name, e.g., `"Allies"`, `"Soviets"`)
- **`house:IsHuman()`** → `boolean` (true if house is human-controlled)
- **`house:IsAlliedWith(otherHouse)`** → `boolean` (checks alliance status)
- **`house:GetCredits()`** → `number` (reads player credits balance)
- **`house:AddCredits(amount)`** → `boolean` (safely adds or subtracts credits)
- **`house:GetPowerOutput()`** → `number` (returns total power production)
- **`house:GetPowerDrain()`** → `number` (returns total power consumption)

### Development Tools
- **`house:SpawnUnit(typeId, count, x, y, facing, force, action)`** → `number`
  - Spawns units at specified coordinates with pathfinding validation
  - **Parameters:**
    - `typeId`: Unit type identifier (e.g., `"APOC"`, `"LTNK"`, `"E1"`)
    - `count`: Number of units to spawn (default: 1)
    - `x, y`: Cell coordinates for spawn location
    - `facing`: Direction (0-255, default: 0 = North)
    - `force`: If true, spawn without pathfinding checks (default: false)
    - `action`: Optional action string (e.g., `"hunt"` to queue Hunt mission)
  - **Returns:** Count of successfully created units
  - **Behavior:**
    - If target cell is occupied and `force=false`, searches in spiral pattern (radius 3) for free cell
    - If coordinates are outside map bounds (0-511), unit is skipped with warning
    - If `action="hunt"`, queues Hunt mission on each spawned unit

---

## 🗺️ World Queries

### Global Collections
- **`World.GetBuildings()`** → `table` (array of all buildings in the world)
- **`World.GetUnits()`** → `table` (array of all units in the world)
- **`World.GetAllUnits()`** → `table` (array of all units, including infantry)

### Spatial Queries
- **`World.GetUnitsInRadius(x, y, radius)`** → `table`
  - Returns array of units within specified radius (in cells) from coordinates
  - Uses Euclidean distance calculation
  - Filters out dead/invalid units automatically

### Map Information
- **`World.GetWaypoint(id)`** → `{x=number, y=number}` or `nil`
  - Gets map coordinates for FinalAlert2 waypoint (0-99)
  - Returns `nil` if waypoint doesn't exist

---

## 💬 Engine Functions

### Messages & HUD
- **`Engine.PrintMessage(text, colorIndex)`** → `boolean`
  - Prints text directly to the standard in-game RA2 message ticker
  - `colorIndex`: 0=white, 1=yellow, 2=red, 3=green, 4=blue

---

## 🎮 Game State

- **`Game.GetFrame()`** → `number` (returns current logical frame from `Unsorted::CurrentFrame`)
- **`Game.IsInMatch()`** → `boolean` (true if match is active, false in menus/loading)

---

## 📡 Event Bus & Callbacks

Register custom script logic to reactive engine events.

### Event Registration
Events are registered in your mod's `main.lua`:

```
lua
local MyMod = {}

function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Your logic here
    return damage -- or modified damage, or nil
end

function MyMod.OnScenarioStart()
    -- Initialization logic
end

function MyMod.OnUnitDestroyed(victim, killer)
    -- Death event logic
end

function MyMod.OnDebugCommand(text)
    -- Called when user enters command via debug console (Backspace/Enter)
end

function MyMod.Update(frame)
    -- Called every logical frame (gated by CurrentFrame)
end

return MyMod
``` 

Supported Events
OnPreDamage(attacker, target, damage, dmgType, frame, subc)
Sub-frame damage interception
Arguments:
attacker: TechnoClass* or nil (attacker unit, may be nil for environmental damage)
target: TechnoClass* (unit being damaged)
damage: number (incoming damage amount)
dmgType: string (warhead type)
frame: number (current frame)
subc: number (sub-cell index)
Return:
number — sets modified damage (e.g., damage * 0.5)
0 — completely cancels incoming damage (full immunity)
nil — passes original damage through untouched
OnScenarioStart()
First-frame match initialization
Arguments: None
Trigger: Fires on frame 1 immediately after map load
Use case: Starting fleet damage, army setup, initial state
OnUnitDestroyed(victim, killer)
Unit death event
Arguments:
victim: TechnoClass* (unit that died)
killer: TechnoClass* or nil (unit that killed victim, may be nil)
Trigger: Fires when a unit or building is destroyed
Use case: Bounties, mission triggers, death effects
OnDebugCommand(text)
Debug console input
Arguments:
text: string (command entered by user)
Trigger: Called when user presses Enter in debug input mode (Backspace toggles mode)
Use case: Dev tools, rapid testing, AI spawning
Update(frame)
Per-frame update
Arguments:
frame: number (current logical frame from Unsorted::CurrentFrame)
Trigger: Called every logical frame (gated to prevent multiplayer desync)
Use case: Continuous logic, polling, state management
🌐 CnCNet Determinism & Multiplayer Guidelines
Critical Rules
⚠️ Never use os.time() or os.clock() in gameplay logic to prevent Out-of-Sync (OOS) errors.
Always seed random sequences from the frame counter: 
math.randomseed(frame + 12345)
local roll = math.random(1, 100)

Logic-Frame Gating
LuaAPI automatically gates Update() calls to logical frames (Unsorted::CurrentFrame), not render frames. This prevents multiplayer desync when clients have different FPS.
What this means for you:
Your Update() is called exactly once per logical frame on all clients
You don't need to implement frame gating yourself
Focus on game logic, not frame synchronization
Multiplayer Testing
To test LuaAPI in CnCNet multiplayer:
Both players must have identical LuaAPI.dll and scripts/ directory
Both players must have the same mods enabled in scripts/active_mods.txt
Launch via CnCNet client, then run injector.exe --attach on each client
Verify no OOS errors during gameplay
🔌 CnCNet Integration Guide
Attach Mode (Recommended)
To use LuaAPI with CnCNet:
Place LuaAPI.dll, injector.exe, and scripts/ in the game root directory
Launch game via CnCNet client (which spawns gamemd-spawn.exe)
Run injector.exe --attach from game directory
Injector polls for gamemd-spawn.exe, waits for Ares/Phobos injection, then injects LuaAPI
Headless Mode
For automated testing or modpack integration: 
injector.exe --withcncnet 

The injector runs headlessly in the background, attaches to gamemd-spawn.exe upon spawn, and auto-exits when the game closes.
ModLoader Configuration
Mods are enabled via scripts/active_mods.txt:
# scripts/active_mods.txt
multi_turret_battleship
shield_overload
my_custom_mod 

One mod name per line (folder name in scripts/mods/). Lines starting with # are comments.
📝 Complete Example: Multi-Turret Battleship 
-- scripts/mods/multi_turret_battleship/main.lua
local MultiTurretMod = {}

function MultiTurretMod.OnScenarioStart()
    local player = House.GetPlayer()
    if not player then return end

    -- Equip all Dreadnoughts with 3 sub-turrets
    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() and unit:GetOwner() == player then
            if unit:GetTypeName() == "DRED" then
                -- Add 3 turrets: Fore, Aft, Port
                unit:AddSubTurret(1, 40, 0, 15, 12, 90)   -- Fore
                unit:AddSubTurret(2, -40, 0, 15, 12, 90)  -- Aft
                unit:AddSubTurret(3, 0, 40, 15, 12, 90)   -- Port
            end
        end
    end
end

function MultiTurretMod.Update(frame)
    local player = House.GetPlayer()
    if not player then return end

    -- Every 30 frames, assign targets and fire
    if frame % 30 == 0 then
        for _, unit in ipairs(World.GetUnits()) do
            if unit:IsAlive() and unit:GetOwner() == player then
                if unit:GetTypeName() == "DRED" then
                    -- Find nearby enemies
                    local pos = unit:GetPosition()
                    local enemies = World.GetUnitsInRadius(pos.x, pos.y, 15)
                    
                    -- Filter to enemy units only
                    local targets = {}
                    for _, enemy in ipairs(enemies) do
                        if enemy:IsAlive() and enemy:GetOwner() ~= player then
                            table.insert(targets, enemy)
                        end
                    end
                    
                    -- Assign targets and fire if we have targets
                    if #targets > 0 then
                        unit:SetSplitTargets(targets)
                        unit:FireSplitSalvo()
                    end
                end
            end
        end
    end
end

return MultiTurretMod
` 
🚀 Development Tools Example: Debug Console 
``
-- scripts/mods/debug_console/main.lua
local DebugConsole = {}

function DebugConsole.OnDebugCommand(text)
    -- Parse "5 APOC" or "3 LTNK"
    local count, typeId = text:match("^(%d+)%s+(%u+)$")
    if not count or not typeId then
        Engine.PrintMessage("[DEBUG] Invalid command: " .. text, 2)
        return
    end
    
    count = tonumber(count)
    local player = House.GetPlayer()
    if not player then
        Engine.PrintMessage("[DEBUG] No player house", 2)
        return
    end
    
    -- Find player's base (first building)
    local baseX, baseY = 0, 0
    for _, b in ipairs(World.GetBuildings()) do
        if b:IsAlive() and b:GetOwner() == player then
            local p = b:GetPosition()
            baseX, baseY = math.floor(p.x), math.floor(p.y)
            break
        end
    end
    
    if baseX == 0 and baseY == 0 then
        Engine.PrintMessage("[DEBUG] No player building", 2)
        return
    end
    
    -- Spawn with hunt mission
    local spawned = player:SpawnUnit(typeId, count, baseX + 5, baseY + 5, 0, false, "hunt")
    Engine.PrintMessage("[DEBUG] Spawned " .. spawned .. " " .. typeId .. " with hunt", 1)
end

return DebugConsole 
` 

🔗 Related Documentation
ROADMAP.md — Project lifecycle and milestone status
CAPABILITIES_AND_COOKBOOK.md — Proven mechanics with post-mortems
ENGINEERING_LESSONS.md — Technical deep-dives and pitfalls
MOD_MANAGER.md — How to create and distribute mods
TUTORIAL.md — Step-by-step guide for new modders
