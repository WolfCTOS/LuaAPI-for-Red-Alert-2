local DelayedExplosion = {}

local BLAST_RADIUS = 3.0      -- 3 cells blast radius
local BLAST_DAMAGE = 150      -- 150 fire damage (lethal to infantry)

local trackedTanks = {}

function DelayedExplosion.Update(frame)
    local currentUnits = World.GetUnits()
    local livingIds = {}

    -- 1. Track currently living vehicles/tanks
    for _, u in ipairs(currentUnits) do
        if u:IsAlive() and u:GetKind() == "unit" then
            local id = u:GetId()
            livingIds[id] = true
            trackedTanks[id] = {
                pos = u:GetPosition(),
                typeName = u:GetTypeName()
            }
        end
    end

    -- 2. Detect destroyed tanks and detonate INSTANTLY in the same frame
    for id, data in pairs(trackedTanks) do
        if not livingIds[id] then
            -- INSTANT BLAST at death position!
            local victims = World.GetUnits()
            local hitCount = 0

            for _, target in ipairs(victims) do
                if target:IsAlive() then
                    local tPos = target:GetPosition()
                    local dx = tPos.x - data.pos.x
                    local dy = tPos.y - data.pos.y
                    local dist = math.sqrt(dx * dx + dy * dy)

                    if dist <= BLAST_RADIUS then
                        -- Instantly ignite target with TerrorBombWH (InfDeath=4 fire death)
                        target:TakeDamage(BLAST_DAMAGE, "TerrorBombWH")
                        hitCount = hitCount + 1
                    end
                end
            end

            local alert = string.format("\u{1F4A5} [Instant Cook-off] %s exploded at (%d,%d)! (%d units caught fire)",
                data.typeName, data.pos.x, data.pos.y, hitCount)
            Engine.PrintMessage(alert)
            print("[LuaAPI] " .. alert)

            trackedTanks[id] = nil
        end
    end
end

return DelayedExplosion
