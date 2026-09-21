local util = require("framework.util")

local SmartAI = {}

local SCAN_INTERVAL = 30       -- Scan every 30 frames (~1 second)
local lastScanFrame = 0

-- ---------------------------------------------------------------------------
-- Capture-aware valuables guard (MVP): high-value own units avoid lone
-- exposure to observable vanilla mind-control threats. Vanilla capture
-- itself is untouched and unimplemented here: this only reads owner /
-- type / position via LuaAPI and issues standard MoveTo orders.
--
-- Vanilla ground truth being reasoned about (not implemented): Yuri
-- mind-control units flip a victim's owner (Yuri clone, Yuri Prime,
-- Mastermind; Hijacker ID unconfirmed and Chaos Drone causes frenzy,
-- not capture, so both are excluded). A flip is observable afterwards
-- as a GetOwner() change on a tracked id.
--
-- VALUABLE_TYPES lists high-value TypeIDs to protect. Extend with real
-- TypeIDs as needed (e.g. a custom Nuclear Truck goes here under the ID
-- of the mod that defines it; no such unit exists in this repo — verified
-- by repo-wide search). THREAT_TYPES lists observable capturer TypeIDs:
-- MIND was observed live; YURIPR/YURI are rules/wiki IDs for Yuri Prime
-- and the Yuri clone (both mind-control infantry).
-- ---------------------------------------------------------------------------

local VALUABLE_TYPES = {
    APOC = true,               -- Apocalypse: expensive, slow, high-value
    HTNK = true,               -- Rhino: main-line armor, capture-observed live
                               -- (HTNK#1058871 Africans -> YuriCountry,
                               -- Dannath session) - lone Rhinos are exactly
                               -- what mind-control punishes
}

local CAPTURE_THREATS = {
    MIND = true,               -- Mastermind: controls vehicles (live-observed)
    YURIPR = true,             -- Yuri Prime: mind control (rules/wiki ID)
    YURI = true,               -- Yuri clone: mind control (rules/wiki ID)
}

local THREAT_RADIUS = 9        -- mind-control engagement envelope class
local HOLD_RADIUS = 4          -- with-group distance: no lone exposure
local REORDER_EVERY = 600      -- refresh pullback at most every ~10 s

local guardState = {}          -- unitId -> { threatened = bool, orderedFrame = n }
local seenOwner = {}           -- unitId -> ownerName (flip observation only)
local guardLastFrame = 0

local function guardReset()
    guardState = {}
    seenOwner = {}
end

-- ---------------------------------------------------------------------------
-- Commander + Officers: AI-house coordination layer (classic split restored:
-- Commander manages the BASE, Officer manages UNITS).
-- One shared scan feeds everything below — no extra World scans. Officers
-- assign ROLES (escort / garrison-screen), never tactics: vanilla AI keeps
-- attacking, officers only attach support.
-- Deterministic (ID-ordered picks, no RNG/clock) and churn-free
-- (transition-only orders + destination memory + refresh caps).
--
-- Escort officer: own ARTILLERY_TYPES (V3, INI-confirmed [V3]) get up to
-- ESCORT_N idle combat bodyguards that follow at destination memory.
-- Engaged bodyguards are released, never yanked.
-- Garrison officer (EXPERIMENTAL): idle own infantry near own DEFENSE_TYPES
-- are ordered onto the building cell (vanilla enter-if-garrisonable, screen
-- otherwise). BUNKER id is UNCONFIRMED live (no BUNK* section in the repo's
-- INI subset); the directive log prints the acted type name so a live
-- census confirms it. NAPILL/GAPILL are INI-confirmed but screen-only
-- (not garrisonable).
-- ---------------------------------------------------------------------------

local ARTILLERY_TYPES = { V3 = true }
local DEFENSE_TYPES = { BUNKER = true, NAPILL = true, GAPILL = true }
local HARVESTER_TYPES = { SMIN = true, HARV = true, CMIN = true }
local MCV_TYPES = { AMCV = true, SMCV = true, YMCV = true }

