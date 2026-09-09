```lua
-- Tactical Patrol (Milestone 14 / Gate 14.7).
--
-- Patrols between strategic buildings instead of using an artificial
-- +/-6 cell patrol around the base.
--
-- Preferred patrol targets:
--   - Airfield / Airport
--   - Oil Derrick
--
-- If one or both targets are unavailable, the showcase falls back to
-- available strategic buildings and finally to the player's base.
--
-- Architecture:
--     Tactical Patrol
--          ↓
--     UnitController
--          ↓
--     Task
--          ↓
--     Query
--          ↓
--     existing LuaAPI primitives
--
-- No C++ changes are required.

local Framework   = require("framework.init")
local UnitControl = Framework.UnitController
local Query       = Framework.Query
local EventBus    = Framework.EventBus

local TacticalPatrol = {}

local ENEMY_RADIUS = 35
local DECIDE_EVERY = 10

-- Known RA2/YR building type IDs.
local AIRFIELD_TYPES = {
    GAAIRC = true, -- Allied Air Force Command
    NAAIRC = true, -- Soviet Air Force Command
    YAAIRC = true, -- Yuri Airforce Command
}

local OIL_DERRICK_TYPES = {
    CAOILD = true, -- Oil Derrick
}

local MCV_TYPES = {
    AMCV = true,
    SMCV = true,
    YMCV = true,
}

local controller = nil
local patrolPoints = nil
local patrolTargets = nil

local spawned = false
local warnedNoUnit = false
local eventsWired = false


local function msg(text)
    if Engine and Engine.PrintMessage then
        Engine.PrintMessage(text)
    end
end


local function isOwnedBy(player, unit)
    if not unit or not unit.IsAlive then
        return false
    end

    local owner = unit:GetOwner()

    return owner ~= nil
        and (owner == player or owner:IsAlliedWith(player))
end


local function isMcv(unit)
    local t = unit:GetTypeName()
    return t ~= nil and MCV_TYPES[t] == true
end


local function buildingPosition(building)
    if not building or not building.IsAlive or not building:IsAlive() then
        return nil
    end

    local p = building:GetPosition()

    if not p then
        return nil
    end

    return {
        x = math.floor(p.x),
        y = math.floor(p.y),
    }
end


-- Find the first player-owned building matching a type set.
local function findBuilding(player, typeSet)
    local buildings = World.GetBuildings()

    if not buildings then
        return nil
    end

    for _, building in ipairs(buildings) do
        if isOwnedBy(player, building) then
            local typeName = building:GetTypeName()

            if typeName and typeSet[typeName] then
                local position = buildingPosition(building)

                if position then
                    return {
                        unit = building,
                        position = position,
                        typeName = typeName,
                    }
                end
            end
        end
    end

    return nil
end


-- Find the first player-owned building that can be used as a fallback anchor.
local function findBaseBuilding(player)
    local buildings = World.GetBuildings()

    if not buildings then
        return nil
    end

    for _, building in ipairs(buildings) do
        if isOwnedBy(player, building) then
            local position = buildingPosition(building)

            if position then
                return {
                    unit = building,
                    position = position,
                    typeName = building:GetTypeName(),
                }
            end
        end
    end

    return nil
end


local function firstPlayerUnit(player)
    local units = World.GetUnits()

    if not units then
        return nil
    end

    for _, unit in ipairs(units) do
        if unit:IsAlive()
            and not isMcv(unit)
            and isOwnedBy(player, unit)
        then
            return unit
        end
    end

    return nil
end


-- Build the strategic patrol route.
--
-- Preferred:
--   Airfield <-> Oil Derrick
--
-- Fallback:
--   Airfield <-> Base
--   Oil Derrick <-> Base
--   Base <-> Base (single point)
local function buildPatrolRoute(player)
    local airfield = findBuilding(player, AIRFIELD_TYPES)
    local oilDerrick = findBuilding(player, OIL_DERRICK_TYPES)
    local base = findBaseBuilding(player)

    local points = {}
    local targets = {}

    if airfield then
        table.insert(points, airfield.position)
        table.insert(targets, airfield)

        msg(string.format(
            "[TACTICAL] patrol target: %s at (%d,%d)",
            airfield.typeName,
            airfield.position.x,
            airfield.position.y
        ))
    end

    if oilDerrick then
        table.insert(points, oilDerrick.position)
        table.insert(targets, oilDerrick)

        msg(string.format(
            "[TACTICAL] patrol target: %s at (%d,%d)",
            oilDerrick.typeName,
            oilDerrick.position.x,
            oilDerrick.position.y
        ))
    end

    -- If we have both strategic targets, this is the intended route.
    if #points >= 2 then
        return points, targets
    end

    -- Fallback: use the available strategic building and the base.
    if base then
        if #points == 0 then
            table.insert(points, base.position)
            table.insert(targets, base)
        else
            table.insert(points, base.position)
            table.insert(targets, base)
        end

        msg("[TACTICAL] using base as patrol fallback")
    end

    -- Last resort: fixed location.
    if #points == 0 then
        points = {
            { x = 26, y = 26 },
            { x = 32, y = 26 },
        }

        targets = {}

        msg("[TACTICAL] no strategic buildings found, using fallback coordinates")
    elseif #points == 1 then
        -- UnitController patrol() expects multiple useful points.
        local p = points[1]

        table.insert(points, {
            x = p.x + 6,
            y = p.y,
        })
    end

    return points, targets
end


local function onTaskDone(task, status, mode)
    if mode == "attack" then
        msg(string.format(
            "[TACTICAL] combat ended (%s), resuming patrol",
            tostring(status)
        ))

        if controller and patrolPoints then
            controller:patrol(patrolPoints)
        end
    end
end


local function ensureController(unit)
    if controller
        and controller:is_alive()
        and controller.id == unit:GetId()
    then
        return controller
    end

    controller = UnitControl.new(unit, {
        onTaskDone = onTaskDone,
    })

    return controller
end


local function decide(player, frame)
    if not controller then
        return
    end

    local unit = controller:unit()

    if not unit then
        return
    end

    local enemy = Query.nearest_enemy(unit, ENEMY_RADIUS)

    local alreadyEngaged =
        controller:has_task()
        and controller:get_mode() == "attack"

    if enemy and not alreadyEngaged then
        msg(string.format(
            "[TACTICAL] enemy detected: %s, engaging",
            tostring(enemy:GetTypeName())
        ))

        controller:attack(enemy, {
            timeout = 900,
        })

        return
    end

    if not controller:has_task() and patrolPoints then
        controller:patrol(patrolPoints)

        msg("[TACTICAL] patrol resumed")
    end
end


function TacticalPatrol.Update(frame)
    Framework.update(frame)

    -- Wire EventBus once per session.
    if not eventsWired then
        eventsWired = true

        Framework.enableUnitEvents(30)

        EventBus.on("unit_destroyed", function(id, snap)
            msg(string.format(
                "[TACTICAL] unit #%d (%s) destroyed",
                id,
                snap and snap.typeName or "?"
            ))
        end)
    end

    local player = House.GetPlayer()

    if not player then
        return
    end

    -- Find or spawn the patrol unit.
    if not controller then
        local unit = firstPlayerUnit(player)

        if not unit then
            if not spawned then
                spawned = true

                local base = findBaseBuilding(player)

                local x = base and base.position.x or 26
                local y = base and base.position.y or 26

                local n = player:SpawnUnit(
                    "LTNK",
                    1,
                    x + 2,
                    y + 2,
                    0,
                    false,
                    "guard"
                )

                msg(string.format(
                    "[TACTICAL] spawned combat unit (LTNK), ret=%d",
                    n
                ))
            elseif not warnedNoUnit then
                warnedNoUnit = true
                msg("[TACTICAL] no player combat unit found, waiting...")
            end

            return
        end

        warnedNoUnit = false

        ensureController(unit)

        -- Build the strategic patrol route once.
        patrolPoints, patrolTargets = buildPatrolRoute(player)

        msg(string.format(
            "[TACTICAL] strategic patrol initialized with %d points",
            #patrolPoints
        ))

        if #patrolTargets >= 2 then
            msg("[TACTICAL] route: strategic building <-> strategic building")
        end

        controller:patrol(patrolPoints)
    end

    -- Advance movement/combat/task lifecycle every frame.
    if controller then
        controller:update(frame)
    end

    if frame % DECIDE_EVERY ~= 0 then
        return
    end

    decide(player, frame)
end


return TacticalPatrol
```
