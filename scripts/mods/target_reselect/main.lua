-- Target Reselect (M14 experiment) - runtime AI target reselection.
--
-- RESEARCH QUESTION: can Lua replace ONE vanilla-AI target-selection decision
-- using runtime battlefield information that INI / Ares / Phobos cannot express?
--
-- Architecture under test, v2 VICTIM-CENTRIC (NOT a smarter-AI claim):
--     Game state -> Lua observes -> Lua decides -> engine executes
--
-- Exactly one decision is tested, observed from the VICTIM side: a player-owned
-- harvester/refinery that is LOSING HP while defended by nearby AA has its
-- current AI attackers shooed off to lower-threat reachable targets via the
-- engine's native `unit:Attack(alt)`, then reads back `unit:GetTarget()` to
-- confirm the engine accepted it.
--
-- Why victim-centric (2026-09-10): the v1 attacker-centric design required a
-- stable attacker Target on a 20-frame tick plus AA within 8 cells of it at
-- that exact tick - four hidden conditions that never coincided in live play
-- (hit-and-run jets, flapping AI retargeting, roaming harvesters). Watching
-- the persistent victim (HP drops don't flap) removes the geometry lottery
-- while keeping the SAME threat gate, weights, radii and thresholds.
--
-- The decision is GENERIC. The only data is a runtime signal (AA threat near
-- the victim under attack); there is no per-enemy-type branch. A totally
-- different signal could replace it.
--
-- VANILLA AI BOUNDARY (important): this mod issues an order and reads it back.
-- It does NOT (and cannot, with the current API) guarantee that the vanilla AI
-- house will not later re-select its own target. That is the true "engine
-- executes + persists" boundary and is documented in PROJECT/RUNTIME_BOUNDARY.md.
--
-- Safety: units are tracked by id only. No Techno/House userdata is retained
-- across frames - every object is re-fetched from a fresh World scan on the tick
-- it is used. Destroyed/invalid objects are skipped.
--
-- How to run: enable `target_reselect`, start a skirmish with an AI opponent,
-- and watch LuaAPI.log for [M14.1] lines.

local util = require("framework.util")

local Mod = {}

local SCAN_EVERY      = 20   -- frames between reselection passes
local AA_RADIUS       = 8    -- cells around the VICTIM to scan for AA
local ALT_RADIUS      = 18   -- cells around the AI unit to look for an alternative
local AA_THREAT_HIGH  = 2.0  -- AA-equivalent threat above which we reconsider
local AA_THREAT_LOW   = 0.8  -- ... and below which we stay (hysteresis)
local ATTACK_RANGE    = 60   -- only consider alternatives within this range
local VICTIM_COOLDOWN = 150  -- frames between re-evaluations of the same victim
                             -- (anti-spam: one evaluation per siege phase, and no
                             -- per-tick native order churn — see the move-order
                             -- freeze postmortem)

-- Data: what counts as anti-air (generic, isolated as data).
-- ID table verified 2026-09-10 against community docs (CnC Wiki infoboxes)
-- AND live log lines:
--   HTK    = Flak track vehicle  (log: HTK@73,122 glued to HARV@72,122;
--            wiki: "Internal ID HTK", trivia "original name was half-track")
--   FLAKT  = Flak trooper infantry (wiki: "Internal ID FLAKT")
--   NAFLAK = Flak cannon structure  (log: NAFLAK@66,82; wiki: "Internal ID NAFLAK";
--            rules gist: "68=NAFLAK ; Flak Cannon")
local AA_TYPES = {
    HTK    = true,  -- Flak track
    NAFLAK = true,  -- Flak cannon
    FLAKT  = true,  -- Flak trooper
    FLAK   = true,  -- retained (unverified; harmless)
    FLAKC  = true,  -- retained (unverified; harmless)
    PPLX   = true,  -- Pillbox? (placeholder, keeps the set data-driven)
}

local AA_KIND_WEIGHT = {
    unit     = 1.0,
    building = 1.1,
    infantry = 0.6,
}

-- Data: what counts as a protected VICTIM (player-side harvest economy).
-- Harvester IDs match the repo's NON_COMBAT sets; refinery IDs are observed
-- live (NAREFN/GAREFN in-match). Yuri refinery ID unknown - omitted rather
-- than invented.
local VICTIM_UNIT_TYPES = {
    HARV = true, CMIN = true, SMIN = true,
}

local VICTIM_BUILDING_TYPES = {
    NAREFN = true, GAREFN = true,
}

local victims = {}  -- victim id -> { type=string, hp=number, coolUntil=frame,
                    --               attacked=bool } (primitives only; no userdata)

local function msg(text)
    if Engine and Engine.PrintMessage then
        Engine.PrintMessage(text)
    end
end

local function log(text)
    print("[TARGET] " .. text)
end

-- [M14.1] diagnostics: change/event-based trail (TARGET / AA / RESELECT /
-- RESELECT_PENDING / TARGET_LOST). Uses the same print-to-log mechanism.
-- Never affects gameplay: string formatting only, all lookups pcall-guarded.
local function dlog(tag, text)
    print("[M14.1][" .. tag .. "] " .. text)
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

-- Is `unit` a player-side protected asset (harvester / refinery)? Same safe
-- pcall style as isEnemyOfPlayer. Repairs (hp up) are NOT attacks - the caller
-- compares hp across ticks and only treats a DROP as an attack.
local function isProtectedAsset(unit)
    if not unit or not util.is_alive(unit) then return false end
    local player = House.GetPlayer()
    if not player then return false end
    local okOwn, owner = pcall(unit.GetOwner, unit)
    if not okOwn or not owner then return false end
    if util.is_neutral_house(owner) then return false end
    if owner ~= player then
        local okAl, allied = pcall(owner.IsAlliedWith, owner, player)
        if not okAl or allied ~= true then return false end
    end
    local kind = util.kind_of(unit)
    local okT, tn = pcall(unit.GetTypeName, unit)
    if not okT or not tn then return false end
    if kind == "unit" and VICTIM_UNIT_TYPES[tn] then return true end
    if kind == "building" and VICTIM_BUILDING_TYPES[tn] then return true end
    return false
end

local function victimHp(unit)
    local ok, hp = pcall(unit.GetHealth, unit)
    return ok and hp or nil
end

-- Is `candidate` a threat to `unit`'s owner (the AI house)? We want the PLAYER's
-- side (the AI's enemy) - i.e. a unit whose owner is NOT allied with the AI unit.
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

-- [M14.1] diagnostics: per-unit classification of ONE radius-scan entry.
-- Mirrors isThreatTo/aaThreat step by step for DISPLAY ONLY - the scoring
-- functions above are untouched and remain the sole deciders. A failed lookup
-- yields a "?/reason" token, never an error, never a behavior change.
local function scanDiag(unit, u)
    if not u or not util.is_alive(u) then return "?/dead" end
    local okT, tn = pcall(u.GetTypeName, u)
    local name = (okT and tn) and tostring(tn) or "?"
    local okAlly, ally = pcall(unit.GetOwner, unit)
    if not okAlly or not ally then return name .. "/noAIowner" end
    local okOwn, owner = pcall(u.GetOwner, u)
    if not okOwn or not owner then return name .. "/noOwner" end
    if owner == ally then return name .. "/sameHouse" end
    if util.is_neutral_house(owner) then return name .. "/neutral" end
    if okT and tn and util.CIVIL_TYPES[tn] then return name .. "/civil" end
    local okRel, rel = pcall(owner.IsAlliedWith, owner, ally)
    if not okRel then return name .. "/allyErr" end
    if rel ~= false then return name .. "/allied" end
    if not (okT and AA_TYPES[tn]) then return name .. "/notAA" end
    local kind = util.kind_of(u)
    local w = AA_KIND_WEIGHT[kind] or 0.5
    return string.format("%s/+%.1f", name, w)
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

    -- PASS 1: collect live protected victims (primitives only; fresh userdata
    -- is used inside this tick and never retained across frames).
    local vics = {}  -- id -> { type, hp, x, y }
    for _, u in ipairs(all) do
        if isProtectedAsset(u) then
            local okId, id = pcall(u.GetId, u)
            local hp = victimHp(u)
            local okP, pos = pcall(u.GetPosition, u)
            local okT, tn = pcall(u.GetTypeName, u)
            if okId and id and hp and okP and pos and pos.x and pos.y and okT and tn then
                vics[id] = { type = tostring(tn), hp = hp, x = pos.x, y = pos.y }
            end
        end
    end

    -- PASS 2: vanished victims (destroyed since last tick): log once, forget.
    for vid, st in pairs(victims) do
        if not vics[vid] then
            dlog("VICTIM_LOST", string.format("victim=%s(%s) tick=%d",
                tostring(vid), st.type, frame))
        end
    end

    -- PASS 3: per victim - defended? shoo whoever holds it right now.
    -- Lock-on suffices; damage is logged (VICTIM) but never required.
    local carried = {}
    for vid, v in pairs(vics) do
        local prior = victims[vid]
        -- An HP DROP since last tick means "under attack right now".
        -- Repairs (hp up) and first sightings are not attacks.
        local attacked = prior and prior.hp ~= nil and v.hp < prior.hp or false
        local cooling = prior and frame < (prior.coolUntil or 0) or false

        -- Attackers currently holding THIS victim as target (fresh objects,
        -- same tick only - never retained).
        local attackers = {}
        for _, u in ipairs(all) do
            if util.is_alive(u) and util.is_mobile(u) and isEnemyOfPlayer(u) then
                local okK, kind = pcall(u.GetKind, u)
                if okK and (kind == "aircraft" or kind == "unit") then
                    if currentTargetId(u) == vid then
                        local okA, aid = pcall(u.GetId, u)
                        if okA and aid then
                            attackers[#attackers + 1] = { id = aid, obj = u }
                        end
                    end
                end
            end
        end

        -- [M14.1] VICTIM: transition into under-attack (change-based, no spam).
        if attacked and not (prior and prior.attacked) then
            dlog("VICTIM", string.format("victim=%s(%s) hp=%s attackers=%d tick=%d",
                tostring(vid), v.type, tostring(v.hp), #attackers, frame))
        end

        local newCool = prior and prior.coolUntil or 0
        -- ACT gate: lock-on + not cooling is enough. NO damage requirement:
        -- jet lock-ons never coincide with damage ticks, so waiting for a
        -- drop means waiting forever. HP tracking stays for VICTIM/LOST logs.
        if #attackers > 0 and not cooling then
            -- DEFENDED? AA threat around the VICTIM, seen from the AI side
            -- (perspective = first attacker). isThreatTo/aaThreat UNCHANGED.
            local per = attackers[1].obj
            local okF, found = pcall(World.GetUnitsInRadius, v.x, v.y, AA_RADIUS)
            local threat = nil
            if okF and found then threat = aaThreat(per, v.x, v.y, found) end
            if threat ~= nil then
                dlog("SCAN", string.format("victim=%s(%s) threat=%.2f pop=%d tick=%d",
                    tostring(vid), v.type, threat, #found, frame))
                local parts = {}
                for _, fu in ipairs(found) do
                    parts[#parts + 1] = scanDiag(per, fu)
                end
                dlog("SCANUNITS", string.format("target=%s tick=%d :: %s",
                    tostring(vid), frame, table.concat(parts, " ")))
                -- [M14.1] CENSUS: live player-side units with TRUE engine
                -- typenames + cells. Decisive split: FLAKT present far from
                -- the victim -> geometry; absent everywhere -> dead/unbuilt;
                -- AA present under another name -> typename bug in AA_TYPES.
                -- Same-tick walk of the already-fetched list; evaluation-gated
                -- (cooldown), so no spam.
                do
                    local okPl, plHouse = pcall(House.GetPlayer)
                    if okPl and plHouse then
                        local cen, extra = {}, 0
                        for _, cu in ipairs(all) do
                            local okO, ow = pcall(cu.GetOwner, cu)
                            if okO and ow and ow == plHouse and util.is_alive(cu) then
                                local okCT, ctn = pcall(cu.GetTypeName, cu)
                                local okCP, cp = pcall(cu.GetPosition, cu)
                                if #cen < 40 and okCT and ctn and okCP and cp and cp.x and cp.y then
                                    cen[#cen + 1] = string.format("%s@%d,%d",
                                        tostring(ctn), cp.x, cp.y)
                                else
                                    extra = extra + 1
                                end
                            end
                        end
                        dlog("CENSUS", string.format("tick=%d victim=%s :: %s%s",
                            frame, tostring(vid), table.concat(cen, " "),
                            extra > 0 and (" (+" .. extra .. " more)") or ""))
                    end
                end
                if threat >= AA_THREAT_HIGH then
                    dlog("AA", string.format("victim=%s threat=%.2f threshold=%s attackers=%d tick=%d",
                        tostring(vid), threat, tostring(AA_THREAT_HIGH), #attackers, frame))
                    for _, a in ipairs(attackers) do
                        local alt = pickAlternative(a.obj, ALT_RADIUS, vid)
                        if alt then
                            local okIda, aid = pcall(alt.GetId, alt)
                            local altType = "?"
                            local okAn, an = pcall(alt.GetTypeName, alt)
                            if okAn and an then altType = tostring(an) end
                            local okA, acc = pcall(a.obj.Attack, a.obj, alt)
                            if okA and acc then
                                -- read-back: confirm the ENGINE accepted the target.
                                local okR, newT = pcall(a.obj.GetTarget, a.obj)
                                local newId = nil
                                if okR and newT then
                                    local okN, nid = pcall(newT.GetId, newT)
                                    if okN then newId = nid end
                                end
                                log(string.format(
                                    "RESELECT attacker #%d: victim %s->%s (aa %.2f) accepted=%s readback=%s",
                                    a.id, tostring(vid), tostring(okIda and aid or "?"),
                                    threat, tostring(acc), tostring(newId)))
                                -- [M14.1] RESELECT at the actual target-change point.
                                dlog("RESELECT", string.format(
                                    "attacker=%s old=%s(%s) new=%s(%s) reason=VICTIM_DEFENDED tick=%d",
                                    tostring(a.id), tostring(vid), v.type,
                                    tostring(okIda and aid or newId or "?"), altType, frame))
                                msg(string.format("[TARGET] attacker #%d shooed to %s",
                                    a.id, tostring(alt.GetTypeName and alt:GetTypeName())))
                            else
                                dlog("RESELECT_PENDING", string.format(
                                    "attacker=%s target=%s reason=AA_THREAT_ATTACK_REJECTED tick=%d",
                                    tostring(a.id), tostring(vid), frame))
                            end
                        else
                            dlog("RESELECT_PENDING", string.format(
                                "attacker=%s target=%s reason=AA_THREAT_NO_ALTERNATIVE tick=%d",
                                tostring(a.id), tostring(vid), frame))
                        end
                    end
                end
                -- One evaluation per siege phase (acted or not): silence - no
                -- orders, no logs - until the cooldown expires. Prevents both
                -- log spam and per-tick native order churn.
                newCool = frame + VICTIM_COOLDOWN
            end
        end

        carried[vid] = { type = v.type, hp = v.hp,
                         coolUntil = newCool, attacked = attacked }
    end
    victims = carried
end

function Mod.Update(frame)
    -- [M14.1] diagnostics: periodic player-army census (every 300 frames),
    -- INDEPENDENT of attacks. If live on-screen flaks never appear here,
    -- the engine scan itself hides them (scan-level bug); if they appear,
    -- the question is only geometry/timing at evaluation ticks.
    -- NOTE: checked BEFORE the SCAN_EVERY gate (300 is a multiple of 20).
    if frame % 300 == 0 then
        local okA, allA = pcall(World.GetAllUnits)
        local okPl, plHouse = pcall(House.GetPlayer)
        if okA and allA and okPl and plHouse then
            local cen, extra = {}, 0
            for _, cu in ipairs(allA) do
                local okO, ow = pcall(cu.GetOwner, cu)
                if okO and ow and ow == plHouse and util.is_alive(cu) then
                    local okCT, ctn = pcall(cu.GetTypeName, cu)
                    local okCP, cp = pcall(cu.GetPosition, cu)
                    if #cen < 40 and okCT and ctn and okCP and cp and cp.x and cp.y then
                        cen[#cen + 1] = string.format("%s@%d,%d",
                            tostring(ctn), cp.x, cp.y)
                    else
                        extra = extra + 1
                    end
                end
            end
            dlog("CENSUS", string.format("tick=%d auto=1 :: %s%s",
                frame, table.concat(cen, " "),
                extra > 0 and (" (+" .. extra .. " more)") or ""))
        end
    end
    if frame % SCAN_EVERY ~= 0 then
        return
    end
    local ok, err = pcall(tick, frame)
    if not ok then
        log("update error: " .. tostring(err))
    end
end

return Mod
