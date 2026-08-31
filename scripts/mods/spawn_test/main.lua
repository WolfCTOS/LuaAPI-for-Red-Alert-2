local SpawnTest = {}

local function log(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text, 1) end
end

local function lastAopcPos()
    local best = nil
    for _, u in ipairs(World.GetUnits()) do
        if u:IsAlive() and u:GetTypeName() == "APOC" then best = u end
    end
    if not best then return nil, nil end
    local p = best:GetPosition()
    return math.floor(p.x), math.floor(p.y)
end

local ran = false

function SpawnTest.Update(frame)
    if ran or frame < 300 then return end
    ran = true

    local player = House.GetPlayer()
    if not player then log("[TEST] no player house") return end

    local ox, oy = nil, nil
    for _, b in ipairs(World.GetBuildings()) do
        if b:IsAlive() then
            local owner = b:GetOwner()
            if owner and not owner:IsAlliedWith(player) then
                local p = b:GetPosition()
                ox, oy = math.floor(p.x), math.floor(p.y)
                break
            end
        end
    end
    if not ox then log("[TEST] no enemy building") return end

    local n, x, y

    n = player:SpawnUnit("APOC", 1, ox + 6, oy + 6)
    x, y = lastAopcPos()
    log("[TEST] S1 passable ret=" .. n .. " at " .. tostring(x) .. "," .. tostring(y))

    n = player:SpawnUnit("APOC", 1, ox, oy)
    x, y = lastAopcPos()
    log("[TEST] S2 occupied+fallback ret=" .. n .. " at " .. tostring(x) .. "," .. tostring(y))

    n = player:SpawnUnit("APOC", 1, ox, oy, 0, true)
    x, y = lastAopcPos()
    log("[TEST] S3 force ret=" .. n .. " at " .. tostring(x) .. "," .. tostring(y))

    n = player:SpawnUnit("APOC", 1, 9000, 9000)
    log("[TEST] S4 no-free ret=" .. n)
end

return SpawnTest