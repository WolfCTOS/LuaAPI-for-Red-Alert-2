-- Miner Safety
--
-- Stateful harvester safety controller.
--
-- Current scope:
--   * Detect nearby enemy threats.
--   * Stop harvesters entering dangerous areas.
--   * Keep the miner in a protected "threatened" state.
--   * Resume autonomous behavior only after the threat is gone.
--   * Keep at least one harvester alive when possible.
--
-- Important:
--   This version does NOT implement player mining assignments or TIBTRE
--   mining points yet because the current public LuaAPI shown here does
--   not expose:
--       1. world mouse-click coordinates;
--       2. Tiberium/ore overlay queries;
--       3. TIBTRE enumeration.
--
-- Those should be added as separate API primitives instead of being faked.

local MOD = {}

----------------------------------------------------------------
-- Configuration
----------------------------------------------------------------

local THREAT_SCAN_EVERY = 30
local SPAWN_SCAN_EVERY  = 300

local THREAT_RADIUS     = 10
local REFINERY_RADIUS   = 15

-- After detecting a threat, don't immediately release the miner.
-- This prevents rapid Stop/Hunt/Stop/Hunt oscillation.
local SAFE_RELEASE_DELAY = 90

local STOP_COOLDOWN      = 120
local LOG_COOLDOWN       = 300

----------------------------------------------------------------
-- Unit definitions
----------------------------------------------------------------

local MINER_TYPES = {
    HARV  = true,
    CMIN  = true,
    YCMIN = true
}

local REFINERY_TYPES = {
    GAREF = true,
    NAREF = true,
    YAREF = true,
    AREF  = true
}

local NEUTRAL_HOUSES = {
    Neutral  = true,
    Civilian = true,
    Special  = true
}

local CIVIL_VEHICLES = {
    CAR   = true,
    PCV   = true,
    BUS   = true,
    TRUCK = true
}

----------------------------------------------------------------
-- Runtime state
----------------------------------------------------------------

-- minerId -> state
--
-- {
--     threatened      = bool,
--     threatId        = enemy id or nil,
--     threatUntil     = frame,
--     lastStop        = frame,
--     lastThreatLog   = frame
-- }
--
local miners = {}

local lastSpawn = -10000
local lastGlobalThreatLog = -10000

----------------------------------------------------------------
-- Utility
----------------------------------------------------------------

local function msg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage

    if f then
        f(text)
    end
end

local function safeCall(fn, ...)
    if not fn then
        return false
    end

    local ok, result = pcall(fn, ...)

    if not ok then
        return false
    end

    return result
end

local function getMinerState(id)
    local state = miners[id]

    if not state then
        state = {
            threatened = false,
            threatId = nil,
            threatUntil = 0,
            lastStop = -10000,
            harvestX = nil,
            harvestY = nil
        }

        miners[id] = state
    end

    return state
end

local function ownedBy(player, unit)
    if not player or not unit then
        return false
    end

    local owner = safeCall(unit.GetOwner, unit)

    return owner ~= nil and owner == player
end

local function isMiner(unit)
    if not unit then
        return false
    end

    local typeName = safeCall(unit.GetTypeName, unit)

    return typeName ~= nil and MINER_TYPES[typeName] == true
end

local function getId(unit)
    if not unit then
        return 0
    end

    local id = safeCall(unit.GetId, unit)

    return id or 0
end

local function getPosition(unit)
    if not unit then
        return nil
    end

    return safeCall(unit.GetPosition, unit)
end

local function getTypeName(unit)
    if not unit then
        return nil
    end

    return safeCall(unit.GetTypeName, unit)
end

local function getMission(unit)
    if not unit then
        return ""
    end

    local mission = safeCall(unit.GetMission, unit)

    if type(mission) ~= "string" then
        return ""
    end

    return string.lower(mission)
end

local function isAlive(unit)
    if not unit then
        return false
    end

    return safeCall(unit.IsAlive, unit) == true
end

----------------------------------------------------------------
-- Enemy detection
----------------------------------------------------------------

