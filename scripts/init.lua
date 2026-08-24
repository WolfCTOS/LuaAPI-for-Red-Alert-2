print("[LuaAPI] Universal ModLoader Online!")

-- Active mods list (toggle on/off here)
local ACTIVE_MODS = {
    -- "tesla_overload",      -- Temporarily disabled for clean testing
    "delayed_explosion"
}

local loadedMods = {}
for _, modName in ipairs(ACTIVE_MODS) do
    local ok, mod = pcall(require, "mods." .. modName .. ".main")
    if ok and mod then
        table.insert(loadedMods, mod)
        print(string.format("[LuaAPI] [+] Mod active: '%s'", modName))
    else
        print(string.format("[LuaAPI] [-] Failed mod: '%s' (%s)", modName, tostring(mod)))
    end
end

local welcomed = false

function OnTick(frame)
    if not welcomed then
        local player = House.GetPlayer()
        if player then
            Engine.PrintMessage(string.format("Commander: %s | Testing: Delayed Explosion Mod (%d active)",
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
