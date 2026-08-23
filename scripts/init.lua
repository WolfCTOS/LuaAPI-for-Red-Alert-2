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
print("[LuaAPI] Testing Gate 5.1 Action API...")

local welcomed = false
local scanned = false
local actionTested = false

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

    -- Gate 5.1: EMP-lock + damage ALL enemy buildings
    if welcomed and not actionTested and frame > 60 then
        local player = House.GetPlayer()
        local buildings = World.GetBuildings()

        if player and #buildings > 0 then
            local hitCount = 0

            for _, bld in ipairs(buildings) do
                local owner = bld:GetOwner()
                if owner and not player:IsAlliedWith(owner) and bld:IsAlive() then
                    local typeName = bld:GetTypeName()
                    local hpBefore = bld:GetHealth()

                    local remainingHp = bld:TakeDamage(50)
                    bld:Disable(300) -- EMP lock for ~10-15 seconds
                    hitCount = hitCount + 1

                    local msg = string.format("[Combat Test] EMP %s: %d -> %d HP", typeName, hpBefore, remainingHp)
                    Engine.PrintMessage(msg)
                    print("[LuaAPI] " .. msg)
                end
            end

            if hitCount > 0 then
                print(string.format("[LuaAPI] EMP locked %d enemy building(s) for 300 frames.", hitCount))
                Engine.PrintMessage(string.format("EMP Lock: %d buildings offline!", hitCount))
            else
                print("[LuaAPI] No enemy buildings found; combat test skipped.")
            end

            actionTested = true
        end
    end
end