local function isEnemy(player, unit)
    if not unit or not isAlive(unit) then
        return false
    end

    local owner = safeCall(unit.GetOwner, unit)

    if not owner then
        return false
    end

    local ownerName = safeCall(owner.GetName, owner)

    if ownerName and NEUTRAL_HOUSES[ownerName] then
        return false
    end

    local typeName = getTypeName(unit)

    if typeName and CIVIL_VEHICLES[typeName] then
        return false
    end

    if owner == player then
        return false
    end

    local allied = safeCall(owner.IsAlliedWith, owner, player)

    if allied then
        return false
    end

    return true
end

local function findEnemyNear(player, x, y, radius)
    local nearby = World.GetUnitsInRadius(x, y, radius)

    if not nearby then
        return nil
    end

    for _, unit in ipairs(nearby) do
        if isEnemy(player, unit) then
            return unit
        end
    end

    return nil
end

----------------------------------------------------------------
-- Miner state
----------------------------------------------------------------

local function markThreatened(miner, threat, frame)
    local id = getId(miner)

    if id == 0 then
        return
    end

    local state = getMinerState(id)

    state.threatened = true
    state.threatId = getId(threat)
    state.threatUntil = frame + SAFE_RELEASE_DELAY
end

local function clearThreat(miner)
    local id = getId(miner)

    if id == 0 then
        return
    end

    local state = miners[id]

    if state then
        state.threatened = false
        state.threatId = nil
        state.threatUntil = 0
    end
end

local function isThreatened(miner)
    local id = getId(miner)

    if id == 0 then
        return false
    end

    local state = miners[id]

    return state ~= nil and state.threatened
end

----------------------------------------------------------------
-- Stop logic
----------------------------------------------------------------

local function shouldStopMiner(miner)
    local mission = getMission(miner)

    return mission == "harvest"
        or mission == "move"
        or mission == "guard"
        or mission == "attack"
end

local function stopMiner(miner, frame, threat)
    local id = getId(miner)

    if id == 0 then
        return false
    end

    local state = getMinerState(id)

    if frame - state.lastStop < STOP_COOLDOWN then
        return false
    end

    state.lastStop = frame

    -- Capture the player's assigned harvest cell BEFORE Stop() clears
    -- FootClass::Destination. Only meaningful while the miner is actually on a
    -- Harvest mission; a Move/Return miner may have a non-harvest destination.
    if getMission(miner) == "harvest" then
        local loc = safeCall(miner.GetHarvestLocation, miner)
        if loc and loc.x and loc.y then
            state.harvestX = loc.x
            state.harvestY = loc.y
        end
    end

    local result = safeCall(miner.Stop, miner)

    if result == false then
        return false
    end

    markThreatened(miner, threat, frame)

    msg(string.format(
        "[miner_safety] miner #%d stopped: enemy #%d nearby",
        id,
        getId(threat)
    ))

    return true
end

----------------------------------------------------------------
-- Resume logic
----------------------------------------------------------------

local function tryResume(miner, frame)
    local id = getId(miner)

    if id == 0 then
        return
    end

    local state = miners[id]

    if not state or not state.threatened then
        return
    end

    -- Give the miner a short safety buffer.
    if frame < state.threatUntil then
        return
    end

    -- Re-check the area immediately before resuming.
    local position = getPosition(miner)

    if not position then
        return
    end

    local player = safeCall(miner.GetOwner, miner)

    if not player then
        return
    end

    local threat = findEnemyNear(
        player,
        position.x,
        position.y,
        THREAT_RADIUS
    )

    if threat then
        -- Threat is still present.
        state.threatId = getId(threat)
        state.threatUntil = frame + SAFE_RELEASE_DELAY
        return
    end

    --
    -- Restore the player's assigned harvest cell if we captured one before the
    -- Stop. Only fall back to autonomous Hunt() when there is no saved location.
    --
    if state.harvestX and state.harvestY then
        local restored = safeCall(
            miner.HarvestAt,
            miner,
            state.harvestX,
            state.harvestY
        )

        if restored ~= false then
            clearThreat(miner)

            msg(string.format(
                "[miner_safety] miner #%d threat cleared, harvest restored at (%d,%d)",
                id,
                state.harvestX,
                state.harvestY
            ))

            return
        end
    end

    -- No saved harvest assignment (or restore failed): autonomous recovery.
    local result = safeCall(miner.Hunt, miner)

    if result ~= false then
        clearThreat(miner)

        msg(string.format(
            "[miner_safety] miner #%d threat cleared, autonomous behavior resumed",
            id
        ))
    end
