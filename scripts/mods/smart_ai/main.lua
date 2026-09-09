-- Smart AI: one brain for the AI opponent (defense + offense + escort + squads).
-- DEFENSE: idle enemy units intercept player units that approach their base.
-- OFFENSE: idle elite enemy jets strike the densest player cluster.
-- SQUADS: independent attack forces via the framework ForceGroup manager
--   (merged from the retired tactical_reassess mod): observe -> evaluate ->
--   decide -> act -> reassess per squad, plus SEEK toward the enemy.
-- ESCORT: engineer/officer convoy at the protected unit's pace.
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

local HEARTBEAT_EVERY = 300  -- ~5 s at 60 fps: prove the mod is alive even with no orders
local lastHeartbeat = 0

-- Units the fallback must never command (economy + capture duty).
local NON_COMBAT_FALLBACK = {
    HARV = true, CMIN = true, SMIN = true,   -- harvesters
    AMCV = true, SMV = true,                 -- MCVs
    E3 = true, ENGINEER = true,              -- engineers
}

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

-- Neutrals/civilians are never targets: no defense against them, no bounty
-- on traffic, no cluster strikes including them. Engineers still capture
-- neutral buildings via the escort logic — that path is untouched.
local NEUTRAL_HOUSES = { Neutral = true, Civilian = true, Special = true }
local CIVIL_TYPES = { CAR = true, PCV = true, BUS = true, TRUCK = true }

local function legitTarget(player, e)
    if not e or not e:IsAlive() then return false end
    local okO, owner = pcall(e.GetOwner, e)
    if not okO or not owner then return false end
    local okN, nm = pcall(owner.GetName, owner)
    if okN and nm and NEUTRAL_HOUSES[nm] then return false end
    local okT, tn = pcall(e.GetTypeName, e)
    if okT and tn and CIVIL_TYPES[tn] then return false end
    return isAllyOf(player, owner)
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
    local upos = unit:GetPosition()
    if not upos then return 0 end
    local ux, uy = upos.x, upos.y
    local dx, dy = ux - fromX, uy - fromY
    local distCells2 = dx * dx + dy * dy        -- positions are in cells
    local proximity = 500 / (1 + distCells2 / 100)
    return base + vetBonus(unit) + proximity
end

