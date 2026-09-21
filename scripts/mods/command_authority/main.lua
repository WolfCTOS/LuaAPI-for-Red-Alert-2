-- Command Authority - a Command Points duel on top of the RTS (iteration 3)
--
-- WHY THIS MOD EXISTS
--   Major Ares/Phobos global mods (Mental Omega, Rise of the East) are
--   design-time configuration: unit rosters, per-type INI flags, static map
--   triggers. What they do not naturally model is PERSISTENT, MATCH-AWARE
--   meta-systems with player decisions in the loop (cf.
--   PROJECT/RUNTIME_BOUNDARY.md — "not naturally modeled", not a superiority
--   claim). This mod is the standing proof of that boundary.
--
-- WHAT CHANGED IN ITERATION 3 (playtest feedback: "boring", "random EMPs
-- are annoying")
--   * RETALIATION DOCTRINE: the AI Director NEVER disables your units on its
--     own initiative. It answers your Sabotage only - with a loud 5 s warning
--     first. Never press V -> never get EMP'd. The most annoying element is
--     now a known, counterable rule.
--   * DIRECTIVES: alternating timed objectives for every human house -
--       HUNT:   destroy the enemy's priciest ground unit in 75 s  (+8 CP)
--       DEFEND: protect your priciest ground unit for 75 s          (+8 CP)
--     They give the player something to DO beyond macro. Deterministic
--     target pick (max HP, first in scan order on ties) - MP-safe.
--   * CP FEED: every point earned or spent is announced ("CP +5 [kill] = 8")
--     so the meter is felt, not imagined.
--   * REINFORCEMENTS LAND AT THE FRONT: at the position of your last kill
--     (where the fighting is), not at a static centroid.
--
-- CORE LOOP (unchanged economics)
--   Earn CP: +5 per kill (nearest-hostile attribution), +1 per 400 damage
--   dealt (fractional bank), +1 per 6 s survival streak, +8 per directive.
--   Spend (skirmish/vs-AI): Z Reinforce 10 | X Repair 6 | C Blitz 4 |
--   V Sabotage 8. The AI Director earns CP by the SAME rules and spends it
--   on repair/reinforce - and on retaliation, only.
--   T: status ping (works in every mode; local read-out only).
--
-- MULTIPLAYER GATE
--   With 2+ human houses the simulation-writing powers LOCK themselves and
--   only deterministic systems run (economy, attribution, directives,
--   status). Directives write CP only - inert while powers are locked.
--
-- ARES/PHOBOS BOUNDARY
--   Powers reuse raw engine verbs (spawn/heal/disable). The per-house CP
--   economy, directive system, retaliation doctrine and the Director are a
--   persistent meta-layer no INI-only feature provides.

local AUTH = {}

-- ---------------------------------------------------------------------------
-- Tuning (exposed for tests and future balancing; read at use time)
-- ---------------------------------------------------------------------------

local TUNING = {
    CP_START         = 3,
    CP_KILL          = 5,
    CP_DAMAGE_PER    = 400,   -- 1 CP per this much damage dealt
    CP_SURVIVE_EVERY = 360,   -- survival income every 6 s ...
    CP_SURVIVE_PAY   = 1,     -- ... if no losses since the previous tick

    DIRECTIVE_PAY    = 8,
    DIRECTIVE_FRAMES = 75 * 60,
    DIRECTIVE_FIRST  = 50 * 60,
    DIRECTIVE_GAP    = 15 * 60,

    RETALIATE_WARN   = 5 * 60,

    POWER_COST = { reinforce = 10, repair = 6, blitz = 4, sabotage = 8 },
}
AUTH.TUNING = TUNING

local SCAN_FRAMES       = 15     -- combat scan 4x per second
local POWER_KEYS        = { 0x5A, 0x58, 0x43, 0x56 } -- Z X C V
local BLITZ_FRAMES      = 8 * 60
local BLITZ_DISCOUNT    = 0.5
local SABOTAGE_FRAMES   = 6 * 60
local REINFORCE_TYPE    = "HTNK"
local REINFORCE_COUNT   = 2
local AI_THINK_EVERY    = 4 * 60

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------

local S = {
    lastFrame = 0,
    nextSurvive = 360,
    cp = {},             -- houseName -> points (integer)
    dmgBank = {},        -- houseName -> fractional damage bank
    nextThink = {},      -- houseName -> next director think frame
    seen = {},           -- unitId -> snapshot
    losses = {},         -- houseName -> lost units since last survive tick
    blitz = {},          -- houseName -> untilFrame
    announced = false,
    mpMode = false,      -- true = 2+ human houses: powers locked
    playerHouseName = nil,

    -- iteration 3
    lastKillPos = {},    -- houseName -> {x,y} of the last kill it was credited
    directive = {},      -- houseName -> active directive
    directiveIndex = {}, -- houseName -> count so far (odd = hunt, even = defend)
    nextDirective = {},  -- houseName -> start frame of the next directive
    retaliation = nil,   -- { from = aiHouseName, due = frame } or nil
}

local function say(msg) Engine.PrintMessage("[AUTH] " .. msg) end

-- Prefix for power messages: the player sees plain lines, the Director's
-- moves are explicitly labeled.
local function who(name)
    return name == S.playerHouseName and "" or "DIRECTOR: "
end

local function houseNameOf(h)
    if not h then return nil end
    local ok, name = pcall(h.GetName, h)
    if ok and type(name) == "string" and name ~= "" then return name end
    return nil
end

local function cpOf(name) return S.cp[name] or 0 end
local function addCp(name, amount)
    if name then S.cp[name] = cpOf(name) + amount end
end

-- Every earned/spent point of the LOCAL player is announced, so the meter
-- is felt. Other houses' points stay silent (read them via T).
local function earnCp(name, amount, tag)
    if not name then return end
    addCp(name, amount)
    if amount ~= 0 and name == S.playerHouseName then
        say(string.format("CP %+d [%s] = %d", amount, tag or "?", cpOf(name)))
    end
end

local function playerHouse() return House.GetPlayer() end

local function aiNames()
    local out = {}
    for name in pairs(S.cp) do
        if name ~= S.playerHouseName then out[#out + 1] = name end
    end
    table.sort(out)
    return out
end

-- name -> House object cache (houses are static for the match lifetime).
local houseObj = {}

local function refreshHouseObjects()
    houseObj = {}
    local n = House.GetCount()
    for i = 0, n - 1 do
        local h = House.GetByIndex(i)
        local name = houseNameOf(h)
        if name then houseObj[name] = h end
    end
end

local function isHumanHouse(name)
    local h = houseObj[name]
    if not h then return false end
    local ok, res = pcall(h.IsHuman, h)
    return ok and res == true
end

local function areAllied(nameA, nameB)
    if nameA == nameB then return true end
    local ha, hb = houseObj[nameA], houseObj[nameB]
    if not ha or not hb then return false end
    local ok, res = pcall(ha.IsAlliedWith, ha, hb)
    return ok and res == true
end

-- Combat-house gate (RCA fix): Neutral/Special are civilian houses
-- (engine `MultiplayPassive`). They must never earn CA command points,
-- never be picked as kill earners, and never act as the AI Director.
-- Alliance checks above intentionally still see them (a parked civilian
-- car is correctly "not allied", just not a war participant).
local NON_COMBATANT = {
    Neutral = true,
    Special = true,
}

local function isCombatHouseName(name)
    return name ~= nil and not NON_COMBATANT[name]
end

-- Object form of the gate. Unresolvable houses fail closed (non-combatant).
local function isCombatHouse(house)
    if not house then return false end
    return isCombatHouseName(houseNameOf(house))
end

-- Nearest hostile unit's house to a point (cell coords). Deterministic
-- tie-break: first in scan order. Aircraft count (they shoot back).
local function nearestHostileUnit(victimHouse, x, y)
    local units = World.GetUnits()
    if not units then return nil end
    local best, bestD2 = nil, math.huge
    for _, u in ipairs(units) do
        if u and u:IsAlive() then
            local owner = houseNameOf(u:GetOwner())
            if owner and isCombatHouseName(owner)
                and owner ~= victimHouse and not areAllied(owner, victimHouse) then
                local p = u:GetPosition()
                if p and p.x and p.y then
                    local dx, dy = p.x - x, p.y - y
                    local d2 = dx * dx + dy * dy
                    if d2 < bestD2 then
                        best, bestD2 = owner, d2
                    end
                end
            end
        end
    end
    return best
end

local function creditDamage(houseName, dmg)
    if not houseName or dmg <= 0 then return end
    S.dmgBank[houseName] = (S.dmgBank[houseName] or 0) + dmg / TUNING.CP_DAMAGE_PER
    local award = math.floor(S.dmgBank[houseName])
    if award >= 1 then
        S.dmgBank[houseName] = S.dmgBank[houseName] - award
        earnCp(houseName, award, "dmg")
    end
end

-- ---------------------------------------------------------------------------
-- Directives: alternating timed objectives for every human house
-- ---------------------------------------------------------------------------

local function directiveStart(name)
    local idx = (S.directiveIndex[name] or 0) + 1
    local kind = (idx % 2 == 1) and "hunt" or "defend"

    local best, bestMax, bestType, bestId = nil, 0, "?", nil
    local all = World.GetUnits()
    if not all then return false end
    for _, u in ipairs(all) do
        if u and u:IsAlive() and u:GetKind() ~= "aircraft" then
            local owner = houseNameOf(u:GetOwner())
            local eligible
            if kind == "hunt" then
                eligible = owner and owner ~= name and not areAllied(owner, name)
            else
                eligible = owner == name
            end
            if eligible then
                local m = u:GetMaxHealth() or 0
                if m > bestMax then
                    best, bestMax = u, m
                    bestType = u:GetTypeName()
                    bestId = u:GetId()
                end
            end
        end
    end

    if not best then
        -- Nothing eligible right now: retry in 10 s.
        S.nextDirective[name] = S.lastFrame + 600
        return false
    end

    S.directiveIndex[name] = idx
    S.directive[name] = {
        kind = kind,
        targetId = bestId,
        ttype = bestType,
        endsAt = S.lastFrame + TUNING.DIRECTIVE_FRAMES,
    }
    if kind == "hunt" then
        say(string.format("DIRECTIVE: HUNT - destroy the enemy %s within %d s for +%d CP.",
            bestType, TUNING.DIRECTIVE_FRAMES / 60, TUNING.DIRECTIVE_PAY))
    else
        say(string.format("DIRECTIVE: DEFEND - protect your %s for %d s for +%d CP (its killer profits instead).",
            bestType, TUNING.DIRECTIVE_FRAMES / 60, TUNING.DIRECTIVE_PAY))
    end
    return true
end

-- Resolution when the directive target dies. Called from the kill scan.
local function directiveOnDeath(victimId, killerName, victimType)
    for name, dir in pairs(S.directive) do
        if dir.targetId == victimId then
            S.directive[name] = nil
            S.nextDirective[name] = S.lastFrame + TUNING.DIRECTIVE_GAP
            if dir.kind == "hunt" then
                -- Whoever actually killed it banks the bounty (FFA-safe).
                earnCp(killerName or name, TUNING.DIRECTIVE_PAY, "directive")
                say(string.format("DIRECTIVE fulfilled: enemy %s destroyed (+%d CP).",
                    victimType or "?", TUNING.DIRECTIVE_PAY))
            else
                if killerName then
                    earnCp(killerName, TUNING.DIRECTIVE_PAY, "directive")
                    say(string.format("DIRECTIVE lost: your %s was destroyed - the killer banks +%d CP.",
                        victimType or "?", TUNING.DIRECTIVE_PAY))
                else
                    say(string.format("DIRECTIVE lost: your %s was destroyed - no bounty.",
                        victimType or "?"))
                end
            end
        end
    end
end

-- Resolution when the timer runs out.
local function directiveTick()
    for name, dir in pairs(S.directive) do
        if S.lastFrame >= dir.endsAt then
            S.directive[name] = nil
            S.nextDirective[name] = S.lastFrame + TUNING.DIRECTIVE_GAP
            if dir.kind == "defend" then
                earnCp(name, TUNING.DIRECTIVE_PAY, "directive")
                say(string.format("DIRECTIVE fulfilled: your %s survived (+%d CP).",
                    dir.ttype, TUNING.DIRECTIVE_PAY))
            else
                say(string.format("DIRECTIVE failed: the %s escaped.", dir.ttype))
            end
        end
    end
end

local function directiveLoop()
    for name in pairs(S.cp) do
        if isHumanHouse(name) and not S.directive[name]
            and S.lastFrame >= (S.nextDirective[name] or TUNING.DIRECTIVE_FIRST) then
            directiveStart(name)
        end
    end
end

-- ---------------------------------------------------------------------------
-- Combat scan: damage attribution, kill attribution, survival income,
-- directive bookkeeping
-- ---------------------------------------------------------------------------

local function combatScan()
    local units = World.GetUnits()
    if not units then return end

    local seenNow = {}

    for _, u in ipairs(units) do
        local id = u and u:IsAlive() and u:GetId() or nil
        if id then
            seenNow[id] = true
            local hp = u:GetHealth()
            local prev = S.seen[id]
            local ownerName = houseNameOf(u:GetOwner())
            local pos = u:GetPosition()

            -- Damage attribution: HP drop since the last scan.
            if prev and hp and prev.hp and hp < prev.hp and pos and pos.x then
                local creditTo = nearestHostileUnit(ownerName, pos.x, pos.y)
                creditDamage(creditTo, prev.hp - hp)
            end

            S.seen[id] = {
                ownerName = ownerName,
                name = u:GetTypeName(),
                hp = hp,
                pos = (pos and pos.x and pos.y) and { x = pos.x, y = pos.y } or nil,
            }
        end
    end

    -- Deaths since the last scan: kill credit to the nearest hostile house
    -- at the victim's last known position; fallback: split among non-allied
    -- houses (keeps 1v1 behavior when no hostile unit is on the map).
    for id, prev in pairs(S.seen) do
        if not seenNow[id] then
            S.losses[prev.ownerName] = (S.losses[prev.ownerName] or 0) + 1

            local killer = nil
            if prev.pos then
                killer = nearestHostileUnit(prev.ownerName, prev.pos.x, prev.pos.y)
            end
            if killer then
                earnCp(killer, TUNING.CP_KILL, "kill")
                -- Reinforcements land where the fighting is.
                S.lastKillPos[killer] = prev.pos
            else
                for name in pairs(S.cp) do
                    if isCombatHouseName(name)
                        and name ~= prev.ownerName
                        and not areAllied(name, prev.ownerName) then
                        earnCp(name, TUNING.CP_KILL, "kill")
                    end
                end
            end

            directiveOnDeath(id, killer, prev.name)
            S.seen[id] = nil
        end
    end

    -- Survival income: on the tick boundary, houses with zero losses since
    -- the last tick get paid; the tick also clears loss counters.
    if S.lastFrame >= S.nextSurvive then
        S.nextSurvive = S.lastFrame + TUNING.CP_SURVIVE_EVERY
        for name in pairs(S.cp) do
            if (S.losses[name] or 0) == 0 and TUNING.CP_SURVIVE_PAY > 0 then
                earnCp(name, TUNING.CP_SURVIVE_PAY, "streak")
            end
            S.losses[name] = 0
        end
    end

    directiveTick()
    directiveLoop()
end

-- ---------------------------------------------------------------------------
-- Powers
-- ---------------------------------------------------------------------------

local function ownUnits(houseName, filter)
    local units = World.GetUnits()
    local out = {}
    if not units then return out end
    for _, u in ipairs(units) do
        if u and u:IsAlive() then
            local okOwner = houseNameOf(u:GetOwner()) == houseName
            local okKind = not filter or filter(u)
            if okOwner and okKind then
                out[#out + 1] = u
            end
        end
    end
    return out
end

-- ---------------------------------------------------------------------------
-- Frontline spawn validation (placement fix): lastKillPos stays the primary
-- frontline source, but the exact victim cell is never used blindly. A
-- last-kill site inside enemy economy (harvesters/refineries) or inside an
-- enemy force concentration would stack fresh reinforcements onto harvesters
-- or into focus fire. A normal contested frontline (a couple of enemies at
-- weapon distance, no eco contact) still spawns exactly at lastKillPos.
-- Deterministic: ID-ordered traversal, pure arithmetic, no RNG, no
-- wall-clock — the same live roster always yields the same point. Runs at
-- most twice per reinforce (rare event), so the extra scans are negligible.
-- ---------------------------------------------------------------------------

local FRONTLINE_SCAN_R = 7       -- look-around window around the candidate
local FRONTLINE_DANGER_R = 5     -- eco/army contact inside this forces a move
local FRONTLINE_OVERLAP_R = 2    -- any enemy building basically on the cell
local FRONTLINE_ARMY_COUNT = 3   -- enemy combat mobiles inside DANGER_R
local FRONTLINE_SHIFT = 8        -- displacement toward own forces (cells)

-- Economic assets recognizable with the existing API (observed live: SMIN,
-- YAREFN; HARV/CMIN/NAREFN/GAREFN are the standard YR harvester/refinery
-- IDs of the other factions).
local FRONTLINE_ECO_TYPES = {
    SMIN = true, HARV = true, CMIN = true,
    YAREFN = true, NAREFN = true, GAREFN = true,
}

-- Enemy combat-house objects within SCAN_R of (x, y), ID-ordered.
-- Civilians (non-combat houses) and allies never contest a spawn point.
local function frontlineFoes(spawnerName, x, y)
    local foes = {}
    local function consider(u)
        if not (u and u:IsAlive()) then return end
        local okO, owner = pcall(u.GetOwner, u)
        local oname = (okO and owner) and houseNameOf(owner) or nil
        if not oname or oname == spawnerName then return end
        if not isCombatHouseName(oname) then return end
        if areAllied(spawnerName, oname) then return end
        local okP, p = pcall(u.GetPosition, u)
        if not (okP and p and p.x and p.y) then return end
        local dx, dy = p.x - x, p.y - y
        if dx * dx + dy * dy > FRONTLINE_SCAN_R * FRONTLINE_SCAN_R then return end
        local okT, t = pcall(u.GetTypeName, u)
        local okK, k = pcall(u.GetKind, u)
        local okI, id = pcall(u.GetId, u)
        foes[#foes + 1] = {
            id = (okI and id) or 0,
            x = p.x, y = p.y,
            type = (okT and t) and tostring(t) or "?",
            kind = (okK and k) and tostring(k) or "?",
        }
    end
    local okU, units = pcall(World.GetUnits)
    if okU and units then
        for _, u in ipairs(units) do consider(u) end
    end
    if World.GetBuildings then
        local okB, blds = pcall(World.GetBuildings)
        if okB and blds then
            for _, b in ipairs(blds) do consider(b) end
        end
    end
    table.sort(foes, function(a, b) return a.id < b.id end)
    return foes
end

-- Danger assessment over an ID-sorted foe list. Order-independent
-- aggregates. Returns danger(bool), enemyCentroidX, enemyCentroidY.
local function frontlineDangerAt(foes, x, y)
    local anx, any, an = 0, 0, 0
    local army = 0
    local danger = false
    local DR2 = FRONTLINE_DANGER_R * FRONTLINE_DANGER_R
    local OR2 = FRONTLINE_OVERLAP_R * FRONTLINE_OVERLAP_R
    for _, f in ipairs(foes) do
        anx, any, an = anx + f.x, any + f.y, an + 1
        local dx, dy = f.x - x, f.y - y
        local d2 = dx * dx + dy * dy
        if d2 <= DR2 then
            if FRONTLINE_ECO_TYPES[f.type] then
                danger = true
            elseif f.kind == "building" then
                if d2 <= OR2 then danger = true end
            else
                army = army + 1
            end
        end
    end
    if army >= FRONTLINE_ARMY_COUNT then danger = true end
    if an == 0 then return false, x, y end
    return danger, anx / an, any / an
end

-- ID-ordered own-force centroid (order-proof even if roster order varies).
local function frontlineOwnCentroid(mine)
    local tagged = {}
    for _, u in ipairs(mine) do
        local ok, id = pcall(u.GetId, u)
        tagged[#tagged + 1] = { u = u, id = (ok and id) or 0 }
    end
    table.sort(tagged, function(a, b) return a.id < b.id end)
    local cx, cy, n = 0, 0, 0
    for _, e in ipairs(tagged) do
        local ok, p = pcall(e.u.GetPosition, e.u)
        if ok and p and p.x and p.y then cx, cy, n = cx + p.x, cy + p.y, n + 1 end
    end
    if n == 0 then return nil end
    return cx / n, cy / n
end

-- Validate a lastKillPos candidate. Returns adjusted (x, y), or nil when no
-- safe frontline point exists (caller falls back to the own-force centroid).
local function adjustSpawnClearOfEco(spawnerName, sx, sy, mine)
    local danger, ex, ey =
        frontlineDangerAt(frontlineFoes(spawnerName, sx, sy), sx, sy)
    if not danger then return sx, sy end
    local mox, moy = frontlineOwnCentroid(mine)
    if not mox then return nil end
    local dx, dy = mox - ex, moy - ey
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 0.001 then return nil end
    local ax = math.floor(sx + dx / len * FRONTLINE_SHIFT + 0.5)
    local ay = math.floor(sy + dy / len * FRONTLINE_SHIFT + 0.5)
    local danger2 =
        frontlineDangerAt(frontlineFoes(spawnerName, ax, ay), ax, ay)
    if danger2 then return nil end
    return ax, ay
end

local function powerReinforce(houseName)
    local mine = ownUnits(houseName)
    if #mine == 0 then return false end
    local house = houseObj[houseName]
    if not house then return false end

    -- Own-force centroid (muster point; always safe by construction).
    local cx, cy, n = 0, 0, 0
    for _, u in ipairs(mine) do
        local p = u:GetPosition()
        if p then cx, cy, n = cx + p.x, cy + p.y, n + 1 end
    end
    if n == 0 then return false end
    cx, cy = math.floor(cx / n + 0.5), math.floor(cy / n + 0.5)

    -- Frontline candidate: last kill site (unchanged design, still primary).
    local sx, sy, tag = cx, cy, "with your forces"
    local lp = S.lastKillPos[houseName]
    if lp then
        sx, sy, tag = lp.x, lp.y, "at the front"
        -- Tactical validation: never stack on enemy eco/army; on failure
        -- fall back to the muster point instead of the victim cell.
        local ax, ay = adjustSpawnClearOfEco(houseName, sx, sy, mine)
        if ax then
            if ax ~= sx or ay ~= sy then tag = "at the front, adjusted" end
            sx, sy = ax, ay
        else
            sx, sy, tag = cx, cy, "with your forces"
        end
    end

    local spawned = house:SpawnUnit(REINFORCE_TYPE, REINFORCE_COUNT, sx, sy, 0, true, "")
    if spawned and spawned > 0 then
        say(string.format("%sREINFORCEMENTS: %d x %s landed %s.",
            who(houseName), spawned, REINFORCE_TYPE, tag))
        return true
    end
    return false
end

local function powerRepair(houseName)
    local wounded = {}
    for _, u in ipairs(ownUnits(houseName, function(u) return u:GetKind() ~= "aircraft" end)) do
        local hp, maxhp = u:GetHealth(), u:GetMaxHealth()
        if hp and maxhp and hp < maxhp then
            wounded[#wounded + 1] = { u = u, frac = hp / maxhp }
        end
    end
    if #wounded == 0 then return false end
    table.sort(wounded, function(a, b) return a.frac < b.frac end)

    local healed = 0
    for i = 1, math.min(5, #wounded) do
        wounded[i].u:SetHealthRatio(1.0)
        healed = healed + 1
    end
    say(string.format("%sFIELD REPAIR: %d unit(s) fully restored.", who(houseName), healed))
    return true
end

local function powerBlitz(houseName)
    S.blitz[houseName] = S.lastFrame + BLITZ_FRAMES
    say(string.format("%sBLITZ ORDER: powers cost half for the next %d seconds!",
        who(houseName), BLITZ_FRAMES / 60))
    return true
end

local function powerSabotage(houseName)
    local all = World.GetUnits()
    if not all then return false end
    local best, bestMax, bestOwner = nil, -1, nil
    for _, u in ipairs(all) do
        if u and u:IsAlive() then
            local owner = houseNameOf(u:GetOwner())
            if owner and owner ~= houseName
                and not areAllied(owner, houseName)
                and u:GetKind() ~= "aircraft" then
                local m = u:GetMaxHealth()
                if m and m > bestMax then
                    best, bestMax, bestOwner = u, m, owner
                end
            end
        end
    end
    if not best then return false end
    best:Disable(SABOTAGE_FRAMES)
    say(string.format("SABOTAGE by %s: %s (%s) disabled for %d s!",
        houseName, best:GetTypeName(), bestOwner, SABOTAGE_FRAMES / 60))

    -- RETALIATION DOCTRINE: only the player's Sabotage schedules a Director
    -- answer (with a warning). The Director never EMPs on its own.
    -- A non-combatant victim house (e.g. a civilian car) never becomes a
    -- retaliation source; the sabotage itself still lands (above).
    if houseName == S.playerHouseName and not S.mpMode
        and isCombatHouseName(bestOwner) then
        S.retaliation = { from = bestOwner, due = S.lastFrame + TUNING.RETALIATE_WARN }
        say(string.format("DIRECTOR: retaliation incoming - impact in %d s. Cover your armor!",
            TUNING.RETALIATE_WARN / 60))
    end
    return true
end

local POWERS = {
    reinforce = powerReinforce,
    repair    = powerRepair,
    blitz     = powerBlitz,
    sabotage  = powerSabotage,
}

local function tryPower(houseName, key, quietOnFail)
    local cost = TUNING.POWER_COST[key]
    if not cost then return false end

    if (S.blitz[houseName] or 0) > S.lastFrame then
        cost = math.max(1, math.floor(cost * BLITZ_DISCOUNT))
    end

    if cpOf(houseName) < cost then
        if not quietOnFail then
            say(string.format("Not enough CP: %s costs %d, you have %d.",
                key, cost, cpOf(houseName)))
        end
        return false
    end

    if POWERS[key](houseName) then
        addCp(houseName, -cost)
        if houseName == S.playerHouseName then
            say(string.format("CP -%d [%s] = %d", cost, key, cpOf(houseName)))
        end
        return true
    end
    return false
end

-- ---------------------------------------------------------------------------
-- Player input: Z/X/C/V powers, T = status
-- ---------------------------------------------------------------------------

local function playerInput()
    if Input.WasKeyPressed(0x54) then -- T: local read-out, safe in every mode
        local ai = aiNames()[1]
        local d = S.directive[S.playerHouseName]
        local dirInfo = d
            and string.format("%s, %ds left", d.kind,
                math.max(0, math.ceil((d.endsAt - S.lastFrame) / 60)))
            or "none"
        local ret = S.retaliation
            and string.format("%ds!", math.max(0, math.ceil((S.retaliation.due - S.lastFrame) / 60)))
            or "none"
        say(string.format("STATUS: CP you %d | %s %d | directive: %s | retaliation: %s",
            cpOf(S.playerHouseName),
            ai and tostring(ai) or "-",
            ai and cpOf(ai) or 0,
            dirInfo, ret))
        return
    end

    if S.mpMode then return end -- powers locked in human-vs-human (no host gate)

    if Input.WasKeyPressed(POWER_KEYS[1]) then
        tryPower(S.playerHouseName, "reinforce")
    elseif Input.WasKeyPressed(POWER_KEYS[2]) then
        tryPower(S.playerHouseName, "repair")
    elseif Input.WasKeyPressed(POWER_KEYS[3]) then
        tryPower(S.playerHouseName, "blitz")
    elseif Input.WasKeyPressed(POWER_KEYS[4]) then
        tryPower(S.playerHouseName, "sabotage")
    end
end

-- ---------------------------------------------------------------------------
-- AI Director: acts on every NON-HUMAN house. NEVER sabotages on its own -
-- retaliation is executed by retaliationLoop below.
-- ---------------------------------------------------------------------------

local function directorAct(ai)
    if cpOf(ai) >= TUNING.POWER_COST.repair and tryPower(ai, "repair", true) then
        return
    end
    if cpOf(ai) >= TUNING.POWER_COST.reinforce and tryPower(ai, "reinforce", true) then
        return
    end
end

local function directorLoop()
    for name in pairs(S.cp) do
        -- Spend-gate: only combat AI houses act as the Director. (Ledger
        -- seeding already excludes non-combatants; this guard keeps the
        -- gate explicit even if a ledger entry were ever recreated.)
        if not isHumanHouse(name) and isCombatHouseName(name) then
            if S.lastFrame >= (S.nextThink[name] or 0) then
                S.nextThink[name] = S.lastFrame + AI_THINK_EVERY
                directorAct(name)
            end
        end
    end
end

-- Retaliation: scheduled by the player's Sabotage, announced, then delivered.
local function retaliationLoop()
    local r = S.retaliation
    if not r or S.lastFrame < r.due then return end
    S.retaliation = nil
    -- Never act as a non-combatant house, even if one was recorded.
    if not isCombatHouseName(r.from) then
        say("Director retaliation fizzled - non-combatant source.")
        return
    end
    say("DIRECTOR RETALIATION!")
    if not POWERS.sabotage(r.from) then
        say("Director retaliation fizzled - no ground target.")
    end
end

-- ---------------------------------------------------------------------------
-- Entry point
-- ---------------------------------------------------------------------------

function AUTH.Update(frame)
    if not S.announced then
        refreshHouseObjects()
        local pname = houseNameOf(playerHouse())
        if pname then
            S.playerHouseName = pname
            -- Combat houses only: Neutral/Special never hold CA command
            -- points, which also keeps them out of every S.cp-driven loop
            -- (survival income, director, directives roster, status).
            for name in pairs(houseObj) do
                if isCombatHouseName(name) then
                    S.cp[name] = S.cp[name] or 0
                end
            end
            S.cp[pname] = S.cp[pname] + TUNING.CP_START

            -- Multiplayer gate: count human houses (simulation state, the
            -- same on every client -> the gate itself is deterministic).
            local humans = 0
            for name in pairs(houseObj) do
                if isHumanHouse(name) then humans = humans + 1 end
            end
            S.mpMode = humans >= 2

            S.announced = true
            if S.mpMode then
                say("COMMAND AUTHORITY (alliance mode): 2+ commanders detected - powers locked, deterministic systems only. Directives and economy run; T = status.")
            else
                say(string.format(
                    "COMMAND AUTHORITY v3 (you: %s). Earn CP: kill +%d | damage +1/%d | streak +%d | directives +%d. Spend: Z Reinforce %d | X Repair %d | C Blitz %d | V Sabotage %d. Sabotage invites Director retaliation. T = status.",
                    pname, TUNING.CP_KILL, TUNING.CP_DAMAGE_PER,
                    TUNING.CP_SURVIVE_PAY, TUNING.DIRECTIVE_PAY,
                    TUNING.POWER_COST.reinforce, TUNING.POWER_COST.repair,
                    TUNING.POWER_COST.blitz, TUNING.POWER_COST.sabotage))
            end
        end
        if not S.announced then return end
    end

    -- Match restart: frame counter went backwards.
    if frame < S.lastFrame then
        S.announced = false
        S.cp = {}
        S.dmgBank = {}
        S.nextThink = {}
        S.seen = {}
        S.losses = {}
        S.blitz = {}
        S.lastKillPos = {}
        S.directive = {}
        S.directiveIndex = {}
        S.nextDirective = {}
        S.retaliation = nil
        S.nextSurvive = TUNING.CP_SURVIVE_EVERY
    end
    S.lastFrame = frame

    -- Scan cadence 4x/s; the survival tick boundary (360) is a multiple of
    -- SCAN_FRAMES (15), so it is never skipped.
    if frame % SCAN_FRAMES == 0 then
        combatScan()
    end

    playerInput()
    directorLoop()
    retaliationLoop()
end

AUTH._S = S -- debug/testing access to live state (harnesses read, never write)

return AUTH
