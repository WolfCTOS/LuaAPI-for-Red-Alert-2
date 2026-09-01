-- Patrol Demo (Milestone 13 / Gate 13.1).
--
-- Vertical slice of the Unit Control API. Uses native methods only
-- (no ActiveClickWith hook): GetTarget/GetMission/MoveTo/Attack/Stop/IsIdle.
-- Out-of-the-box: skips MCVs, spawns a LTNK if the player has combat units,
-- throttles Attack, and ignores neutrals/civilians/vehicles.

local PatrolDemo = {}

local PATROL_OFFSET = 6
local ENEMY_RADIUS  = 35
local TICK_EVERY    = 20
local ATTACK_COOLDOWN = 100   -- кадров между Attack на одну и ту же цель

-- MCV: нет оружия, цель не закрепляется — патрулировать их не нужно.
local MCV_TYPES = { AMCV = true, SMCV = true, YMCV = true }
-- Гражданские/нейтральные дома и транспорты — не цели.
local IGNORED_HOUSES = { Neutral = true, Civilian = true, Special = true }
local IGNORED_TYPES  = { CAR = true, PCV = true, BUS = true, TRUCK = true }

local warnedNoUnit = false
local spawned      = false

local anchor     = {}   -- unitId -> {x, y}
local leg        = {}   -- unitId -> "A" | "B"
local lastAttack = {}   -- unitId -> {target = id, frame = n}

local function printMsg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text) end
end

local function isEnemyOf(player, unit)
    if not unit or not unit:IsAlive() then return false end
    local owner = unit:GetOwner()
    if not owner then return false end

    -- Игнорируем нейтралов/гражданских и гражданский транспорт.
    local houseName = owner:GetName()
    if IGNORED_HOUSES[houseName] then return false end
    local typeName = unit:GetTypeName()
    if IGNORED_TYPES[typeName] then return false end

    -- Страховка: сравниваем и по ссылке, и по альянсу (кэш домов в C++).
    return owner ~= player and not owner:IsAlliedWith(player)
end

local function isMcvUnit(unit)
    local t = unit:GetTypeName()
    return t ~= nil and MCV_TYPES[t] == true
end

local function isOwnedBy(player, unit)
    local owner = unit:GetOwner()
    return owner and (owner == player or owner:IsAlliedWith(player))
end

local function firstPlayerUnit(player)
    local units = World.GetUnits()
    if not units then return nil end
    for _, unit in ipairs(units) do
        if unit:IsAlive() and not isMcvUnit(unit) and isOwnedBy(player, unit) then
            return unit
        end
    end
    return nil
end

-- Координаты базы игрока (первое своё здание) — как точка спавна LTNK.
local function playerBasePos(player)
    local buildings = World.GetBuildings()
    if not buildings then return nil end
    for _, b in ipairs(buildings) do
        if b:IsAlive() and isOwnedBy(player, b) then
            local p = b:GetPosition()
            if p then return p.x, p.y end
        end
    end
    return nil
end

function PatrolDemo.Update(frame)
    if frame % TICK_EVERY ~= 0 then return end

    local player = House.GetPlayer()
    if not player then return end

    local unit = firstPlayerUnit(player)

    if not unit then
        -- Самодостаточный тест: спавним себе боевой юнит один раз.
        if not spawned then
            spawned = true
            local x, y = playerBasePos(player)
            if not x then x, y = 26, 26 end
            local n = player:SpawnUnit("LTNK", 1, math.floor(x) + 2, math.floor(y) + 2, 0, false, "guard")
            printMsg(string.format("[PATROL] spawned combat unit (LTNK) at (%d,%d), ret=%d",
                math.floor(x) + 2, math.floor(y) + 2, n))
        elseif not warnedNoUnit then
            printMsg("[PATROL] no player combat unit found, waiting...")
            warnedNoUnit = true
        end
        return
    end
    warnedNoUnit = false

    local id = unit.GetId and unit:GetId() or 1

    if not anchor[id] then
        local position = unit:GetPosition()
        if not position then
            printMsg(string.format("[PATROL] unit #%d has no position", id))
            return
        end
        anchor[id] = { x = math.floor(position.x), y = math.floor(position.y) }
        leg[id] = "A"
        printMsg(string.format("[PATROL] unit #%d anchored at (%d,%d)",
            id, anchor[id].x, anchor[id].y))
    end

    local position = unit:GetPosition()
    if not position then return end

    -- 1) Поиск врага в радиусе.
    local enemy = nil
    local nearbyUnits = World.GetUnitsInRadius(position.x, position.y, ENEMY_RADIUS)
    if nearbyUnits then
        for _, candidate in ipairs(nearbyUnits) do
            if isEnemyOf(player, candidate) then
                enemy = candidate
                break
            end
        end
    end

    if enemy then
        local currentTarget = unit:GetTarget()
        local enemyId = enemy.GetId and enemy:GetId() or -1
        local currentTargetId = currentTarget and currentTarget.GetId and currentTarget:GetId() or -1
        local last = lastAttack[id]
        local sameTarget = last and last.target == enemyId
        local inCooldown = last and (frame - last.frame) < ATTACK_COOLDOWN

        -- Атакуем только смену цели и не чаще раза в ATTACK_COOLDOWN кадров.
        if currentTargetId ~= enemyId then
            if not (sameTarget and inCooldown) then
                if unit:Attack(enemy) then
                    lastAttack[id] = { target = enemyId, frame = frame }
                    printMsg(string.format("[PATROL] unit #%d ATTACK -> enemy #%d (%s)",
                        id, enemyId, enemy:GetTypeName()))
                else
                    printMsg(string.format("[PATROL] unit #%d Attack REJECTED", id))
                end
            end
        end
        return
    end

    -- 2) Врага нет — сброс закреплённой цели.
    if unit:GetTarget() then
        if unit:Stop() then
            lastAttack[id] = nil
            printMsg(string.format("[PATROL] unit #%d STOP (target cleared)", id))
        end
        return
    end

    -- 3) Патруль между двумя точками.
    if unit:IsIdle() then
        local a = anchor[id]
        local destination
        if leg[id] == "A" then
            destination = { x = a.x + PATROL_OFFSET, y = a.y }
            leg[id] = "B"
        else
            destination = { x = a.x, y = a.y }
            leg[id] = "A"
        end
        if unit:MoveTo(destination.x, destination.y) then
            printMsg(string.format("[PATROL] unit #%d MOVE -> (%d,%d)",
                id, destination.x, destination.y))
        else
            printMsg(string.format("[PATROL] unit #%d Move REJECTED -> (%d,%d)",
                id, destination.x, destination.y))
        end
    end
end

return PatrolDemo
