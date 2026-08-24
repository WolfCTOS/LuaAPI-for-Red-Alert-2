local DelayedExplosion = {}

local DELAY_FRAMES = 60       -- 2 seconds fuse
local BLAST_RADIUS = 4.0      -- 4 cells blast radius
local BLAST_DAMAGE = 150      -- 150 damage (lethal to infantry and light vehicles)

local trackedUnits = {}       -- [id] = { pos = {x,y}, typeName = name, lastSeen = frame }
local pendingExplosions = {}

function DelayedExplosion.Update(frame)
    local currentUnits = World.GetUnits()
    local livingIds = {}

    -- 1. Snapshot all currently living units by ID
    for _, u in ipairs(currentUnits) do
        if u:IsAlive() then
            local id = u:GetId()
            livingIds[id] = true
            trackedUnits[id] = {
                pos = u:GetPosition(),
                typeName = u:GetTypeName(),
                lastSeen = frame
            }
        end
    end

    -- 2. Detect destroyed units (tracked on previous frames but absent now)
    for id, data in pairs(trackedUnits) do
        if not livingIds[id] then
            -- Unit died! Queue delayed detonation at its last known position
            table.insert(pendingExplosions, {
                detonateFrame = frame + DELAY_FRAMES,
                pos = data.pos,
                typeName = data.typeName
            })
            print(string.format("[LuaAPI] \u{23F3} %s (ID:%d) destroyed! Cook-off timer started at (%d,%d)",
                data.typeName, id, data.pos.x, data.pos.y))
            trackedUnits[id] = nil
        end
    end

    -- 3. Process pending explosions
    local remaining = {}
    for _, exp in ipairs(pendingExplosions) do
        if frame >= exp.detonateFrame then
            local victims = World.GetUnits()
            local hitCount = 0

            for _, target in ipairs(victims) do
                if target:IsAlive() then
                    local tPos = target:GetPosition()
                    local dx = tPos.x - exp.pos.x
                    local dy = tPos.y - exp.pos.y
                    local dist = math.sqrt(dx * dx + dy * dy)

                    if dist <= BLAST_RADIUS then
                        target:TakeDamage(BLAST_DAMAGE)
                        hitCount = hitCount + 1
                    end
                end
            end

            local alert = string.format("\u{1F4A5} [Cook-off] %s secondary blast detonated at (%d,%d)! (%d units hit)",
                exp.typeName, exp.pos.x, exp.pos.y, hitCount)
            Engine.PrintMessage(alert)
            print("[LuaAPI] " .. alert)
        else
            table.insert(remaining, exp)
        end
    end
    pendingExplosions = remaining
end

return DelayedExplosion
