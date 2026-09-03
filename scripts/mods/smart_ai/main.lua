-- Smart AI (MVP): AI opponent that reads the capability registry.
-- DEFENSE: idle enemy jets intercept player units that approach their base.
-- OFFENSE: idle elite enemy jets strike the densest player cluster.
-- Registry-driven: only uses unit types and effects declared by loaded mods.
-- Hardened: every engine call is guarded (alive re-check + pcall) and the whole
-- update is wrapped in pcall so a bad unit never crashes the game.

local SmartAI = {}

local SCAN_EVERY = 30
local DEFENSE_RADIUS = 10
local CLUSTER_RADIUS = 5
local MIN_CLUSTER = 2
local ASSIGN_COOLDOWN = 90

local assignedCooldown = {}

local function msg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text, 1) end
end

local function vetRank(v)
    if v == "elite" then return 3 end
    if v == "veteran" then return 2 end
    return 1
end

local function caps()
    return _G.CapabilityRegistry or {}
end

local function isEnemyOf(player, owner)
    return owner ~= nil and owner ~= player and not owner:IsAlliedWith(player)
end

local function isAllyOf(player, owner)
    return owner ~= nil and (owner == player or owner:IsAlliedWith(player))
end

local function ready(id, frame)
    return (not assignedCooldown[id]) or frame >= assignedCooldown[id]
end

local function knownUnitTypes()
    local set = {}
    for _, cap in ipairs(caps()) do
        if cap.unitTypes then
            for t, _ in pairs(cap.unitTypes) do set[t] = true end
        end
    end
    return set
end

-- Guarded attack: alive re-check, diagnostic, pcall around the engine call.
local function safeAttack(jet, target, id, frame, modeMsg)
    if not jet:IsAlive() or not target:IsAlive() then
        return   -- unit vanished: skip, no crash
    end

    msg(string.format("[AI] attack: %s -> %s, jet alive=%s, target alive=%s",
        tostring(jet:GetTypeName()), tostring(target:GetTypeName()),
        tostring(jet:IsAlive()), tostring(target:IsAlive())))

    local ok, res = pcall(function() return jet:Attack(target) end)
    if not ok then
        msg(string.format("[AI] attack failed: %s", tostring(res)))
        return
    end
    if not res then
        msg(string.format("[AI] attack rejected: %s -> %s",
            tostring(jet:GetTypeName()), tostring(target:GetTypeName())))
        return
    end

    assignedCooldown[id] = frame + ASSIGN_COOLDOWN
    if modeMsg then msg(modeMsg) end
end

-- Threat model: pick the highest-scoring target (economy only when nothing
-- combat-worthy is around). GetPosition() returns map CELLS, so per-cell
-- distance is computed directly (no /CELL^2 needed).
local CELL = 256               -- leptons per map cell (defensive reference)

local ECONOMIC = { CMIN = true, HARV = true, SMIN = true }
local MCV_TYPES = { AMCV = true, SMV = true }
local HIGH_THREAT = {
    SREF = true,   -- Prism Tank
    APOC = true,   -- Apocalypse Tank
    TTNK = true,   -- Tesla Tank
    HTNK = true,   -- Rhino Tank
}

-- Veteran priority: elites/veterans are engaged before rookies.
local function vetBonus(unit)
    local v = unit:GetVeterancy()
    if v == "elite" then return 800 end
    if v == "veteran" then return 400 end
    return 0
end

local function threatScore(unit, fromX, fromY)
    local t = unit:GetTypeName()
    local base = 1000
    if ECONOMIC[t] then base = 5 end
    if MCV_TYPES[t] then base = 100 end
    if HIGH_THREAT[t] then base = 1500 end
    local ux, uy = unit:GetPosition()
    local dx, dy = ux - fromX, uy - fromY
    local distCells2 = dx * dx + dy * dy        -- positions are in cells
    local proximity = 500 / (1 + distCells2 / 100)
    return base + vetBonus(unit) + proximity
