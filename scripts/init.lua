print("[LuaAPI] Universal ModLoader Online!")

-- Read enabled mod IDs from scripts/active_mods.txt (one per line, '#' comments).
local function loadActiveModList()
    local active = {}
    local f = io.open("scripts/active_mods.txt", "r")
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

for _, modName in ipairs(ACTIVE_MODS) do
    local ok, mod = pcall(require, "mods." .. modName .. ".main")
    if ok and mod then
        table.insert(loadedMods, mod)
        print(string.format("[LuaAPI] [+] Mod active: '%s'", modName))
    else
        print(string.format("[LuaAPI] [-] Failed to load mod: '%s' (%s)", modName, tostring(mod)))
    end
end

local welcomed = false

function OnTick(frame)
    if not welcomed then
        local player = House.GetPlayer()
        if player then
            Engine.PrintMessage(string.format("Commander: %s | Active Mods: %d loaded",
                player:GetName(), #loadedMods))
            welcomed = true
        end
    end

    for _, mod in ipairs(loadedMods) do
        if type(mod.Update) == "function" then
            pcall(mod.Update, frame)
        end
    end
end
