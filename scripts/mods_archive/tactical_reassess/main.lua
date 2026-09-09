-- Squad Tactics — multi-squad tactical AI for the enemy AI house.
--
-- Drives NUM_SQUADS independent attack squads (FORCE_SIZE units each) through
-- the framework ForceGroup manager. Every squad runs its own loop:
--     Observe -> Evaluate -> Decide -> Act -> Reassess
-- so squads in different places can reach different decisions on the same
-- frame (one retreats while another keeps attacking).
--
-- Your units are never touched: members are recruited only from the enemy AI
-- house (non-human, non-neutral, at war with you). No AI house -> idle.
--
-- Vanilla+: only native Attack / MoveTo / Stop orders. No pathfinding,
-- production or economy replacement.
--
-- Logging (LuaAPI.log + in-game messages, change-only per squad):
--     [TACTICAL] [squad_1] RETREAT (tier=... reason=...)
--
-- How to run: enable `tactical_reassess`, start a skirmish against an AI
-- opponent. Watch the [TACTICAL] lines.

local ForceGroup = require("framework.force_group")
local util = require("framework.util")

local Mod = {}

local NUM_SQUADS    = 3    -- independent squads attacking in parallel
local FORCE_SIZE    = 3    -- combat units per squad
local TICK_EVERY    = 5    -- frames between squad updates (orders re-issued)
local GATHER_EVERY  = 30   -- frames between recruiting fresh AI units
local SCAN_RADIUS   = 18   -- local battlefield radius (cells) for evaluation
local RANGE_RADIUS  = 14   -- how far an enemy must be to be attackable
local RETREAT_DIST  = 12   -- retreat vector length (cells)
local RETREAT_HOLD  = 90   -- frames a retreat holds before release (~1.5 s)

-- Never join a squad: harvesters, MCVs, engineers. (An ENGINEER was once
-- recruited into a combat squad and marched to war instead of capturing.)
local NON_COMBAT = {
    HARV = true, CMIN = true, SMIN = true,   -- harvesters
    AMCV = true, SMV = true,                 -- MCVs
    E3 = true, ENGINEER = true,              -- engineers (capture duty)
}

-- Drive the AI house, never the player's: the first non-human, non-neutral
-- house at war with the player. Cached by index so squads don't hop between
-- houses mid-match. Returns nil when no such house exists.
local NEUTRAL_HOUSES = { Neutral = true, Civilian = true, Special = true }
local aiHouseIndex = nil

local function isAiHouse(h, player)
    if not h then return false end
    local okH, human = pcall(h.IsHuman, h)
    if not okH or human then return false end
    local okN, name = pcall(h.GetName, h)
    if not okN or not name or NEUTRAL_HOUSES[name] then return false end
    local okA, allied = pcall(h.IsAlliedWith, h, player)
    if okA and allied then return false end
    return true
end

local function aiHouse()
    local okP, player = pcall(House.GetPlayer)
    if not okP or not player then return nil end
    if aiHouseIndex ~= nil then
        local okC, h = pcall(House.GetByIndex, aiHouseIndex)
        if okC and h and isAiHouse(h, player) then return h end
        aiHouseIndex = nil
    end
    local okN, count = pcall(House.GetCount)
    if not okN or not count then return nil end
    for i = 0, count - 1 do
        local okH, h = pcall(House.GetByIndex, i)
        if okH and h and isAiHouse(h, player) then
            aiHouseIndex = i
            return h
        end
    end
    return nil
end

local function msg(text)
    if Engine and Engine.PrintMessage then
        Engine.PrintMessage(text)
    end
end

local function log(text)
    print("[TACTICAL] " .. text)
end

local mgr = nil
local lastSeenHouseIdx = nil
local warnedNoHouse = false
local lastLogged = {}   -- group id -> last logged "decision" key (change-only)
local lastStats = nil   -- "available/in squads" key (change-only)

local TRACK_EVERY = 1800  -- ~30 s: squad position track (proves movement)

local function ensureManager()
    if mgr then return end
    mgr = ForceGroup.new({})
    for i = 1, NUM_SQUADS do
        local gid = "squad_" .. i
        local g = mgr:add_group({
            id = gid,
            getHouse = aiHouse,
            radius = SCAN_RADIUS,
            pulseEvery = 15,
            rangeRadius = RANGE_RADIUS,
            retreatDist = RETREAT_DIST,
            retreatHold = RETREAT_HOLD,
        })
        g:set_handler("onDecision", function(grp, res)
            local dec = tostring(res.decision)
            if lastLogged[grp.id] ~= dec then
                lastLogged[grp.id] = dec
                log(string.format("[%s] %s (tier=%s reason=%s)", tostring(grp.id),
                    string.upper(dec), tostring(res.tier), tostring(res.reason)))
                msg(string.format("[TACTICAL] [%s] %s", tostring(grp.id), string.upper(dec)))
            end
        end)
    end
    log(string.format("initialized: %d squads x %d units", NUM_SQUADS, FORCE_SIZE))
end

-- Sorted group ids (deterministic fill order, no pairs() randomness).
local function sortedGroupIds()
    local out = mgr:group_ids()
    table.sort(out, function(a, b) return tostring(a) < tostring(b) end)
    return out
end

-- All unit ids already tracked by any squad.
local function trackedSet()
    local set = {}
    if not mgr then return set end
    for _, gid in ipairs(sortedGroupIds()) do
        local g = mgr:group(gid)
        if g then
            for _, id in ipairs(g:ids()) do set[id] = true end
        end
    end
    return set
end

