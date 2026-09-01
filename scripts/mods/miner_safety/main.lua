-- Miner Safety.
--
-- Public showcase of the Unit Control slice.
-- Stops harvesters from continuing into enemy fire and resumes
-- their autonomous behavior after the threat is gone.
--
-- Featured APIs:
--   World.GetUnitsInRadius / World.GetUnits / World.GetBuildings
--   unit:GetMission / unit:GetPosition / unit:GetId / unit:Stop / unit:Hunt
--   house:SpawnUnit
--   House.GetPlayer / unit:GetOwner / house:GetName / house:IsAlliedWith

local MOD = {}

local THREAT_SCAN_EVERY  = 60    -- frames between threat scans
local SPAWN_SCAN_EVERY   = 300   -- frames between respawn checks
local THREAT_RADIUS      = 10    -- danger radius around a harvester
local REFINERY_RADIUS    = 15    -- danger radius around the refinery
local STOP_COOLDOWN      = 200   -- frames before re-stopping the same miner
local LOG_COOLDOWN        = 300   -- frames between refinery threat warnings

local MINER_TYPES    = { HARV = true, CMIN = true, YCMIN = true }
local REFINERY_TYPES = { GAREF = true, NAREF = true, YAREF = true, AREF = true }
local NEUTRAL_HOUSES = { Neutral = true, Civilian = true, Special = true }
local CIVIL_VEHICLES = { CAR = true, PCV = true, BUS = true, TRUCK = true }

-- minerId -> frame of last Stop()
local lastStop = {}

-- minerId -> true when this script stopped the miner
local threatenedMiners = {}

local lastThreatLog = -10000
local lastSpawn = -10000

local function msg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text) end
end

local function ownedBy(player, unit)
    local owner = unit:GetOwner()
    return owner ~= nil and owner == player
end

local function isMiner(unit)
    return MINER_TYPES[unit:GetTypeName()] == true
end

-- Another house, non-neutral, non-civilian: a real threat.
local function isEnemy(player, unit)
    if not unit or not unit:IsAlive() then return false end

    local owner = unit:GetOwner()
    if not owner then return false end

    if NEUTRAL_HOUSES[owner:GetName()] then return false end
    if CIVIL_VEHICLES[unit:GetTypeName()] then return false end

    return owner ~= player and not owner:IsAlliedWith(player)
end

local function enemyNear(player, x, y, radius)
    local nearby = World.GetUnitsInRadius(x, y, radius)
    if not nearby then return nil end

    for _, unit in ipairs(nearby) do
        if isEnemy(player, unit) then
            return unit
        end
    end

    return nil
end

local function fname(unit)
    local mission = unit:GetMission()
    return (type(mission) == "string") and string.lower(mission) or ""
end

local function minerId(unit)
    return unit.GetId and unit:GetId() or 0
end

local function countMiners(player)
    local units = World.GetUnits()
    local n = 0

    if not units then return n end

    for _, unit in ipairs(units) do
        if unit:IsAlive() and isMiner(unit) and ownedBy(player, unit) then
            n = n + 1
        end
    end

    return n
end

local function findRefinery(player)
    local buildings = World.GetBuildings()
    if not buildings then return nil end

    for _, building in ipairs(buildings) do
        if building:IsAlive()
            and REFINERY_TYPES[building:GetTypeName()]
            and ownedBy(player, building) then

            local p = building:GetPosition()

            if p then
                return {
                    x = math.floor(p.x),
                    y = math.floor(p.y)
                }
            end
        end
    end

    return nil
end

-- Stop miners that are currently harvesting or moving into danger.
local function scanThreats(player, frame)
    local units = World.GetUnits()
    if not units then return end

    for _, miner in ipairs(units) do
        if miner:IsAlive() and isMiner(miner) and ownedBy(player, miner) then

            local id = minerId(miner)
            local position = miner:GetPosition()

            if position then
                local threat = enemyNear(
                    player,
                    position.x,
                    position.y,
                    THREAT_RADIUS
                )

                if threat then
                    local mission = fname(miner)

                    local shouldStop =
                        mission == "harvest" or
                        mission == "move"

                    if shouldStop then
                        local last = lastStop[id]

                        if not last or (frame - last) >= STOP_COOLDOWN then
                            lastStop[id] = frame

                            if miner:Stop() then
                                threatenedMiners[id] = true

                                msg(string.format(
                                    "[miner_safety] threat detected, miner #%d stopped",
                                    id
                                ))
                            end
                        end
                    end

                elseif threatenedMiners[id] then
                    -- The threat is gone.
                    --
                    -- The current Unit Control API does not expose a dedicated
                    -- ResumeHarvest()/Harvest() command, so Hunt() is used as
                    -- the available autonomous recovery action.

                    if miner:Hunt() ~= false then
                        threatenedMiners[id] = nil

                        msg(string.format(
                            "[miner_safety] threat cleared, miner #%d resumed",
                            id
                        ))
                    end
                end
            end
        end
    end
end

-- Keep at least one harvester if the refinery is safe.
local function maybeSpawn(player, frame)
    if countMiners(player) > 0 then return end

    local refinery = findRefinery(player)
    if not refinery then return end

    if enemyNear(
        player,
        refinery.x,
        refinery.y,
        REFINERY_RADIUS
    ) then

        if frame - lastThreatLog >= LOG_COOLDOWN then
            lastThreatLog = frame

            msg("[miner_safety] refinery under threat, delaying spawn")
        end

        return
    end

    if frame - lastSpawn >= SPAWN_SCAN_EVERY then
        lastSpawn = frame

        local created = player:SpawnUnit(
            "HARV",
            1,
            refinery.x + 2,
            refinery.y + 2,
            0,
            false,
            "hunt"
        )

        msg(string.format(
            "[miner_safety] spawned harvester near refinery (ret=%d)",
            created
        ))
    end
end

function MOD.Update(frame)
    local player = House.GetPlayer()
    if not player then return end

    if frame % THREAT_SCAN_EVERY == 0 then
        scanThreats(player, frame)
    end

    if frame % SPAWN_SCAN_EVERY == 0 then
        maybeSpawn(player, frame)
    end
end

return MOD
