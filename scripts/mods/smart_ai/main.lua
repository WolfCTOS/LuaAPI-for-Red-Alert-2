local SmartAI = {}

local SCAN_INTERVAL = 30       -- Scan every 30 frames (~1 second)
local lastScanFrame = 0

function SmartAI.Update(frame)
    if frame - lastScanFrame < SCAN_INTERVAL then return end
    lastScanFrame = frame

    local humanPlayer = House.GetPlayer()
    if not humanPlayer then return end

    local buildings = World.GetBuildings()
    local units = World.GetUnits()

    -- Group AI houses
    local aiHouses = {}
    for idx = 0, House.GetCount() - 1 do
        local h = House.GetByIndex(idx)
        if h and not h:IsHuman() and not h:IsAlliedWith(humanPlayer) and h:GetName() ~= "Neutral" then
            table.insert(aiHouses, h)
        end
    end

    if #aiHouses == 0 then return end

    -- 1. Scan AI buildings for flank attacks / damage
    for _, aiHouse in ipairs(aiHouses) do
        local aiName = aiHouse:GetName()
        local breachedBuilding = nil

        for _, bld in ipairs(buildings) do
            if bld:IsAlive() then
                local owner = bld:GetOwner()
                if owner and owner:GetName() == aiName then
                    -- Check if building is damaged below 85% HP
                    if bld:GetHealth() < (bld:GetMaxHealth() * 0.85) then
                        breachedBuilding = bld
                        break
                    end
                end
            end
        end

        -- 2. If base flank is under attack -> Rally idle reserve tanks across the base!
        if breachedBuilding then
            local bPos = breachedBuilding:GetPosition()
            local ralliedCount = 0

            for _, u in ipairs(units) do
                if u:IsAlive() and u:GetKind() == "unit" then
                    local uOwner = u:GetOwner()
                    if uOwner and uOwner:GetName() == aiName then
                        -- Check if unit is far from breach or idle
                        local dist = u:GetDistanceTo(breachedBuilding)
                        if dist and dist > 6.0 and u:IsIdle() then
                            -- Command reserve tank to reinforce the breached flank!
                            u:MoveTo(bPos.x, bPos.y)
                            u:Hunt()
                            ralliedCount = ralliedCount + 1
                        end
                    end
                end
            end

            if ralliedCount > 0 then
                local alert = string.format("\u{1F6A8} [AI Commander - %s] Flank breach at (%d,%d)! Rallied %d reserve tanks to counter-attack!",
                    aiName, bPos.x, bPos.y, ralliedCount)
                Engine.PrintMessage(alert)
                print("[LuaAPI] " .. alert)
            end
        end
    end
end

return SmartAI
