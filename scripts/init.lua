print("[LuaAPI] Universal ModLoader Online!")

-- Directory of this script (the DLL module), not the process working dir.
-- The launcher (injector.exe) writes the enabled-mod list to the ABSOLUTE path
-- <module-dir>\scripts\active_mods.txt. Reading via a CWD-relative path breaks
-- when an external client (CnCNet/Syringe) launches the game with a different
-- working directory: then the launcher and the ModLoader look at TWO DIFFERENT
-- files and the launcher-enabled mod never loads.
local function moduleScriptDir()
    local src = debug.getinfo(1, "S").source
    if src and src:sub(1, 1) == "@" then src = src:sub(2) end
    src = src:gsub("\\", "/")
    return src:match("^(.*)/[^/]*$") or "."
end

local MODULE_DIR = moduleScriptDir()

-- Read enabled mod IDs from active_mods.txt (one per line, '#' comments).
-- The file is read from the module directory (where init.lua lives), i.e. exactly
-- the same file the launcher writes to.
local function loadActiveModList()
    local active = {}
    local f = io.open(MODULE_DIR .. "/active_mods.txt", "r")
    if not f then
        f = io.open("scripts/active_mods.txt", "r")
    end
    if not f then
        f = io.open("active_mods.txt", "r")
    end

    if f then
        for line in f:lines() do
            local clean = line:match("^%s*(.-)%s*$")
            if clean and clean ~= "" and not clean:match("^#") then
                table.insert(active, clean)
            end
        end
        f:close()
    else
        -- Fallback defaults if file doesn't exist
        active = { "delayed_explosion", "smart_ai" }
    end
    return active
end

local ACTIVE_MODS = loadActiveModList()
local loadedMods = {}

-- Per-mod timing state: total_ms, max_ms, call_count indexed by mod name.
local modTiming = {}
local lastStatsReport = os.clock()

-- [[DETERMINISTIC RNG SEEDING]]
-- Critical for CnCNet multiplayer: using os.clock() or os.time() causes
-- Out-of-Sync (OOS) desync between clients.
--
-- The base seed is fixed and synchronized on all clients.
-- If mods need reseeding mid-game, only use the current frame:
-- math.randomseed(current_frame + 12345)
math.randomseed(12345)

for _, modName in ipairs(ACTIVE_MODS) do
    local ok, mod = pcall(require, "mods." .. modName .. ".main")
    if ok and mod then
        table.insert(loadedMods, { name = modName, mod = mod })
        modTiming[modName] = modTiming[modName] or { total_ms = 0.0, max_ms = 0.0, calls = 0 }
        print(string.format("[LuaAPI] [+] Mod active: '%s'", modName))
    else
        print(string.format("[LuaAPI] [-] Failed to load mod: '%s' (%s)", modName, tostring(mod)))
    end
end

local welcomed = false

-- [[Event buses]]
-- Mods may subscribe to these events in main.lua:
--   function OnScenarioStart()  -- called once when the map loads
--   function OnUnitDestroyed(victim, killer) -- called when a unit is destroyed
-- global functions are auto-dispatched from the C++ engine.

function OnScenarioStart()
    -- Base empty handler. Override it in your mod's main.lua.
end

function OnUnitDestroyed(victim, killer)
    -- Base empty handler. Override it in your mod's main.lua.
    -- victim = TechnoClass pointer (or nil), killer = TechnoClass pointer (or nil)
end

function OnTick(frame)
    if not welcomed then
        local player = House.GetPlayer()
        if player then
            Engine.PrintMessage(string.format("Commander: %s | Active Mods: %d loaded",
                player:GetName(), #loadedMods))
            welcomed = true
        end
    end

    -- Report per-mod stats every 5 seconds
    local now = os.clock()
    if now - lastStatsReport >= 5.0 then
        lastStatsReport = now
        for name, t in pairs(modTiming) do
            local avg_ms = t.calls > 0 and (t.total_ms / t.calls) or 0.0
            print(string.format("[LuaAPI] Mod timing [%s]: Avg %.2f ms | Max %.2f ms | Calls %d",
                name, avg_ms, t.max_ms, t.calls))
        end
        -- Reset counters after reporting
        for name in pairs(modTiming) do
            modTiming[name] = { total_ms = 0.0, max_ms = 0.0, calls = 0 }
        end
    end

    -- Wrap each mod.Update in wall-clock timing
    for _, entry in ipairs(loadedMods) do
        local mod = entry and entry.mod
        local modName = entry and entry.name
        if mod and type(mod.Update) == "function" and modName then
            local start = os.clock()
            local ok, err = pcall(mod.Update, frame)
            local elapsed_ms = (os.clock() - start) * 1000.0
            -- Accumulate timing for this mod
            if modTiming[modName] then
                modTiming[modName].total_ms = modTiming[modName].total_ms + elapsed_ms
                modTiming[modName].calls = modTiming[modName].calls + 1
                if elapsed_ms > modTiming[modName].max_ms then
                    modTiming[modName].max_ms = elapsed_ms
                end
            end
        end
    end
end