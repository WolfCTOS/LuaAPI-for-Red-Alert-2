-- Bounty Hunter: awards the player 50% of a destroyed enemy unit's build cost.
--
-- The engine destroy event is not wired up, so we poll. Track live enemy units
-- (by UniqueID) and, when one disappears, pay half its base cost as a bounty.
-- Featured APIs:
--   House.GetPlayer / house:AddCredits / unit:GetOwner / house:IsAlliedWith
--   unit:GetId / unit:GetTypeName / unit:GetCost / unit:IsAlive
--   World.GetUnits

local BountyHunter = {}

local SCAN_EVERY = 10           -- frames between bounty checks
local BOUNTY_RATIO = 0.5        -- 50% of the unit's cost

local seen = {}                 -- unitId -> { cost = int, type = string }

local function isEnemyOf(player, unit)
    if not unit or not unit:IsAlive() then return false end
    local owner = unit:GetOwner()
    if not owner then return false end
    return owner ~= player and not owner:IsAlliedWith(player)
end

function BountyHunter.Update(frame)
    if frame % SCAN_EVERY ~= 0 then return end

    local player = House.GetPlayer()
    if not player then return end

    local units = World.GetUnits()   -- excludes buildings (mobile units only)
    if not units then return end

    -- Currently alive enemy units.
    local aliveNow = {}
    for _, u in ipairs(units) do
        if u:IsAlive() and isEnemyOf(player, u) then
            aliveNow[u:GetId()] = true
        end
    end

    -- Remember cost/type for newly seen enemies.
    for _, u in ipairs(units) do
        local id = u:GetId()
        if u:IsAlive() and isEnemyOf(player, u) and not seen[id] then
            seen[id] = { cost = u:GetCost() or 0, type = tostring(u:GetTypeName()) }
        end
    end

    -- Pay bounties for tracked enemies that are no longer alive.
    for id, entry in pairs(seen) do
        if not aliveNow[id] then
            local bounty = math.floor((entry.cost or 0) * BOUNTY_RATIO)
            if bounty > 0 then
                local ok, err = pcall(function() player:AddCredits(bounty) end)
                if ok then
                    Engine.PrintMessage(string.format("[Bounty] +$%d for %s", bounty, entry.type), 1)
                else
                    Engine.PrintMessage(string.format("[Bounty] grant failed: %s", tostring(err)), 1)
                end
            end
            seen[id] = nil
        end
    end
end

return BountyHunter
