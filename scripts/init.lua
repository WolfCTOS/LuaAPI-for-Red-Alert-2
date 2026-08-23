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
local scanned = false

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

    -- Gate 4.1: scan world objects once
    if welcomed and not scanned and frame > 30 and World.GetBuildings then
        local buildings = World.GetBuildings()
        local units = World.GetUnits()
        print(string.format("[LuaAPI] World Scan: %d buildings found", #buildings))

        local printed = 0
        for i, bld in ipairs(buildings) do
            if bld:IsAlive() then
                local owner = bld:GetOwner()
                local ownerName = owner and owner:GetName() or "Neutral"
                local pos = bld:GetPosition()
                local typeName = bld:GetTypeName()
                local hp = bld:GetHealth()
                local maxHp = bld:GetMaxHealth()

                print(string.format("[LuaAPI] Building #%d: [%s] HP:%d/%d Owner:%s Pos:(%d,%d)",
                    i, typeName, hp, maxHp, ownerName, pos.x, pos.y))

                if printed < 3 then
                    Engine.PrintMessage(string.format("Found: %s (HP: %d/%d)", typeName, hp, maxHp))
                    printed = printed + 1
                end
            end
        end

        for i, u in ipairs(units) do
            if u:IsAlive() then
                local pos = u:GetPosition()
                print(string.format("[LuaAPI] Unit #%d: [%s] HP:%d/%d Pos:(%d,%d)",
                    i, u:GetTypeName(), u:GetHealth(), u:GetMaxHealth(), pos.x, pos.y))
            end
        end

        Engine.PrintMessage("World scan complete!")
        print("[LuaAPI] World scan complete.")
        scanned = true
    end
end