local function selectBestTarget(jet, candidates)
    local jpos = jet:GetPosition()
    if not jpos then return nil end
    local jx, jy = jpos.x, jpos.y
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

    -- Fallback: registry empty (nobody declared unitTypes) — track any
    -- enemy mobile combat unit by GetKind, otherwise the mod stays silent.
    local enemyJets = {}
    for _, u in ipairs(World.GetUnits()) do
        if u:IsAlive() and isEnemyOf(player, u:GetOwner()) then
            if hasAny then
                if unitTypes[u:GetTypeName()] then
                    enemyJets[#enemyJets + 1] = u
                end
            else
                -- Fallback commands fighters only: never conscript harvesters,
                -- MCVs or engineers. An idle miner/MCV at match start would
                -- otherwise abandon the economy for a fight and stall the AI.
                local kind = u:GetKind()
                if kind == "unit" or kind == "infantry" or kind == "aircraft" then
                    local tn = u:GetTypeName()
                    if not NON_COMBAT_FALLBACK[tn] then
                        enemyJets[#enemyJets + 1] = u
                    end
                end
            end
        end
    end

    if frame - lastHeartbeat >= HEARTBEAT_EVERY then
        lastHeartbeat = frame
        msg(string.format("[AI] heartbeat frame=%d: %d enemy combat units under watch (types=%s)",
            frame, #enemyJets, hasAny and "registry" or "fallback"))
    end

    if #enemyJets == 0 then return end

    -- One-commander rule: squad members are commanded by the squad brain,
    -- never by defense/offense.
    local squadIds = squadTrackedSet()

    -- DEFENSE: idle jets intercept the best-scoring player unit in range.
    for _, jet in ipairs(enemyJets) do
        local okJ, jid = pcall(jet.GetId, jet)
        if okJ and jid and not squadIds[jid] and jet:IsIdle() then
            local id = jid
            if ready(id, frame) then
                local jpos = jet:GetPosition()
                if jpos then
                    local arr = {}
                    for _, e in ipairs(World.GetUnitsInRadius(jpos.x, jpos.y, DEFENSE_RADIUS)) do
                        if e:GetKind() ~= "building" and legitTarget(player, e) then
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
        if u:GetKind() == "unit" and legitTarget(player, u) then
            playerUnits[#playerUnits + 1] = u
        end
    end
    if #playerUnits < MIN_CLUSTER then return end

    local best = nil
    for _, pu in ipairs(playerUnits) do
        local ppos = pu:GetPosition()
        if not ppos then
            return
        end
        local px, py = ppos.x, ppos.y
        local n = 0
        local units = {}
        for _, nb in ipairs(World.GetUnitsInRadius(px, py, CLUSTER_RADIUS)) do
            if nb:GetKind() == "unit" and legitTarget(player, nb) then
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
        local okJ, jid = pcall(jet.GetId, jet)
        if okJ and jid and not squadIds[jid] and jet:IsIdle() then
            for _, cap in ipairs(offensiveCaps) do
                if cap.unitTypes and cap.unitTypes[jet:GetTypeName()]
                    and vetRank(jet:GetVeterancy()) >= vetRank(cap.minVeterancy) then
                    local id = jid
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

-- ===========================================================================
-- Squads: independent attack forces of the AI house (merged from the retired
-- tactical_reassess mod so ONE brain commands everything). Each squad runs its
-- own Observe -> Evaluate -> Decide -> Act -> Reassess loop via the framework
-- ForceGroup manager, so squads in different places reach different decisions.
-- One-commander rule: squad members are excluded from the defense/offense
-- pools above and from escort duty below — see squadTrackedSet().
-- ===========================================================================

local ForceGroup = require("framework.force_group")
local futil = require("framework.util")

local SQUAD_NUM          = 3    -- independent squads attacking in parallel
local SQUAD_SIZE         = 3    -- combat units per squad
local SQUAD_TICK_EVERY   = 5    -- frames between squad updates (orders re-issued)
local SQUAD_GATHER_EVERY = 30   -- frames between recruiting fresh AI units
local SQUAD_SCAN_RADIUS  = 18   -- evaluation radius (cells)
local SQUAD_RANGE        = 14   -- how far an enemy must be to be attackable
local SQUAD_RETREAT_DIST = 12
local SQUAD_RETREAT_HOLD = 90
local SQUAD_TRACK_EVERY  = 1800 -- ~30 s: position track (movement proof)

local squadMgr = nil
local squadHouseIndex = nil
local squadLastHouseIdx = nil
local squadWarnedNoHouse = false
local squadLastLogged = {}   -- group id -> last logged decision (change-only)
local squadLastStats = nil   -- "available/in squads" key (change-only)

local function squadLog(text)
    print("[TACTICAL] " .. text)
end

-- The AI house for squads: first non-human, non-neutral house at war with the
-- player. Cached by index so squads don't hop between houses mid-match.
local function squadHouse()
    local okP, player = pcall(House.GetPlayer)
    if not okP or not player then return nil end
    local function acceptable(h)
        if not h then return false end
        local okN, nm = pcall(h.GetName, h)
        if not okN or not nm or NEUTRAL_HOUSES[nm] then return false end
        local okE, enemy = pcall(isEnemyOf, player, h)
        return okE and enemy
    end
    if squadHouseIndex ~= nil then
        local okC, h = pcall(House.GetByIndex, squadHouseIndex)
        if okC and acceptable(h) then return h end
        squadHouseIndex = nil
    end
    local okN, count = pcall(House.GetCount)
    if not okN or not count then return nil end
    for i = 0, count - 1 do
        local okH, h = pcall(House.GetByIndex, i)
        if okH and acceptable(h) then
            squadHouseIndex = i
            return h
        end
    end
    return nil
end

local function ensureSquads()
    if squadMgr then return end
    squadMgr = ForceGroup.new({})
    for i = 1, SQUAD_NUM do
        local gid = "squad_" .. i
        local g = squadMgr:add_group({
            id = gid,
            getHouse = squadHouse,
            radius = SQUAD_SCAN_RADIUS,
            pulseEvery = 15,
            rangeRadius = SQUAD_RANGE,
            retreatDist = SQUAD_RETREAT_DIST,
            retreatHold = SQUAD_RETREAT_HOLD,
        })
        g:set_handler("onDecision", function(grp, res)
            local dec = tostring(res.decision)
            if squadLastLogged[grp.id] ~= dec then
                squadLastLogged[grp.id] = dec
                squadLog(string.format("[%s] %s (tier=%s reason=%s)", tostring(grp.id),
                    string.upper(dec), tostring(res.tier), tostring(res.reason)))
                msg(string.format("[TACTICAL] [%s] %s", tostring(grp.id), string.upper(dec)))
            end
        end)
    end
    squadLog(string.format("initialized: %d squads x %d units", SQUAD_NUM, SQUAD_SIZE))
end

-- Sorted group ids (deterministic fill order, no pairs() randomness).
local function sortedSquadIds()
    local out = squadMgr:group_ids()
    table.sort(out, function(a, b) return tostring(a) < tostring(b) end)
    return out
end

-- All unit ids currently commanded by squads (the one-commander rule).
local function squadTrackedSet()
    local set = {}
    if not squadMgr then return set end
    for _, gid in ipairs(sortedSquadIds()) do
        local g = squadMgr:group(gid)
        if g then
            for _, id in ipairs(g:ids()) do set[id] = true end
        end
    end
    return set
end

local function isSquadRecruit(u, house, tracked)
    if not u then return false end
    local okA, alive = pcall(u.IsAlive, u)
    if not okA or not alive then return false end
    -- Active escorts stay on convoy duty, never drafted into squads.
    if escort then
        local okE, eid = pcall(u.GetId, u)
        if okE and eid and escort.escorts and escort.escorts[eid] then return false end
    end
    local okO, owner = pcall(u.GetOwner, u)
    if not okO or owner == nil or owner ~= house then return false end
    local okK, kind = pcall(u.GetKind, u)
    if not okK or (kind ~= "unit" and kind ~= "infantry" and kind ~= "aircraft") then
        return false
    end
    local okT, tn = pcall(u.GetTypeName, u)
    if okT and tn and NON_COMBAT_FALLBACK[tn] then return false end
    local okI, id = pcall(u.GetId, u)
    if not okI or not id or tracked[id] then return false end
    return true
end

-- Fill squads up to SQUAD_SIZE each with fresh AI-house combat units.
local function gatherSquads(house)
    local tracked = squadTrackedSet()
    local okU, units = pcall(World.GetUnits)
    if not okU or not units then return end
    for _, u in ipairs(units) do
        if isSquadRecruit(u, house, tracked) then
            for _, gid in ipairs(sortedSquadIds()) do
                local g = squadMgr:group(gid)
                if g and g:count() < SQUAD_SIZE then
                    local okT, id = pcall(g.add_member, g, u)
                    if okT and id then
                        tracked[id] = true
                        local okN, tn = pcall(u.GetTypeName, u)
                        squadLog(string.format("[%s] recruit: %s #%d", tostring(gid),
                            tostring(okN and tn or "?"), id))
                    end
                    break
                end
            end
        end
    end
end

local function countAiCombat(house)
    local n = 0
    local okU, units = pcall(World.GetUnits)
    if not okU or not units then return 0 end
    for _, u in ipairs(units) do
        local okA, alive = pcall(u.IsAlive, u)
        if okA and alive then
            local okO, owner = pcall(u.GetOwner, u)
            if okO and owner ~= nil and owner == house then
                local okK, kind = pcall(u.GetKind, u)
                if okK and (kind == "unit" or kind == "infantry" or kind == "aircraft") then
                    local okT, tn = pcall(u.GetTypeName, u)
                    if not (okT and tn and NON_COMBAT_FALLBACK[tn]) then n = n + 1 end
                end
            end
        end
    end
    return n
end

local function squadTrackedTotal()
    local m = 0
    if not squadMgr then return 0 end
    for _, gid in ipairs(sortedSquadIds()) do
        local g = squadMgr:group(gid)
        if g then m = m + g:count() end
    end
    return m
end

-- Nearest legitimate enemy position to (cx, cy) plus distance in cells.
local function squadEnemyPos(house, cx, cy)
    local listFn = World.GetAllUnits or World.GetUnits
    if not listFn then return nil end
    local okU, units = pcall(listFn)
    if not okU or not units then return nil end
    local bx, by, bestD = nil, nil, math.huge
    for _, u in ipairs(units) do
        -- futil.is_enemy: same house / allies / neutrals / civilians are never
        -- enemies — SEEK must not march squads onto capturable derricks.
        if futil.is_enemy(house, u) then
            local okP, pos = pcall(u.GetPosition, u)
            if okP and pos and pos.x and pos.y then
                local dx, dy = pos.x - cx, pos.y - cy
                local d = dx * dx + dy * dy
                if d < bestD then bestD, bx, by = d, pos.x, pos.y end
            end
        end
    end
    if not bx then return nil end
    return bx, by, math.sqrt(bestD)
end

local function squadMemberById(id)
    local ok, units = pcall(World.GetUnits)
    if not ok or not units then return nil end
    for _, u in ipairs(units) do
        local okId, uid = pcall(u.GetId, u)
        if okId and uid == id then return u end
    end
    return nil
end

-- SEEK: a squad with no target advances toward the nearest enemy instead of
-- idling at the AI base. Retreat/disengage are respected (no seek).
local function seekSquads(house)
    for _, gid in ipairs(sortedSquadIds()) do
        local g = squadMgr:group(gid)
        if g then
            local res = g:decision()
            if res and res.decision == "find_target" then
                local cx, cy = g:centroid()
                if cx then
                    local ex, ey, dist = squadEnemyPos(house, cx, cy)
                    if ex and dist and dist > SQUAD_RANGE then
                        for _, id in ipairs(g:ids()) do
                            local u = squadMemberById(id)
                            if u then pcall(u.MoveTo, u, math.floor(ex), math.floor(ey)) end
                        end
                    end
                end
            end
        end
    end
end

local function squadTick(frame)
    ensureSquads()
    local house = squadHouse()
    if not house then
        if not squadWarnedNoHouse then
            squadWarnedNoHouse = true
            squadLog("no AI house found; squads idle")
        end
        return
    end
    squadWarnedNoHouse = false

    -- Opponent changed (new match / new house): drop old squads, recruit fresh.
    if squadLastHouseIdx ~= squadHouseIndex then
        squadLastHouseIdx = squadHouseIndex
        squadMgr:reset()
        squadLastLogged = {}
        squadLastStats = nil
        local okNm, hname = pcall(house.GetName, house)
        squadLog(string.format("opponent: %s; squads reset", tostring(okNm and hname or "?")))
    end

    if frame % SQUAD_GATHER_EVERY == 0 then
        gatherSquads(house)
        local avail, insquads = countAiCombat(house), squadTrackedTotal()
        local key = avail .. "/" .. insquads
        if squadLastStats ~= key then
            squadLastStats = key
            squadLog(string.format("AI combat units: %d available, %d in squads", avail, insquads))
        end
    end

    if frame % SQUAD_TICK_EVERY == 0 then
        squadMgr:update(frame)
        seekSquads(house)
    end

    -- Movement proof: centroid + nearest-enemy distance per squad.
    if frame % SQUAD_TRACK_EVERY == 0 then
        for _, gid in ipairs(sortedSquadIds()) do
            local g = squadMgr:group(gid)
            if g then
                local cx, cy = g:centroid()
                if cx then
                    local res = g:decision()
                    local ex, ey, dist = squadEnemyPos(house, cx, cy)
                    squadLog(string.format("[%s] pos=%d,%d members=%d decision=%s enemy=%s",
                        tostring(gid), math.floor(cx), math.floor(cy), g:count(),
                        tostring(res and res.decision),
                        dist and string.format("%.0f cells", dist) or "none"))
                end
            end
        end
    end
end

-- ===========================================================================
-- Squad Speed Sync capability: escort a protected unit (engineer / tactical
-- officer) to an objective while keeping the escorting convoy at the protected
-- unit's pace (FootClass::SetSpeedPercent). The AI "knows" it has this ability
-- (registered in _G.CapabilityRegistry) and actually uses it.
-- ===========================================================================

local ESCORT_SCAN_EVERY    = 60   -- frames between looking for an escort objective
local ESCORT_TARGET_RADIUS = 45   -- cells to search for a capturable objective
local ESCORT_GATHER_RADIUS = 14   -- cells to gather escorts around the protected unit
local ESCORT_RESYNC_EVERY  = 10   -- frames between re-clamping escort speeds
local ESCORT_ARRIVE_EPS    = 2    -- cells: distance at which the objective is "reached"
local ESCORT_MIN_PCT      = 0.05  -- lowest allowed speed fraction

-- Engineers auto-capture capturable buildings / repair bridges on contact.
local ENGINEER_TYPES = { E3 = true, ENGINEER = true }
-- Hero units are natural "tactical officers"; if none, the costliest ground unit
-- is promoted to that role (so the ability is not engineer-only).
local OFFICER_TYPES  = { BORIS = true, TANYA = true }

-- Capturable neutral buildings (engineer captures on arrival).
local CAPTURE_BUILDINGS = {
    CAOILD = true,  -- Oil Derrick
    CAAIRP = true,  -- Air Force Command HQ (Allied)
    NAAIRC = true,  -- Air Force Command HQ (Soviet)
    YAAIRC = true,  -- Air Force Command HQ (Yuri)
    CAHOSP = true,  -- Hospital
}
local CAPTURE_LABEL = {
    CAOILD = "oil derrick", CAAIRP = "airport", NAAIRC = "airport",
    YAAIRC = "airport", CAHOSP = "hospital",
}
-- Send engineers at hostile buildings too.
local CAPTURE_ENEMY = false
-- Bridge repair: set the waypoint ids that sit on the bridge(s) to repair. The
-- engineer is marched there and auto-repairs a damaged bridge on contact.
local BRIDGE_WAYPOINTS = {}
local NEUTRAL_HOUSES = { Neutral = true, Civilian = true, Special = true }

-- Smart AI registers that it is now aware of the squad-speed-sync ability.
_G.CapabilityRegistry = _G.CapabilityRegistry or {}
table.insert(_G.CapabilityRegistry, {
    id          = "squad_speed_sync",
    effect      = "escort_sync",
    kind        = "movement",
    description = "An AI convoy escorts a protected unit (engineer / tactical officer) at the slowest member's pace.",
})

local escort = nil             -- { protectedId, role, obj={x,y,label}, escorts={[id]=true} }
local escortSavedFactor = {}   -- id -> original SpeedMultiplier (restored on release)
local escortLastResync = 0
local escortNextScan   = 0

-- Cross-match memory (the "remaining 5%"). Pure Lua via `io` (enabled by
-- luaL_openlibs); file is anchored to the module dir so CnCNet CWD changes do
-- not affect it. Read once here; flushed to disk occasionally (NOT per frame).
local persist = require("framework.persist")
local memory  = persist.load()

local function dist2(x1, y1, x2, y2)
    local dx, dy = x2 - x1, y2 - y1
    return dx * dx + dy * dy
end

local function groundKind(u)
    local ok, k = pcall(u.GetKind, u)
    if ok and k then return k end
    return nil
end

local function ownerName(u)
    local ok, owner = pcall(u.GetOwner, u)
    if not ok or not owner then return nil end
    local okN, nm = pcall(owner.GetName, owner)
    if okN and nm then return nm end
    return nil
end

local function ownedByHouse(u, houseName)
    if not houseName then return false end
    return ownerName(u) == houseName
end

local function baseSpeedOf(u)
    local ok, s = pcall(u.GetBaseSpeed, u)
    if ok and s and s > 0 then return s end
    return nil
end

-- The AI house = a non-neutral house that is an enemy of the player.
local function findAiHouse()
    local player = House.GetPlayer()
    if not player then return nil end
    for i = 0, House.GetCount() - 1 do
        local h = House.GetByIndex(i)
        if h then
            local okN, nm = pcall(h.GetName, h)
            if okN and nm and not NEUTRAL_HOUSES[nm] and isEnemyOf(player, h) then
                return h
            end
        end
    end
    return nil
end

-- Nearest objective (capturable building and/or bridge waypoint) to (hx,hy).
-- allowEnemy=true lets the officer target hostile buildings as an advance point.
local function findObjective(house, hx, hy, allowEnemy)
    local best, bestD = nil, math.huge
    local okB, buildings = pcall(World.GetBuildings)
    if okB and buildings then
        for _, b in ipairs(buildings) do
            if b and b.IsAlive and b:IsAlive() then
                local okT, tn = pcall(b.GetTypeName, b)
                if okT and CAPTURE_BUILDINGS[tn] then
                    local okay, owner = pcall(b.GetOwner, b)
                    local oname = nil
                    if okay and owner then
                        local okO, onm = pcall(owner.GetName, owner)
                        if okO then oname = onm end
                    end
                    local isNeutral = oname and NEUTRAL_HOUSES[oname]
                    local isEnemy = false
                    if allowEnemy and okay and owner and house then
                        isEnemy = isEnemyOf(house, owner)
                    end
                    if isNeutral or isEnemy then
                        local okP, pos = pcall(b.GetPosition, b)
                        if okP and pos then
                            local d = dist2(hx, hy, pos.x, pos.y)
                            if d < bestD and d <= ESCORT_TARGET_RADIUS ^ 2 then
                                bestD = d
                                best = { x = math.floor(pos.x), y = math.floor(pos.y),
                                         label = CAPTURE_LABEL[tn] or "building" }
                            end
                        end
                    end
                end
            end
        end
    end

    if #BRIDGE_WAYPOINTS > 0 then
        for _, w in ipairs(BRIDGE_WAYPOINTS) do
            local okW, pos = pcall(World.GetWaypoint, w)
            if okW and pos and pos.x then
                local d = dist2(hx, hy, pos.x, pos.y)
                if d < bestD and d <= ESCORT_TARGET_RADIUS ^ 2 then
                    bestD = d
                    best = { x = pos.x, y = pos.y, label = "bridge" }
                end
            end
        end
    end
    return best
end

-- Choose the protected unit + its objective:
--   1) an engineer that has a capturable/bridge objective nearby
--   2) a hero "tactical officer" with an advance objective
--   3) the AI's costliest ground unit, promoted to officer
local function pickProtected(house, houseName)
    local okU, units = pcall(World.GetUnits)
    if not okU or not units then return nil end

    for _, u in ipairs(units) do
        if u and u:IsAlive() and ENGINEER_TYPES[u:GetTypeName()]
            and ownedByHouse(u, houseName) and groundKind(u) == "infantry" then
            local okP, pos = pcall(u.GetPosition, u)
            if okP and pos then
                local obj = findObjective(house, pos.x, pos.y, CAPTURE_ENEMY)
                if obj then return u, "engineer", obj end
            end
        end
    end

    for _, u in ipairs(units) do
        if u and u:IsAlive() and OFFICER_TYPES[u:GetTypeName()]
            and ownedByHouse(u, houseName)
            and (groundKind(u) == "infantry" or groundKind(u) == "unit") then
            local okP, pos = pcall(u.GetPosition, u)
            if okP and pos then
                local obj = findObjective(house, pos.x, pos.y, true)
                if obj then return u, "officer", obj end
            end
        end
    end

    local best, bestCost = nil, -1
    for _, u in ipairs(units) do
        if u and u:IsAlive() and ownedByHouse(u, houseName)
            and (groundKind(u) == "infantry" or groundKind(u) == "unit")
            and not ENGINEER_TYPES[u:GetTypeName()] then
            local okC, cost = pcall(u.GetCost, u)
            if okC and cost and cost > bestCost then
                bestCost, best = cost, u
            end
        end
    end
    if best then
        local okP, pos = pcall(best.GetPosition, best)
        if okP and pos then
            local obj = findObjective(house, pos.x, pos.y, true)
            if obj then return best, "officer", obj end
        end
    end
    return nil
end

-- Clamp one escort to the protected unit's pace (the actual speed-sync).
local function clampEscort(e, protectedSpeed)
    if not e or not e.IsAlive or not e:IsAlive() then return end
    local eid = e.GetId(e)
    if not eid then return end
    if not escortSavedFactor[eid] then
        local ok, f = pcall(e.GetSpeedFactor, e)
        escortSavedFactor[eid] = (ok and f) or 1.0
    end
    local ebs = baseSpeedOf(e)
    if not ebs or ebs <= 0 then return end
    local pct = escortSavedFactor[eid]
    if protectedSpeed and ebs > protectedSpeed then
        pct = protectedSpeed / ebs
        if pct < ESCORT_MIN_PCT then pct = ESCORT_MIN_PCT end
    end
    pcall(e.SetSpeedPercent, pct)
end

local function startEscort(houseName, protected, role, obj)
    local pid = protected.GetId(protected)
    if not pid then return end
    local okP, ppos = pcall(protected.GetPosition, protected)
    local pSpeed = baseSpeedOf(protected)

    pcall(protected.MoveTo, protected, obj.x, obj.y)

    -- One-commander rule: marching squad members are never pulled into escort.
    local squadIds = squadTrackedSet()

    local escorts = {}
    if okP and ppos then
        for _, u in ipairs(World.GetUnits() or {}) do
            local okId, uid = pcall(u.GetId, u)
            if okId and uid ~= pid and not squadIds[uid] and u:IsAlive() and ownedByHouse(u, houseName)
                and (groundKind(u) == "unit" or groundKind(u) == "infantry") then
                local okE, pos = pcall(u.GetPosition, u)
                if okE and pos and dist2(ppos.x, ppos.y, pos.x, pos.y) <= ESCORT_GATHER_RADIUS ^ 2 then
                    escorts[#escorts + 1] = u
                end
            end
        end
    end

    escort = { protectedId = pid, role = role, obj = obj, escorts = {} }
    for _, e in ipairs(escorts) do
        pcall(e.MoveTo, e, obj.x, obj.y)
        clampEscort(e, pSpeed)
        local eid = e.GetId(e)
        if eid then escort.escorts[eid] = true end
    end

    -- Cross-match memory: the agent "remembers" how many convoys it has run for
    -- this player across all matches. Incremented in cache; flushed on throttle.
    memory.convoys_total = (tonumber(memory.convoys_total) or 0) + 1
    memory.last_player   = houseName

    local tn = protected.GetTypeName(protected)
    if role == "engineer" then
        msg(string.format("[AI-ESCORT] Escorting engineer (#%d) to capture %s nearby or repair the bridge.",
            pid, obj.label or "building"))
    else
        msg(string.format("[AI-ESCORT] Escorting tactical officer %s (#%d) to %s.",
            tostring(tn or "?"), pid, obj.label or "enemy objective"))
    end
end

local function releaseEscort()
    if not escort then return end
    for id, _ in pairs(escort.escorts) do
        local orig = escortSavedFactor[id] or 1.0
        for _, u in ipairs(World.GetUnits() or {}) do
            local okId, uid = pcall(u.GetId, u)
            if okId and uid == id and u:IsAlive() then
                pcall(u.SetSpeedPercent, orig)
                break
            end
        end
    end
    escortSavedFactor = {}
    escort = nil
end

local function escortTick(frame)
    local house = findAiHouse()
    if not house then return end
    local okN, houseName = pcall(house.GetName, house)
    if not okN or not houseName then return end

    local okU, units = pcall(World.GetUnits)
    if not okU or not units then units = {} end

    if escort then
        local protectedAlive = false
        for _, u in ipairs(units) do
            local okId, uid = pcall(u.GetId, u)
            if okId and uid == escort.protectedId and u:IsAlive() and ownedByHouse(u, houseName) then
                protectedAlive = true
                if (frame - escortLastResync) >= ESCORT_RESYNC_EVERY then
                    escortLastResync = frame
                    local pSpeed = baseSpeedOf(u)
                    for _, e in ipairs(units) do
                        local okE, eid = pcall(e.GetId, e)
                        if okE and escort.escorts[eid] and e:IsAlive() then
                            clampEscort(e, pSpeed)
                        end
                    end
                end
                local okP, pos = pcall(u.GetPosition, u)
                if okP and pos and escort.obj
                    and dist2(pos.x, pos.y, escort.obj.x, escort.obj.y) <= ESCORT_ARRIVE_EPS ^ 2 then
                    releaseEscort()
                    msg("[AI-ESCORT] Objective reached: escort released.")
                end
                break
            end
        end
        if not protectedAlive then
            releaseEscort()
            msg("[AI-ESCORT] Escorted unit captured the objective or is gone: escort released.")
        end
        return
    end

    if frame < escortNextScan then return end
    escortNextScan = frame + ESCORT_SCAN_EVERY
    local protected, role, obj = pickProtected(house, houseName)
    if protected then
        -- One-commander rule: a squad member is never "promoted" to officer.
        local okPid, protId = pcall(protected.GetId, protected)
        if okPid and protId and squadTrackedSet()[protId] then protected = nil end
    end
    if protected and obj then
        startEscort(houseName, protected, role, obj)
    end
end

function SmartAI.Update(frame)
    local ok, err = pcall(updateInner, frame)
    if not ok then
        msg(string.format("[AI] update error: %s", tostring(err)))
    end

    local okEsc, escErr = pcall(escortTick, frame)
    if not okEsc then
        msg(string.format("[AI-ESCORT] error: %s", tostring(escErr)))
    end

    local okSq, sqErr = pcall(squadTick, frame)
    if not okSq then
        msg(string.format("[AI-SQUAD] error: %s", tostring(sqErr)))
    end

    -- Persist cross-match memory rarely (roughly every 5 s at 60 fps), so a
    -- file write never lands in a hot game frame. Written outside the sim path.
    if frame % 300 == 0 then
        pcall(persist.flush)
    end
end

return SmartAI
