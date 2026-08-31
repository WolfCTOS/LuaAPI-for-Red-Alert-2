Отлично, продолжаю синхронизацию. Следующий: **TUTORIAL.md** под актуальный API.

Вот обновлённый **TUTORIAL.md**:

```markdown
# 🎓 LuaAPI Tutorial: Your First Mod

> **Prerequisites:** Basic Lua programming, understanding of Red Alert 2 modding concepts
> **Difficulty:** Beginner
> **Time:** 30 minutes

This tutorial will guide you through creating your first LuaAPI mod from scratch. By the end, you'll have a working mod that spawns units and responds to game events.

---

## 📋 Table of Contents

1. [Installation & Setup](#installation--setup)
2. [Creating Your First Mod](#creating-your-first-mod)
3. [Understanding the API](#understanding-the-api)
4. [Working with Units](#working-with-units)
5. [Responding to Events](#responding-to-events)
6. [Testing Your Mod](#testing-your-mod)
7. [Next Steps](#next-steps)

---

## Installation & Setup

### Prerequisites

- **Red Alert 2: Yuri's Revenge** (version 1.001)
- **LuaAPI release** (download from GitHub releases)
- **Text editor** (VS Code, Notepad++, or any code editor)

### Installation Steps

1. **Extract LuaAPI** to your game directory:
   ```
   C:\Games\Yuri's Revenge\
   ├── gamemd.exe
   ├── LuaAPI.dll
   ├── injector.exe
   └── scripts/
       ├── init.lua
       ├── active_mods.txt
       └── mods/
   ```

2. **Verify installation** by running:
   ```bash
   injector.exe
   ```
   Then launch the game. Check `LuaAPI.log` for:
   ```
   [info] Lua engine initialized
   [info] Universal ModLoader Online!
   ```

---

## Creating Your First Mod

### Step 1: Create the Mod Directory

Navigate to `scripts/mods/` and create a new folder:

```
scripts/mods/my_first_mod/
```

### Step 2: Create `main.lua`

Create `scripts/mods/my_first_mod/main.lua` with this template:

```lua
local MyFirstMod = {}

function MyFirstMod.OnScenarioStart()
    -- This runs when a match starts
    Engine.PrintMessage("MyFirstMod loaded!", 1)
end

function MyFirstMod.Update(frame)
    -- This runs every logical frame
    -- We'll add logic here later
end

return MyFirstMod
```

### Step 3: Enable the Mod

Open `scripts/active_mods.txt` and add your mod name:

```
# scripts/active_mods.txt
my_first_mod
```

**Important:** The mod name must exactly match the folder name (case-sensitive on Linux).

### Step 4: Test Your Mod

1. Launch the game via `injector.exe`
2. Start a skirmish match
3. Check the in-game message ticker for "MyFirstMod loaded!"

If you see the message, congratulations! Your mod is working.

---

## Understanding the API

LuaAPI uses **namespaces** to organize functionality:

### Core Namespaces

| Namespace | Purpose | Example |
|-----------|---------|---------|
| **`House`** | Player and house management | `House.GetPlayer()` |
| **`World`** | Global queries | `World.GetUnits()` |
| **`Engine`** | Game engine functions | `Engine.PrintMessage()` |
| **`Game`** | Game state | `Game.GetFrame()` |

### Unit Methods

When you get a unit from `World.GetUnits()`, you can call methods on it:

```lua
local unit = World.GetUnits()[1]
if unit then
    local owner = unit:GetOwner()      -- Returns HouseClass*
    local typeName = unit:GetTypeName() -- Returns "HTNK", "E1", etc.
    local health = unit:GetHealth()    -- Returns current HP
    local pos = unit:GetPosition()     -- Returns {x=number, y=number}
end
```

**Important:** All methods include automatic validation. If a unit is destroyed between frames, methods return `nil` instead of crashing.

---

## Working with Units

### Getting All Units

```lua
function MyFirstMod.Update(frame)
    local units = World.GetUnits()
    
    for i, unit in ipairs(units) do
        if unit:IsAlive() then
            local typeName = unit:GetTypeName()
            local owner = unit:GetOwner()
            
            Engine.PrintMessage("Unit: " .. typeName, 1)
        end
    end