end

local function selectBestTarget(jet, candidates)
    local jx, jy = jet:GetPosition()
    local best, bestScore = nil, -1
    for _, c in ipairs(candidates) do
        if c:IsAlive() then
            local s = threatScore(c, jx, jy)
            if s > bestScore then
                bestScore = s
                best = c
            end
        end
    end
    return best
end

local function updateInner(frame)
    if frame % SCAN_EVERY ~= 0 then return end
    local player = House.GetPlayer()
    if not player then return end

    local unitTypes = knownUnitTypes()
    local hasAny = false
    for _ in pairs(unitTypes) do hasAny = true break end
    if not hasAny then return end

    local enemyJets = {}
    for _, u in ipairs(World.GetUnits()) do
        if u:IsAlive() and unitTypes[u:GetTypeName()] and isEnemyOf(player, u:GetOwner()) then
            enemyJets[#enemyJets + 1] = u
        end
    end
    if #enemyJets == 0 then return end

    -- DEFENSE: idle jets intercept the best-scoring player unit in range.
    for _, jet in ipairs(enemyJets) do
        if jet:IsIdle() then
            local id = jet:GetId()
            if ready(id, frame) then
                local jx, jy = jet:GetPosition()
                local arr = {}
                for _, e in ipairs(World.GetUnitsInRadius(jx, jy, DEFENSE_RADIUS)) do
                    if e:IsAlive() and e:GetKind() ~= "building" and isAllyOf(player, e:GetOwner()) then
                        arr[#arr + 1] = e
                    end
                end
                local target = selectBestTarget(jet, arr)
                if target then
                    safeAttack(jet, target, id, frame,
                        string.format("[AI] %s defends vs %s",
                            tostring(jet:GetTypeName()), tostring(target:GetTypeName())))
                end
            end
        end
    end

    -- OFFENSE: idle elite jets strike the densest player cluster.
    local offensiveCaps = {}
    for _, cap in ipairs(caps()) do
        if cap.effect and cap.minVeterancy then
            offensiveCaps[#offensiveCaps + 1] = cap
        end
    end
    if #offensiveCaps == 0 then return end

    local playerUnits = {}
    for _, u in ipairs(World.GetUnits()) do
        if u:IsAlive() and u:GetKind() == "unit" and isAllyOf(player, u:GetOwner()) then
            playerUnits[#playerUnits + 1] = u
        end
    end
    if #playerUnits < MIN_CLUSTER then return end

    local best = nil
    for _, pu in ipairs(playerUnits) do
        local px, py = pu:GetPosition()
        local n = 0
        local units = {}
        for _, nb in ipairs(World.GetUnitsInRadius(px, py, CLUSTER_RADIUS)) do
            if nb:IsAlive() and nb:GetKind() == "unit" and isAllyOf(player, nb:GetOwner()) then
                n = n + 1
                units[#units + 1] = nb
            end
        end
        if n >= MIN_CLUSTER and (not best or n > best.n) then
            best = { n = n, units = units }
        end
    end
    if not best then return end

    for _, jet in ipairs(enemyJets) do
        if jet:IsIdle() then
            for _, cap in ipairs(offensiveCaps) do
                if cap.unitTypes and cap.unitTypes[jet:GetTypeName()]
                    and vetRank(jet:GetVeterancy()) >= vetRank(cap.minVeterancy) then
                    local id = jet:GetId()
                    if ready(id, frame) then
                        local target = selectBestTarget(jet, best.units)
                        if target then
                            safeAttack(jet, target, id, frame,
                                string.format("[AI] %s strikes cluster of %d",
                                    tostring(jet:GetTypeName()), best.n))
                        end
                    end
                end
            end
        end
    end
end

function SmartAI.Update(frame)
    local ok, err = pcall(updateInner, frame)
    if not ok then
        msg(string.format("[AI] update error: %s", tostring(err)))
    end
end

return SmartAI