local function isRecruit(u, house, tracked)
    if not u then return false end
    local okA, alive = pcall(u.IsAlive, u)
    if not okA or not alive then return false end
    local okO, owner = pcall(u.GetOwner, u)
    if not okO or owner == nil or owner ~= house then return false end
    local okK, kind = pcall(u.GetKind, u)
    if not okK or (kind ~= "unit" and kind ~= "infantry" and kind ~= "aircraft") then
        return false
    end
    local okT, tn = pcall(u.GetTypeName, u)
    if okT and tn and NON_COMBAT[tn] then return false end
    local okI, id = pcall(u.GetId, u)
    if not okI or not id or tracked[id] then return false end
    return true
end

-- How many AI-house combat units exist (recruitable pool, not only squads).
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
                    if not (okT and tn and NON_COMBAT[tn]) then n = n + 1 end
                end
            end
        end
    end
    return n
end

local function trackedTotal()
    local m = 0
    if not mgr then return 0 end
    for _, gid in ipairs(sortedGroupIds()) do
        local g = mgr:group(gid)
        if g then m = m + g:count() end
    end
    return m
end

-- Fill squads up to FORCE_SIZE each with fresh AI-house combat units.
local function gather(house)
    local tracked = trackedSet()
    local okU, units = pcall(World.GetUnits)
    if not okU or not units then return end
    for _, u in ipairs(units) do
        if isRecruit(u, house, tracked) then
            for _, gid in ipairs(sortedGroupIds()) do
                local g = mgr:group(gid)
                if g and g:count() < FORCE_SIZE then
                    local okT, id = pcall(g.add_member, g, u)
                    if okT and id then
                        tracked[id] = true
                        local okN, tn = pcall(u.GetTypeName, u)
                        log(string.format("[%s] recruit: %s #%d", tostring(gid),
                            tostring(okN and tn or "?"), id))
                    end
                    break
                end
            end
        end
    end
end

-- Nearest enemy position to (cx, cy) plus distance in cells. Nil when none.
local function nearestEnemyPos(house, cx, cy)
    local listFn = World.GetAllUnits or World.GetUnits
    if not listFn then return nil end
    local okU, units = pcall(listFn)
    if not okU or not units then return nil end
    local bx, by, bestD = nil, nil, math.huge
    for _, u in ipairs(units) do
        -- util.is_enemy: same house / allies / neutrals / civilians are never
        -- enemies. Without this SEEK marches squads onto neutral derricks and
        -- vanilla guard-fire destroys what engineers were meant to capture.
        if util.is_enemy(house, u) then
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

local function memberById(id)
    local ok, units = pcall(World.GetUnits)
    if not ok or not units then return nil end
    for _, u in ipairs(units) do
        local okId, uid = pcall(u.GetId, u)
        if okId and uid == id then return u end
    end
    return nil
end

-- SEEK: a squad whose evaluator finds no target advances toward the nearest
-- enemy instead of idling at the AI base forever. Retreat/disengage are
-- respected (no seek). Orders are silent — the FIND_TARGET decision log
-- already shows the state.
local function seek(house)
    for _, gid in ipairs(sortedGroupIds()) do
        local g = mgr:group(gid)
        if g then
            local res = g:decision()
            if res and res.decision == "find_target" then
                local cx, cy = g:centroid()
                if cx then
                    local ex, ey, dist = nearestEnemyPos(house, cx, cy)
                    if ex and dist and dist > RANGE_RADIUS then
                        for _, id in ipairs(g:ids()) do
                            local u = memberById(id)
                            if u then pcall(u.MoveTo, u, math.floor(ex), math.floor(ey)) end
                        end
                    end
                end
            end
        end
    end
end

function Mod.Update(frame)
    ensureManager()

    local house = aiHouse()
    if not house then
        if not warnedNoHouse then
            warnedNoHouse = true
            log("no AI house found; idle (your units untouched)")
        end
        return
    end
    warnedNoHouse = false

    -- Opponent changed (new match / new house): drop old squads, recruit fresh.
    if lastSeenHouseIdx ~= aiHouseIndex then
        lastSeenHouseIdx = aiHouseIndex
        mgr:reset()
        lastLogged = {}
        lastStats = nil
        local okNm, hname = pcall(house.GetName, house)
        log(string.format("opponent: %s; squads reset", tostring(okNm and hname or "?")))
    end

    if frame % GATHER_EVERY == 0 then
        gather(house)
        local avail, insquads = countAiCombat(house), trackedTotal()
        local key = avail .. "/" .. insquads
        if lastStats ~= key then
            lastStats = key
            log(string.format("AI combat units: %d available, %d in squads", avail, insquads))
        end
    end

    if frame % TICK_EVERY == 0 then
        mgr:update(frame)
        seek(house)
    end

    -- Movement proof: centroid + nearest-enemy distance per squad. Two such
    -- lines decide "walking but far" vs "standing still (vanilla holds them)".
    if frame % TRACK_EVERY == 0 then
        for _, gid in ipairs(sortedGroupIds()) do
            local g = mgr:group(gid)
            if g then
                local cx, cy = g:centroid()
                if cx then
                    local res = g:decision()
                    local ex, ey, dist = nearestEnemyPos(house, cx, cy)
                    log(string.format("[%s] pos=%d,%d members=%d decision=%s enemy=%s",
                        tostring(gid), math.floor(cx), math.floor(cy), g:count(),
                        tostring(res and res.decision),
                        dist and string.format("%.0f cells", dist) or "none"))
                end
            end
        end
    end
end

return Mod