end

----------------------------------------------------------------
-- Threat scan
----------------------------------------------------------------

local function scanThreats(player, frame)
    local units = World.GetUnits()

    if not units then
        return
    end

    for _, miner in ipairs(units) do
        if isAlive(miner) and isMiner(miner) and ownedBy(player, miner) then

            local position = getPosition(miner)

            if position then

                -- Keep the assigned harvest cell fresh during the scan, so a
                -- later Stop always has a location to restore. Only overwrite
                -- with a valid cell; never clobber with a nil/cleared value.
                if getMission(miner) == "harvest" then
                    local loc = safeCall(miner.GetHarvestLocation, miner)
                    if loc and loc.x and loc.y then
                        local st = getMinerState(getId(miner))
                        st.harvestX = loc.x
                        st.harvestY = loc.y
                    end
                end

                local threat = findEnemyNear(
                    player,
                    position.x,
                    position.y,
                    THREAT_RADIUS
                )

                if threat then
                    if shouldStopMiner(miner) then
                        stopMiner(miner, frame, threat)
                    else
                        -- Miner is already stopped/idle.
                        -- Keep the state alive so it cannot immediately
                        -- resume while the threat remains.
                        local id = getId(miner)
                        local state = getMinerState(id)

                        state.threatened = true
                        state.threatId = getId(threat)
                        state.threatUntil =
                            frame + SAFE_RELEASE_DELAY
                    end
                else
                    tryResume(miner, frame)
                end
            end
        end
    end
end

----------------------------------------------------------------
-- Cleanup
----------------------------------------------------------------

local function cleanupStates()
    local units = World.GetUnits()

    if not units then
        return
    end

    local alive = {}

    for _, unit in ipairs(units) do
        if isAlive(unit) then
            local id = getId(unit)

            if id ~= 0 then
                alive[id] = true
            end
        end
    end

    for id, _ in pairs(miners) do
        if not alive[id] then
            miners[id] = nil
        end
    end
end

----------------------------------------------------------------
-- Refinery
----------------------------------------------------------------

local function findRefinery(player)
    local buildings = World.GetBuildings()

    if not buildings then
        return nil
    end

    for _, building in ipairs(buildings) do
        if isAlive(building)
            and REFINERY_TYPES[getTypeName(building)]
            and ownedBy(player, building) then

            local position = getPosition(building)

            if position then
                return {
                    x = math.floor(position.x),
                    y = math.floor(position.y)
                }
            end
        end
    end

    return nil
end

----------------------------------------------------------------
-- Miner count
----------------------------------------------------------------

local function countMiners(player)
    local units = World.GetUnits()

    if not units then
        return 0
    end

    local count = 0

    for _, unit in ipairs(units) do
        if isAlive(unit)
            and isMiner(unit)
            and ownedBy(player, unit) then

            count = count + 1
        end
    end

    return count
end

----------------------------------------------------------------
-- Respawn
----------------------------------------------------------------

local function maybeSpawn(player, frame)
    if countMiners(player) > 0 then
        return
    end

    local refinery = findRefinery(player)

    if not refinery then
        return
    end

    local threat = findEnemyNear(
        player,
        refinery.x,
        refinery.y,
        REFINERY_RADIUS
    )

    if threat then
        if frame - lastGlobalThreatLog >= LOG_COOLDOWN then
            lastGlobalThreatLog = frame

            msg(
                "[miner_safety] refinery under threat, delaying harvester spawn"
            )
        end

        return
    end

    if frame - lastSpawn < SPAWN_SCAN_EVERY then
        return
    end

    lastSpawn = frame

    local result = player:SpawnUnit(
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
        result
    ))
end

----------------------------------------------------------------
-- Main loop
----------------------------------------------------------------

function MOD.Update(frame)
    local player = House.GetPlayer()

    if not player then
        return
    end

    if frame % THREAT_SCAN_EVERY == 0 then
        scanThreats(player, frame)
        cleanupStates()
    end

    if frame % SPAWN_SCAN_EVERY == 0 then
        maybeSpawn(player, frame)
    end
end

return MOD