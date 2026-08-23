print("[LuaAPI] Red Alert 2 LuaAPI Engine Started!")

local TeslaOverload = require("tesla_overload")
local welcomed = false

function OnTick(frame)
    if not welcomed then
        local player = House.GetPlayer()
        if player then
            local name = player:GetName()
            local credits = player:GetCredits()
            Engine.PrintMessage(string.format("Commander: %s | Money: %d$ | Tesla Overload: ACTIVE", name, credits))
            welcomed = true
        end
    end

    TeslaOverload.Update(frame)
end
