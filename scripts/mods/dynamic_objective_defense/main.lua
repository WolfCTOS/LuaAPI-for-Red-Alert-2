-- Dynamic Objective Defense (manual assignment).
--
-- Public showcase of the Unit Control slice. Nothing happens until you pick a
-- combat unit in-game and press Numpad1. Featured APIs:
--   Input.WasKeyPressed(vk)              — Numpad1 (0x61) edge-triggered
--   World.GetSelectedUnits()             — read the engine's current selection
--   World.GetUnitsInRadius / World.GetUnits — scanning
--   unit:Attack / unit:MoveTo / unit:Stop / unit:IsIdle / unit:GetId
--   unit:GetPosition / unit:GetTarget / unit:GetKind / unit:GetTypeName
--   unit:GetOwner / house:GetName / house:IsAlliedWith
-- All logic runs inside Update(); no event hooks.

local MOD = {}

local TICK_EVERY      = 20    -- frames between defense decisions
local PATROL_OFFSET   = 6     -- patrol points at +/-6 cells around the objective
local DEFENSE_RADIUS  = 15    -- enemy scan radius around the objective
local ATTACK_COOLDOWN = 100   -- frames between Attack calls on the same target
local ASSIGN_RADIUS   = 20    -- how far the objective may be from the unit

local KEY_NUMPAD1 = 0x61

local NEUTRAL_HOUSES = { Neutral = true, Civilian = true, Special = true }
local CIVIL_VEHICLES = { CAR = true, PCV = true, BUS = true, TRUCK = true }

local assigned = {}   -- unitId -> { x, y, typeName }
local leg      = {}   -- unitId -> 1 | 2   (which patrol flank is next)
local lastAttack = {} -- unitId -> { target = id, frame = n }

local function msg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text) end
end

local function isEnemy(player, unit)
    if not unit or not unit:IsAlive() then return false end
    local owner = unit:GetOwner()
    if not owner then return false end
    if NEUTRAL_HOUSES[owner:GetName()] then return false end
    if CIVIL_VEHICLES[unit:GetTypeName()] then return false end
    return owner ~= player and not owner:IsAlliedWith(player)
end

-- Objective buildings: oil derrick, hospital, airport, or any captured "CA*".
local function isObjectiveType(candidate, ownerName)
    local t = candidate:GetTypeName()
    if t == "CAOILD" or t == "CAHOSP" or t == "CAAIRP" then return true end
    if string.sub(t, 1, 2) == "CA" and ownerName ~= "Neutral"
       and ownerName ~= "Civilian" and ownerName ~= "Special" then
        return true
    end
    return false
end

-- Nearest objective building within ASSIGN_RADIUS of the unit.
local function nearestObjective(unit)
    local p = unit:GetPosition()
    if not p then return nil end
    local best, bestDist = nil, ASSIGN_RADIUS
    local nearby = World.GetUnitsInRadius(p.x, p.y, ASSIGN_RADIUS)
    if not nearby then return nil end
    for _, c in ipairs(nearby) do
        if c:GetKind() == "building" then
            local owner = c:GetOwner()
            local ownerName = owner and owner:GetName() or ""
            if isObjectiveType(c, ownerName) then
                local cp = c:GetPosition()
                if cp then
                    local dx, dy = cp.x - p.x, cp.y - p.y
                    local d = math.sqrt(dx * dx + dy * dy)
                    if d <= bestDist then
                        bestDist = d
                        best = { x = math.floor(cp.x), y = math.floor(cp.y), typeName = c:GetTypeName() }
                    end
                end
            end
        end
    end
    return best
end

-- Defend one assigned objective: attack intruders, then patrol the flanks.
local function defendUnit(player, id, unit, objective, frame)
    local enemy = nil
    local nearby = World.GetUnitsInRadius(objective.x, objective.y, DEFENSE_RADIUS)
    if nearby then
        for _, c in ipairs(nearby) do
            if isEnemy(player, c) then enemy = c; break end
        end
    end

    if enemy then
        local enemyId = enemy.GetId and enemy:GetId() or -1
        local cur = unit:GetTarget()
        local curId = cur and cur.GetId and cur:GetId() or -1
        local last = lastAttack[id]
        local sameTarget = last and last.target == enemyId
        local inCooldown = last and (frame - last.frame) < ATTACK_COOLDOWN
        if curId ~= enemyId and not (sameTarget and inCooldown) then
            if unit:Attack(enemy) then
                lastAttack[id] = { target = enemyId, frame = frame }
            end
        end
        return
    end

    if unit:GetTarget() then
        if unit:Stop() then
            lastAttack[id] = nil
        end
        return
    end

    if unit:IsIdle() then
        local point = (leg[id] == 1)
            and { x = objective.x - PATROL_OFFSET, y = objective.y }
            or  { x = objective.x + PATROL_OFFSET, y = objective.y }
        leg[id] = (leg[id] == 1) and 2 or 1
        unit:MoveTo(point.x, point.y)
    end
end

function MOD.Update(frame)
    local player = House.GetPlayer()
    if not player then return end

    -- Numpad1: assign the selected unit, or release it if already assigned.
    if Input.WasKeyPressed(KEY_NUMPAD1) then
        local sel = World.GetSelectedUnits()
        local unit = sel and sel[1]
        if not unit then
            msg("[DEFENSE] no unit selected")
        else
            local id = unit.GetId and unit:GetId() or 0
            if assigned[id] then
                assigned[id] = nil
                leg[id] = nil
                lastAttack[id] = nil
                unit:Stop()
                msg("[DEFENSE] unit released")
            else
                local obj = nearestObjective(unit)
                if obj then
                    assigned[id] = { x = obj.x, y = obj.y, typeName = obj.typeName }
                    leg[id] = 1
                    msg(string.format("[DEFENSE] unit assigned to %s at (%d,%d)", obj.typeName, obj.x, obj.y))
                else
                    msg("[DEFENSE] no objective nearby")
                end
            end
        end
    end

    if frame % TICK_EVERY ~= 0 then return end

    -- Defend every assigned unit that is still alive; drop dead ones.
    local live = {}
    local units = World.GetUnits()
    if units then
        for _, u in ipairs(units) do
            live[u.GetId and u:GetId() or 0] = u
        end
    end
    for id, objective in pairs(assigned) do
        local unit = live[id]
        if unit and unit:IsAlive() then
            defendUnit(player, id, unit, objective, frame)
        else
            assigned[id] = nil
            leg[id] = nil
            lastAttack[id] = nil
        end
    end
end

return MOD
