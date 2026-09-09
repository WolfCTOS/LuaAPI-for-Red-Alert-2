-- Target Reselect (M14 experiment) — runtime AI target reselection.
--
-- RESEARCH QUESTION: can Lua replace ONE vanilla-AI target-selection decision
-- using runtime battlefield information that INI / Ares / Phobos cannot express?
--
-- Architecture under test (NOT a smarter-AI claim):
--     Game state → Lua observes → Lua decides → engine executes
--
-- Exactly one decision is tested: an AI-owned mobile unit that is currently
-- attacking (or idle) keeps/abandons its current target based on a live signal
-- computed from runtime state: anti-air threat concentration near the CURRENT
-- target. If the signal crosses a threshold, Lua issues the engine's native
-- `unit:Attack(alt)` to a lower-threat reachable target, then reads back
-- `unit:GetTarget()` to confirm the engine accepted it.
--
-- The decision is GENERIC. The only data is a runtime signal (AA threat near the
-- current target); there is no `if typeName == "SOME_UNIT"` branch. A totally
-- different signal could replace it.
--
-- VANILLA AI BOUNDARY (important): this mod issues an order and reads it back.
-- It does NOT (and cannot, with the current API) guarantee that the vanilla AI
-- house will not later re-select its own target. That is the true "engine
-- executes + persists" boundary and is documented in PROJECT/RUNTIME_BOUNDARY.md.
--
-- Safety: units are tracked by id only. No Techno/House userdata is retained
-- across frames — every object is re-fetched from a fresh World scan on the tick
-- it is used. Destroyed/invalid objects are skipped.
--
-- How to run: enable `target_reselect`, start a skirmish with an AI opponent,
-- and watch LuaAPI.log for [TARGET] lines.

local util = require("framework.util")

local Mod = {}

local SCAN_EVERY      = 20   -- frames between reselection passes
local AA_RADIUS       = 8    -- cells around the current target to scan for AA
local ALT_RADIUS      = 18   -- cells around the AI unit to look for an alternative
local AA_THREAT_HIGH  = 2.0  -- AA-equivalent threat above which we reconsider
local AA_THREAT_LOW   = 0.8  -- ... and below which we stay (hysteresis)
local ATTACK_RANGE    = 60   -- only consider alternatives within this range

-- Data: what counts as anti-air (generic, isolated as data).
local AA_TYPES = {
    FLAKT = true,  -- Flak track
    FLAK  = true,  -- Flak cannon
    FLAKC = true,  -- Flak cannon (alternate)
    PPLX  = true,  -- Pillbox? (placeholder, keeps the set data-driven)
}

local AA_KIND_WEIGHT = {
    unit     = 1.0,
    building = 1.1,
    infantry = 0.6,
}

local seen = {}   -- id -> recent per-unit state (primitives only; no userdata)

local function msg(text)
    if Engine and Engine.PrintMessage then
        Engine.PrintMessage(text)
    end
end

local function log(text)
    print("[TARGET] " .. text)
end

-- Is `unit` an AI-controlled unit (i.e. hostile to the player house)? Used to
-- pick which units are the "AI side" to influence at all.
local function isEnemyOfPlayer(unit)
    local player = House.GetPlayer()
    if not player or not unit or not util.is_alive(unit) then return false end
    local okOwn, owner = pcall(unit.GetOwner, unit)
    if not okOwn or not owner then return false end
    if owner == player then return false end
    -- Neutrals/civilians are never the "AI side": don't read or steer them.
    if util.is_neutral_house(owner) then return false end
    local okAl, allied = pcall(owner.IsAlliedWith, owner, player)
    return okAl and allied == false
end