local ESCORT_N = 2
local ESCORT_RADIUS = 6
local ESCORT_EVERY = 300
local GARRISON_N = 3
local GARRISON_RADIUS = 12
local GARRISON_EVERY = 600

local escortState = {}   -- v3id -> { guards = {ids}, ax, ay, orderedFrame }
local garrisonState = {} -- bldId -> { orderedFrame }
local cmdState = {}      -- house -> { x, y, frame } last breach (Commander blackboard)

local function officerReset()
    escortState = {}
    garrisonState = {}
    cmdState = {}
end

-- Coordination: units the Officer is actively handling (escort bodyguards,
-- threatened pullbacks). Commander rally stands off them — no order fights.
local function isOfficerAssigned(id)
    local gs = guardState[id]
    if gs and gs.threatened then return true end
    for _, st in pairs(escortState) do
        for _, gid in ipairs(st.guards) do
            if gid == id then return true end
        end
    end
    return false
end

local function findUnitById(list, id)
    for _, u in ipairs(list) do
        if u:IsAlive() then
            local ok, uid = pcall(u.GetId, u)
            if ok and uid == id then return u end
        end
    end
    return nil
end

function SmartAI.Update(frame)
    -- Match restart: frame counter went backwards (same pattern as
    -- Command Authority). Resets both the legacy scan gate and the
    -- capture-guard state so a new match starts clean.
    if frame < lastScanFrame then lastScanFrame = 0 end
    if frame < guardLastFrame then guardReset(); officerReset() end
    guardLastFrame = frame

    if frame - lastScanFrame < SCAN_INTERVAL then return end
    lastScanFrame = frame

    local humanPlayer = House.GetPlayer()
    if not humanPlayer then return end

    local buildings = World.GetBuildings()
    local units = World.GetUnits()

    -- Group AI houses
    local aiHouses = {}
    for idx = 0, House.GetCount() - 1 do
        local h = House.GetByIndex(idx)
        if h and not h:IsHuman() and not h:IsAlliedWith(humanPlayer) and not util.is_neutral_house(h) then
            table.insert(aiHouses, h)
        end
    end

    if #aiHouses == 0 then return end

    -- COMMANDER recon (base): breach detection feeds the shared blackboard.
    -- pendingBreach is same-scan only (building userdata never persists).
    -- Officer runs before Commander acts, so the rally below sees fresh
    -- Officer assignments and stands off them (mutual coordination).
    local pendingBreach = {}
    for _, aiHouse in ipairs(aiHouses) do
        for _, bld in ipairs(buildings) do
            if bld:IsAlive() and bld:GetOwner() == aiHouse
                and bld:GetHealth() < (bld:GetMaxHealth() * 0.85) then
                pendingBreach[aiHouse] = bld
                break
            end
        end
    end

    -- OFFICER (units): maneuvers, pullbacks, observation. Never touches base.
    -- Capture-aware valuables guard (see header block). Per AI house:
    -- valuable unit + nearby observable capturer + lone exposure
    -- -> pull back toward the own group (MoveTo only, once per
    -- transition + periodic refresh). Engaged units are never yanked.
    -- Owner flips are logged as observations (possible vanilla
    -- capture/mind-control); they drive no decisions.
    local seenNow = {}
    for _, u in ipairs(units) do
        if u:IsAlive() then
            local id = u:GetId()
            if id then
                seenNow[id] = true
                local owner = u:GetOwner()
                local oname = owner and owner:GetName() or nil
                if oname then
                    local prev = seenOwner[id]
                    if prev and prev ~= oname then
                        local note = string.format(
                            "[AI Commander] Ownership change observed: %s#%s %s -> %s (possible vanilla capture/mind-control)",
                            tostring(u:GetTypeName()), tostring(id),
                            tostring(prev), tostring(oname))
                        Engine.PrintMessage(note)
                        print("[LuaAPI] " .. note)
                    end
                    seenOwner[id] = oname
                end
            end
        end
    end
    for id in pairs(seenOwner) do
        if not seenNow[id] then seenOwner[id] = nil end
    end
    for id in pairs(guardState) do
        if not seenNow[id] then guardState[id] = nil end
    end

    for _, aiHouse in ipairs(aiHouses) do
        local aiName = aiHouse:GetName()
        -- Own mobiles, ID-ordered (deterministic centroid).
        local tagged = {}
        for _, u in ipairs(units) do
            if u:IsAlive() then
                local owner = u:GetOwner()
                if owner and owner == aiHouse then
                    local id = u:GetId()
                    if id then tagged[#tagged + 1] = { u = u, id = id } end
                end
            end
        end
        table.sort(tagged, function(a, b) return a.id < b.id end)
        local cx, cy, n = 0, 0, 0
        for _, e in ipairs(tagged) do
            local p = e.u:GetPosition()
            if p and p.x and p.y then cx, cy, n = cx + p.x, cy + p.y, n + 1 end
        end
        if n > 0 then
            cx, cy = cx / n, cy / n
            for _, e in ipairs(tagged) do
                local u = e.u
                local tname = u:GetTypeName()
                if tname and VALUABLE_TYPES[tname] and u:IsIdle() then
                    -- Nearest observable hostile capturer.
                    local nearest = nil
                    for _, v in ipairs(units) do
                        if v:IsAlive() and v ~= u then
                            local vt = v:GetTypeName()
                            if vt and CAPTURE_THREATS[vt] then
                                local vo = v:GetOwner()
                                local von = vo and vo:GetName() or nil
                                if von and von ~= aiName and not aiHouse:IsAlliedWith(vo) then
                                    local d = u:GetDistanceTo(v)
                                    if d and (not nearest or d < nearest) then
                                        nearest = d
                                    end
                                end
                            end
                        end
                    end
                    local up = u:GetPosition()
                    local exposed = up and up.x and up.y and
                        ((up.x - cx) * (up.x - cx) + (up.y - cy) * (up.y - cy)
                            > HOLD_RADIUS * HOLD_RADIUS)
                    local st = guardState[e.id]
                    if nearest and nearest <= THREAT_RADIUS and exposed then
                        if not st or not st.threatened
                            or (frame - (st.orderedFrame or 0) >= REORDER_EVERY) then
                            u:MoveTo(math.floor(cx + 0.5), math.floor(cy + 0.5))
                            guardState[e.id] = { threatened = true, orderedFrame = frame }
                            local alert = string.format(
                                "[AI Commander - %s] %s#%s capture risk (threat %.1f cells): pulling back to group!",
                                aiName, tostring(tname), tostring(e.id), nearest)
                            Engine.PrintMessage(alert)
                            print("[LuaAPI] " .. alert)
                        end
                    elseif st and st.threatened then
                        guardState[e.id] = nil
                    end
                end
            end
        end
    end

    -- Commander + Officer share this scan (no extra World calls).
    -- Split: Commander takes base defense, Officer takes unit escort.
    local v3ids, defids = {}, {}
    for _, aiHouse in ipairs(aiHouses) do
        local aiName = aiHouse:GetName()
        local ownUnits, ownBlds = {}, {}
        for _, u in ipairs(units) do
            if u:IsAlive() and u:GetKind() == "unit" and u:GetOwner() == aiHouse then
                local id = u:GetId()
                if id then ownUnits[#ownUnits + 1] = { u = u, id = id } end
            end
        end
        for _, b in ipairs(buildings) do
            if b:IsAlive() and b:GetOwner() == aiHouse then
                local id = b:GetId()
                if id then ownBlds[#ownBlds + 1] = { b = b, id = id } end
            end
        end
        table.sort(ownUnits, function(a, b) return a.id < b.id end)
        table.sort(ownBlds, function(a, b) return a.id < b.id end)

        -- OFFICER: escort for own artillery (units business).
        local taken = {}
        for _, e in ipairs(ownUnits) do
            local tname = e.u:GetTypeName()
            if tname and ARTILLERY_TYPES[tname] then
                local pos = e.u:GetPosition()
                if pos and pos.x and pos.y then
                    v3ids[e.id] = true
                    local st = escortState[e.id]
                    if not st then
                        st = { guards = {}, ax = pos.x, ay = pos.y, orderedFrame = 0,
                               houseName = aiName, typename = tname }
                        escortState[e.id] = st
                    end
                    -- Commander priority (blackboard, previous scan): a V3
                    -- inside an active breach sector rallies with the base;
                    -- escort stands down, hygiene releases + logs below.
                    local br = cmdState[aiHouse]
                    local standDown = br and frame - br.frame < 120
                        and (pos.x - br.x) * (pos.x - br.x)
                            + (pos.y - br.y) * (pos.y - br.y) <= 225
                    st.standDown = standDown or nil
                    local live = {}
                    for _, gid in ipairs(st.guards) do
                        local g = findUnitById(units, gid)
                        if g and g:IsIdle() then
                            live[#live + 1] = gid
                            taken[gid] = true
                        end
                    end
                    st.guards = live
                    local moved = (pos.x - st.ax) * (pos.x - st.ax)
                        + (pos.y - st.ay) * (pos.y - st.ay)
                        > ESCORT_RADIUS * ESCORT_RADIUS
                    local need = ESCORT_N - #live
                    if not st.standDown
                        and (need > 0 or moved or frame - st.orderedFrame >= ESCORT_EVERY) then
                        local cands = {}
                        for _, c in ipairs(ownUnits) do
                            local cn = c.u:GetTypeName() or ""
                            if c.id ~= e.id and not taken[c.id] and c.u:IsIdle()
                                and not HARVESTER_TYPES[cn] and not MCV_TYPES[cn]
                                and not ARTILLERY_TYPES[cn] then
                                local d = c.u:GetDistanceTo(e.u)
                                if d then cands[#cands + 1] = { c = c, d = d } end
                            end
                        end
                        table.sort(cands, function(a, b)
                            if a.d ~= b.d then return a.d < b.d end
                            return a.c.id < b.c.id
                        end)
                        local added = 0
                        for i = 1, math.min(need > 0 and need or #live, #cands) do
                            local g = cands[i].c
                            if need > 0 then
                                if g.u:MoveTo(pos.x, pos.y) then
                                    st.guards[#st.guards + 1] = g.id
                                    taken[g.id] = true
                                    added = added + 1
                                    need = need - 1
                                end
                            end
                        end
                        if need <= 0 and #live > 0 and added == 0 then
                            -- Follow refresh: V3 moved or cap elapsed, guards hold.
                            for _, gid in ipairs(live) do
                                local g = findUnitById(units, gid)
                                if g then g:MoveTo(pos.x, pos.y) end
                            end
                        end
                        if added > 0 or moved then
                            st.ax, st.ay, st.orderedFrame = pos.x, pos.y, frame
                            local alert = string.format(
                                "[AI Commander - %s] %s#%s escort +%d bodyguard(s).",
                                tostring(aiName), tostring(tname), tostring(e.id), added)
                            Engine.PrintMessage(alert)
                            print("[LuaAPI] " .. alert)
                        elseif #live > 0 then
                            st.orderedFrame = frame
                        end
                    end
                end
            end
        end

        -- COMMANDER: garrison for own defenses (base business; EXPERIMENTAL).
        for _, e in ipairs(ownBlds) do
            local tname = e.b:GetTypeName()
            if tname and DEFENSE_TYPES[tname] then
                defids[e.id] = true
                local gs = garrisonState[e.id]
                if not gs or frame - gs.orderedFrame >= GARRISON_EVERY then
                    local bp = e.b:GetPosition()
                    if bp and bp.x and bp.y then
                        local cands = {}
                        for _, u in ipairs(units) do
                            if u:IsAlive() and u:GetKind() == "infantry" and u:IsIdle()
                                and u:GetOwner() == aiHouse then
                                local d = u:GetDistanceTo(e.b)
                                if d and d <= GARRISON_RADIUS then
                                    local id = u:GetId()
                                    if id then cands[#cands + 1] = { u = u, d = d, id = id } end
                                end
                            end
                        end
                        table.sort(cands, function(a, b)
                            if a.d ~= b.d then return a.d < b.d end
                            return a.id < b.id
                        end)
                        local sent = 0
                        for i = 1, math.min(GARRISON_N, #cands) do
                            if cands[i].u:MoveTo(bp.x, bp.y) then sent = sent + 1 end
                        end
                        if sent > 0 then
                            garrisonState[e.id] = { orderedFrame = frame }
                            local alert = string.format(
                                "[AI Commander - %s] GARRISON? %s (%d inf) -> %s#%s (experimental).",
                                tostring(aiName), tostring(tname), sent,
                                tostring(tname), tostring(e.id))
                            Engine.PrintMessage(alert)
                            print("[LuaAPI] " .. alert)
                        end
                    end
                end
            end
        end
    end

    -- COMMANDER acts (base): rally, working around Officer assignments.
    for _, aiHouse in ipairs(aiHouses) do
        local aiName = aiHouse:GetName()
        local breachedBuilding = pendingBreach[aiHouse]
        if breachedBuilding and breachedBuilding:IsAlive() then
            local bPos = breachedBuilding:GetPosition()
            if bPos and bPos.x and bPos.y then
                local ralliedCount = 0
                for _, u in ipairs(units) do
                    if u:IsAlive() and u:GetKind() == "unit" then
                        local uOwner = u:GetOwner()
                        if uOwner and uOwner == aiHouse then
                            local uid = u:GetId()
                            local dist = u:GetDistanceTo(breachedBuilding)
                            if uid and dist and dist > 6.0 and u:IsIdle()
                                and not isOfficerAssigned(uid) then
                                -- Command reserve tank to reinforce the breached flank!
                                u:MoveTo(bPos.x, bPos.y)
                                u:Hunt()
                                ralliedCount = ralliedCount + 1
                            end
                        end
                    end
                end
                if ralliedCount > 0 then
                    local alert = string.format("\u{1F6A8} [AI Commander - %s] Flank breach at (%d,%d)! Rallied %d reserve tanks to counter-attack!",
                        aiName, bPos.x, bPos.y, ralliedCount)
                    Engine.PrintMessage(alert)
                    print("[LuaAPI] " .. alert)
                end
            end
            local bp = breachedBuilding:GetPosition()
            if bp and bp.x and bp.y then
                cmdState[aiHouse] = { x = bp.x, y = bp.y, frame = frame }
            end
        else
            cmdState[aiHouse] = nil
        end
    end

    -- Officer state hygiene: drop principals that vanished; release
    -- stand-down escorts (Commander breach priority) with one log line.
    for id, st in pairs(escortState) do
        if not v3ids[id] then
            escortState[id] = nil
        elseif st.standDown then
            if st.guards and #st.guards > 0 then
                local alert = string.format(
                    "[AI Commander - %s] V3#%s escort released: base breach priority.",
                    tostring(st.houseName or "?"), tostring(id))
                Engine.PrintMessage(alert)
                print("[LuaAPI] " .. alert)
            end
            escortState[id] = nil
        end
    end
    for id in pairs(garrisonState) do
        if not defids[id] then garrisonState[id] = nil end
    end
    local houseSet = {}
    for _, h in ipairs(aiHouses) do houseSet[h] = true end
    for h in pairs(cmdState) do
        if not houseSet[h] then cmdState[h] = nil end
    end
end

return SmartAI