end
```

### Filtering Units

```lua
function MyFirstMod.Update(frame)
    local player = House.GetPlayer()
    if not player then return end
    
    local units = World.GetUnits()
    local playerUnits = {}
    
    for _, unit in ipairs(units) do
        if unit:IsAlive() and unit:GetOwner() == player then
            table.insert(playerUnits, unit)
        end
    end
    
    Engine.PrintMessage("You have " .. #playerUnits .. " units", 1)
end
```

### Spatial Queries

Find units within a radius:

```lua
function MyFirstMod.Update(frame)
    local player = House.GetPlayer()
    if not player then return end
    
    -- Find player's base (first building)
    local buildings = World.GetBuildings()
    local basePos = nil
    
    for _, building in ipairs(buildings) do
        if building:IsAlive() and building:GetOwner() == player then
            basePos = building:GetPosition()
            break
        end
    end
    
    if not basePos then return end
    
    -- Find all units within 10 cells of base
    local nearbyUnits = World.GetUnitsInRadius(basePos.x, basePos.y, 10)
    
    Engine.PrintMessage(#nearbyUnits .. " units near base", 1)
end
```

---

## Responding to Events

### OnScenarioStart

Runs once when a match starts. Perfect for initialization:

```lua
local MyFirstMod = {}
local initialized = false

function MyFirstMod.OnScenarioStart()
    Engine.PrintMessage("Match started! Initializing...", 1)
    initialized = true
end

function MyFirstMod.Update(frame)
    if not initialized then return end
    
    -- Your game logic here
end

return MyFirstMod
```

### OnUnitDestroyed

Runs when any unit or building is destroyed:

```lua
function MyFirstMod.OnUnitDestroyed(victim, killer)
    if not victim or not victim:IsAlive() then return end
    
    local victimType = victim:GetTypeName()
    local killerType = killer and killer:GetTypeName() or "unknown"
    
    Engine.PrintMessage(victimType .. " destroyed by " .. killerType, 2)
end
```

### OnPreDamage

Intercepts damage before it's applied. Return modified damage or `0` to cancel:

```lua
function MyFirstMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Make player units take 50% less damage
    local player = House.GetPlayer()
    if not player then return damage end
    
    if target and target:GetOwner() == player then
        return damage * 0.5 -- 50% damage reduction
    end
    
    return damage -- Pass through unchanged
end
```

---

## Testing Your Mod

### Method 1: Development Tools (Recommended)

Use `house:SpawnUnit()` for rapid testing:

```lua
function MyFirstMod.OnScenarioStart()
    local player = House.GetPlayer()
    if not player then return end
    
    -- Find player's base
    local buildings = World.GetBuildings()
    local basePos = nil
    
    for _, building in ipairs(buildings) do
        if building:IsAlive() and building:GetOwner() == player then
            basePos = building:GetPosition()
            break
        end
    end
    
    if not basePos then return end
    
    -- Spawn 5 Apocalypses near base with hunt mission
    local spawned = player:SpawnUnit("APOC", 5, basePos.x + 5, basePos.y + 5, 0, false, "hunt")
    Engine.PrintMessage("Spawned " .. spawned .. " Apocalypses", 1)
end
```

### Method 2: Debug Console

Create a debug console mod to spawn units on-demand:

```lua
-- scripts/mods/debug_console/main.lua
local DebugConsole = {}

function DebugConsole.OnDebugCommand(text)
    -- Parse "5 APOC" or "3 LTNK"
    local count, typeId = text:match("^(%d+)%s+(%u+)$")
    if not count or not typeId then
        Engine.PrintMessage("[DEBUG] Invalid command", 2)
        return
    end
    
    count = tonumber(count)
    local player = House.GetPlayer()
    if not player then return end
    
    -- Find player's base
    local buildings = World.GetBuildings()
    local basePos = nil
    
    for _, building in ipairs(buildings) do
        if building:IsAlive() and building:GetOwner() == player then
            basePos = building:GetPosition()
            break
        end
    end
    
    if not basePos then return end
    
    -- Spawn with hunt mission
    local spawned = player:SpawnUnit(typeId, count, basePos.x + 5, basePos.y + 5, 0, false, "hunt")
    Engine.PrintMessage("[DEBUG] Spawned " .. spawned .. " " .. typeId, 1)
end

return DebugConsole
```

**Usage:**
1. Enable both `my_first_mod` and `debug_console` in `active_mods.txt`
2. In-game, press **Backspace** to toggle debug mode
3. Type `5 APOC` and press **Enter**
4. 5 Apocalypses spawn near your base and attack enemies

---

## Next Steps

### Explore More Examples

Check out the sample mods in `scripts/mods/`:

- **`multi_turret_battleship`** — Advanced multi-turret combat system
- **`shield_overload`** — Sub-frame damage interception
- **`bounty_hunter`** — Dynamic economy and rewards
- **`damaged_fleet`** — Pre-damaged starting units

### Read the Documentation

- **[API.md](../API.md)** — Complete API reference with all methods
- **[CAPABILITIES_AND_COOKBOOK.md](../PROJECT/CAPABILITIES_AND_COOKBOOK.md)** — Proven mechanics with copy-paste recipes
- **[ENGINEERING_LESSONS.md](ENGINEERING_LESSONS.md)** — Technical deep-dives and pitfalls

### Join the Community

- Report issues on GitHub
- Share your mods
- Contribute to LuaAPI development

---

## 🎯 Common Pitfalls

### 1. Dead Units Cause nil Errors

**Problem:**
```lua
local unit = World.GetUnits()[1]
local health = unit:GetHealth() -- Crashes if unit died this frame
```

**Solution:**
```lua
local unit = World.GetUnits()[1]
if unit and unit:IsAlive() then
    local health = unit:GetHealth()
end
```

### 2. Forgetting to Return from OnPreDamage

**Problem:**
```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    -- Forgot to return!
    -- Damage becomes nil, causing undefined behavior
end
```

**Solution:**
```lua
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc)
    return damage -- Always return something
end
```

### 3. Using os.time() in Multiplayer

**Problem:**
```lua
function MyMod.Update(frame)
    if os.time() % 10 == 0 then -- Causes OOS in multiplayer!
        -- Logic here
    end
end
```

**Solution:**
```lua
function MyMod.Update(frame)
    if frame % 300 == 0 then -- Use logical frames instead
        -- Logic here
    end
end
```

### 4. Not Enabling the Mod

**Problem:** Mod folder exists but mod doesn't load.

**Solution:** Check `scripts/active_mods.txt` contains your mod name (exact folder name match).

---

## 🚀 Advanced Topics

### Sub-Turrets

Create multi-turret units:

```lua
function MyMod.OnScenarioStart()
    local player = House.GetPlayer()
    if not player then return end
    
    for _, unit in ipairs(World.GetUnits()) do
        if unit:IsAlive() and unit:GetOwner() == player then
            if unit:GetTypeName() == "DRED" then
                -- Add 3 sub-turrets
                unit:AddSubTurret(1, 40, 0, 15, 12, 90)   -- Fore
                unit:AddSubTurret(2, -40, 0, 15, 12, 90)  -- Aft
                unit:AddSubTurret(3, 0, 40, 15, 12, 90)   -- Port
            end
        end
    end
end
```

See **[CAPABILITIES_AND_COOKBOOK.md](../PROJECT/CAPABILITIES_AND_COOKBOOK.md)** for the complete multi-turret implementation.

### CnCNet Multiplayer

Test your mods in online matches:

1. Both players install identical LuaAPI + scripts
2. Both enable the same mods in `active_mods.txt`
3. Launch via CnCNet client
4. Run `injector.exe --attach` on each client
5. Play and verify no OOS errors

See **[API.md](../API.md)** section "CnCNet Determinism" for details.

---

## 📚 Quick Reference

### Namespaces

```lua
House.GetPlayer()           -- Get local player house
House.GetCount()            -- Total number of houses
House.GetByIndex(i)         -- Get house by index

World.GetBuildings()        -- All buildings
World.GetUnits()            -- All units
World.GetUnitsInRadius(x, y, r) -- Units in radius

Engine.PrintMessage(text, color) -- Print to HUD
Game.GetFrame()             -- Current logical frame
```

### Unit Methods

```lua
unit:GetOwner()             -- Returns HouseClass*
unit:GetTypeName()          -- Returns "HTNK", "E1", etc.
unit:GetHealth()            -- Current HP
unit:GetMaxHealth()         -- Maximum HP
unit:IsAlive()              -- Is unit alive?
unit:GetPosition()          -- Returns {x, y}
unit:IsAttacking()          -- Is unit in Attack mission?
unit:TakeDamage(amount, warhead) -- Apply damage
```

### House Methods

```lua
house:GetName()             -- House name
house:IsHuman()             -- Is human-controlled?
house:IsAlliedWith(other)   -- Alliance check
house:GetCredits()          -- Current credits
house:AddCredits(amount)    -- Add/subtract credits
house:SpawnUnit(typeId, count, x, y, facing, force, action) -- Spawn units
```

### Events

```lua
function MyMod.OnScenarioStart() end
function MyMod.OnUnitDestroyed(victim, killer) end
function MyMod.OnPreDamage(attacker, target, damage, dmgType, frame, subc) end
function MyMod.OnDebugCommand(text) end
function MyMod.Update(frame) end
```

---

## 🎓 You're Ready!

You now have the foundation to create powerful LuaAPI mods. Start simple, test often, and gradually add complexity.

**Remember:**
- Always validate pointers with `IsAlive()` checks
- Use `Engine.PrintMessage()` for debugging
- Test in both single-player and multiplayer
- Read the logs in `LuaAPI.log` when things go wrong

Happy modding! 🎮
```

Следующий шаг: **README.md** — отделить v1.0 stable от v1.1 development. Делаю?