-- Is `candidate` a threat to `unit`'s owner (the AI house)? We want the PLAYER's
-- side (the AI's enemy) — i.e. a unit whose owner is NOT allied with the AI unit.
-- Returns true for the player's/human's units and allied-to-player houses.
local function isThreatTo(unit, candidate)
    if not unit or not candidate or not util.is_alive(candidate) then return false end
    local okAlly, ally = pcall(unit.GetOwner, unit)
    if not okAlly or not ally then return false end
    local okOwn, owner = pcall(candidate.GetOwner, candidate)
    if not okOwn or not owner then return false end
    if owner == ally then return false end
    -- Neutrals, civilians and civilian cars are never threats/targets:
    -- no redirecting AI fire onto derricks, hospitals or traffic.
    if util.is_neutral_house(owner) then return false end
    local okT, tn = pcall(candidate.GetTypeName, candidate)
    if okT and tn and util.CIVIL_TYPES[tn] then return false end
    local okRel, rel = pcall(owner.IsAlliedWith, owner, ally)
    return okRel and rel == false
end

-- Anti-air threat concentration near a position, from the AI unit's perspective.
-- It counts candidate units that are hostile to `unit` (the AI side).
local function aaThreat(unit, x, y, found)
    local threat = 0.0
    for _, u in ipairs(found) do
        if isThreatTo(unit, u) then
            local okT, typeName = pcall(u.GetTypeName, u)
            local kind = util.kind_of(u)
            local w = AA_KIND_WEIGHT[kind] or 0.5
            if okT and AA_TYPES[typeName] then
                threat = threat + w
            end
        end
    end
    return threat
end

-- Pick the lowest-AA-threat legitimate target within ALT_RADIUS of the AI unit.
-- Returns a fresh userdata (valid only for this call) or nil. `currentId` is the
-- id of the current target, used as a tie-break so we don't ping-pong.
local function pickAlternative(unit, radius, currentId)
    local ok, found = pcall(World.GetUnitsInRadius, unit:GetPosition().x, unit:GetPosition().y, radius)
    if not ok or not found then return nil end

    local best, bestThreat = nil, math.huge
    -- candidates must be threats to `unit` (i.e. valid targets for the AI unit).
    -- Exclude AA-*type* candidates: retargeting onto a Flak is not a safer move.
    for _, u in ipairs(found) do
        if isThreatTo(unit, u) then
            local okT, typeName = pcall(u.GetTypeName, u)
            if okT and AA_TYPES[typeName] then
                goto continue_candidate
            end
            local kg = util.kind_of(u)
            if kg == "unit" or kg == "infantry" or kg == "aircraft" then
                local ux, uy = u:GetPosition().x, u:GetPosition().y
                local threat = aaThreat(unit, ux, uy, found)
                -- Prefer a lower-threat target that we're not already attacking.
                local okId, uid = pcall(u.GetId, u)
                if okId and uid and uid ~= currentId and threat < bestThreat then
                    bestThreat = threat
                    best = u
                end
            end
            ::continue_candidate::
        end
    end
    return best
end

-- Read the current target id of a fresh unit (or nil).
local function currentTargetId(unit)
    local ok, target = pcall(unit.GetTarget, unit)
    if not ok or not target then return nil end
    local okId, tid = pcall(target.GetId, target)
    return okId and tid or nil
end

local function tick(frame)
    local ok, all = pcall(World.GetAllUnits)
    if not ok or not all then return end

    for _, unit in ipairs(all) do
        -- Only AI-owned mobile combat units, alive.
        if not util.is_alive(unit) or not util.is_mobile(unit) or not isEnemyOfPlayer(unit) then
            goto continue_unit
        end

        local okId, id = pcall(unit.GetId, unit)
        if not okId or not id then goto continue_unit end

        local okKind, kind = pcall(unit.GetKind, unit)
        if okKind and (kind == "aircraft" or kind == "unit") then
            local okPos, pos = pcall(unit.GetPosition, unit)
            if okPos and pos then
                -- OBSERVE: current target + its AA signal.
                local tId = currentTargetId(unit)
                local tThreat = nil
                if tId then
                    local robj = nil
                    for _, cand in ipairs(all) do
                        local okCid, cid = pcall(cand.GetId, cand)
                        if okCid and cid == tId then robj = cand; break end
                    end
                    if robj then
                        local okTPos, tpos = pcall(robj.GetPosition, robj)
                        if okTPos and tpos then
                            local okF, found = pcall(World.GetUnitsInRadius, tpos.x, tpos.y, AA_RADIUS)
                            if okF and found then tThreat = aaThreat(unit, tpos.x, tpos.y, found) end
                        end
                    end
                end

                -- EVALUATE + DECIDE.
                local prior = seen[id]
                local still = prior and prior.lastTargetId == tId
                local decideReselect = false
                if tThreat ~= nil then
                    if prior and prior.threat ~= nil and still then
                        -- rising crossing in the SAME target: hysteresis avoids
                        -- reselecting every frame.
                        decideReselect = (prior.threat < AA_THREAT_HIGH and tThreat >= AA_THREAT_HIGH)
                    else
                        decideReselect = (tThreat >= AA_THREAT_HIGH)
                    end
                end

                -- Remember this observation (primitives only).
                seen[id] = { lastTargetId = tId, threat = tThreat }

                -- ACT (only on a rising crossing). Then read back the engine's
                -- accepted target. This is the 'engine executes' probe.
                if decideReselect and tId then
                    local alt = pickAlternative(unit, ALT_RADIUS, tId)
                    if alt then
                        local okIda, aid = pcall(alt.GetId, alt)
                        local okA, acc = pcall(unit.Attack, unit, alt)
                        if okA and acc then
                            -- read-back: confirm the ENGINE accepted the target.
                            local okR, newT = pcall(unit.GetTarget, unit)
                            local newId = nil
                            if okR and newT then
                                local okN, nid = pcall(newT.GetId, newT)
                                if okN then newId = nid end
                            end
                            log(string.format(
                                "RESELECT #%d: target %s->%s (aa %.2f) accepted=%s readback=%s",
                                id, tostring(tId), tostring(okIda and aid or "?"),
                                tThreat, tostring(acc), tostring(newId)))
                            msg(string.format("[TARGET] reselected #%d to %s",
                                id, tostring(alt.GetTypeName and alt:GetTypeName())))
                        end
                    end
                end
            end
        end

        ::continue_unit::
    end
end

function Mod.Update(frame)
    if frame % SCAN_EVERY ~= 0 then
        return
    end
    local ok, err = pcall(tick, frame)
    if not ok then
        log("update error: " .. tostring(err))
    end
end

return Mod
