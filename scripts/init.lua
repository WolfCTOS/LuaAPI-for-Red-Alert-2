-- Minimal LuaAPI Proof of Concept
print("[LuaAPI] Lua script loaded successfully!")
if Engine then
    print("[LuaAPI] Engine table is present.")
end

print("[LuaAPI] Initializing gameplay scripts...")

-- Test HUD message directly on screen
if Engine.PrintMessage then
    Engine.PrintMessage("LuaAPI Connected! Welcome Commander.")
end

print("[LuaAPI] Testing House API...")

local welcomed = false

function OnTick(frame)
    if not welcomed then
        local player = House.GetPlayer()
        if player then
            local name = player:GetName()
            local money = player:GetCredits()
            local pOut = player:GetPowerOutput()
            local pDrain = player:GetPowerDrain()

            local msg = string.format("Player: %s | Money: %d$ | Power: %d/%d", name, money, pOut, pDrain)
            Engine.PrintMessage(msg)
            print("[LuaAPI] " .. msg)
            welcomed = true
        end
    end
end
