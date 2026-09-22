-- Bounty Hunter v2 (Gate 2B): single active Bounty Target with visual mark.
--
-- WHAT IT DOES
--   Every SELECT_EVERY frames the mod picks ONE enemy vehicle as the bounty
--   target, draws a rectangle + "BOUNTY" label on it (C++ DrawAsVXL detour,
--   draw-only), and pays a veterancy-scaled reward to the killer's house when
--   the target dies. Then a cooldown, then the next target.
--
-- SELECTION (controlled cycle, one roll per cycle — never per frame):
--   candidates = enemy (of the player) mobile UnitClass vehicles, combat
--   houses only (Neutral/Special excluded), alive.
--   weight: rookie 10 / veteran 25 / elite 40  →  Elite > Veteran > Rookie.
--   total = sum(weights); r = random() * total; id-sorted walk, first to
--   drive r < 0 wins. RNG seeded once with a fixed constant so selections
--   are reproducible in the harness (documented, not a sim input).
--
-- REWARD (from live GetCost, never hardcoded prices):
--   rookie  = floor(cost * 1.50)
--   veteran = floor(cost * 1.75)
--   elite   = floor(cost * 2.00)
--   Paid via house:AddCredits to the nearest hostile house at the victim's
--   last known position (Command Authority attribution pattern); fallback is
--   the player house when hostile to the victim, else no payout.
--
-- LIFECYCLE (OnUnitDestroyed is never dispatched — ID-diff polling instead):
--   target tracked by UniqueID; each SCAN_EVERY frames the id is re-resolved
--   in World.GetUnits(). Absent → destroyed → payout → clear → cooldown.
--   Owner change (capture) → cleared WITHOUT reward. Match restart
--   (frame < lastFrame) → full reset + Engine.ClearBountyMarks().
--
-- SCOPE: UnitClass vehicles/ships only — the draw detour covers exactly this
-- path (infantry/buildings/aircraft return false from MarkBounty and are
-- excluded from candidates). No combo system (future phase).

local BountyHunter = {}

local TUNING = {
    FIRST_SELECT      = 20 * 60, -- first selection delay (frames)
    SELECT_EVERY      = 30 * 60, -- idle reselection period (frames)
    COOLDOWN_AFTER_KILL = 15 * 60, -- pause after a payout (frames)
    COOLDOWN_INVALID  = 10 * 60, -- pause after invalid/capture clear (frames)
    SCAN_EVERY        = 15,      -- target-watch cadence (frames)
    COLOR             = 0x00FF00, -- overlay color: green = money (COLORREF)
    VISUAL_ENABLED    = true,    -- Test A diagnostic: false skips MarkBounty
                                 -- registration (full Lua lifecycle otherwise)
    RNG_SEED          = 1337,    -- fixed seed: reproducible selections

    WEIGHT = { rookie = 10, veteran = 25, elite = 40 },
    MULT   = { rookie = 1.50, veteran = 1.75, elite = 2.00 },
}
BountyHunter.TUNING = TUNING

local S = {
    lastFrame = 0,
    nextSelect = TUNING.FIRST_SELECT,
    playerName = nil,
    target = nil, -- {id,type,vet,mult,cost,reward,ownerName,pos}
    seeded = false,
}
BountyHunter._S = S -- harness read access (tests read, never write)

local NON_COMBATANT = { Neutral = true, Special = true }

local function say(msg) Engine.PrintMessage("[BOUNTY] " .. msg) end

local function houseNameOf(h)
    if not h then return nil end
    local ok, name = pcall(h.GetName, h)
    if ok and type(name) == "string" and name ~= "" then return name end
    return nil
end

local function isCombatName(name)
    return name ~= nil and not NON_COMBATANT[name]
end

local function playerHouse() return House.GetPlayer() end

local function areAllied(nameA, nameB)
    if nameA == nameB then return true end
    if not isCombatName(nameA) or not isCombatName(nameB) then return false end
    -- Resolve objects via index scan (no house-object cache kept).
    local ha, hb = nil, nil
    local n = House.GetCount()
    for i = 0, n - 1 do
        local h = House.GetByIndex(i)
        local nm = houseNameOf(h)
        if nm == nameA then ha = h end
        if nm == nameB then hb = h end
    end
    if not ha or not hb then return false end
    local ok, res = pcall(ha.IsAlliedWith, ha, hb)
    return ok and res == true
end

local function vetOf(u)
    local ok, v = pcall(u.GetVeterancy, u)
    if ok and (v == "veteran" or v == "elite" or v == "rookie") then return v end
    return "rookie"
end

local function costOf(u)
    local ok, c = pcall(u.GetCost, u)
    if ok and type(c) == "number" and c > 0 then return math.floor(c) end
    return 0
end

-- Nearest hostile UNIT's owner object to (x, y). Deterministic: first in
-- scan order wins ties. Returns house object or nil.
local function nearestHostileOwner(victimName, x, y)
    local units = World.GetUnits()
    if not units then return nil end
    local best, bestD2 = nil, math.huge
    for _, u in ipairs(units) do
        if u and u:IsAlive() then
            local owner = u:GetOwner()
            local oname = houseNameOf(owner)
            if oname and isCombatName(oname)
                and oname ~= victimName
                and not areAllied(oname, victimName) then
                local p = u:GetPosition()
                if p and p.x and p.y then
                    local dx, dy = p.x - x, p.y - y
                    local d2 = dx * dx + dy * dy
                    if d2 < bestD2 then best, bestD2 = owner, d2 end
                end
            end
        end
    end
    return best
end

local function collectCandidates()
    local out = {}
    local units = World.GetUnits()
    if not units then return out end
    for _, u in ipairs(units) do
        if u and u:IsAlive() and u:GetKind() == "unit" then
            local oname = houseNameOf(u:GetOwner())
            if oname and isCombatName(oname)
                and oname ~= S.playerName
                and not areAllied(oname, S.playerName) then
                local vet = vetOf(u)
                out[#out + 1] = {
                    obj = u,
                    id = u:GetId(),
                    type = u:GetTypeName(),
                    vet = vet,
                    weight = TUNING.WEIGHT[vet] or TUNING.WEIGHT.rookie,
                    cost = costOf(u),
                }
            end
        end
    end
    table.sort(out, function(a, b) return a.id < b.id end)
    return out
end

local function creditHouse(hobj, amount)
    if not hobj or amount <= 0 then return false end
    local ok = pcall(hobj.AddCredits, hobj, amount)
    return ok == true
end

local function findById(id)
    local units = World.GetUnits()
    if not units then return nil end
    for _, u in ipairs(units) do
        if u and u:IsAlive() then
            local ok, uid = pcall(u.GetId, u)
            if ok and uid == id then return u end
        end
    end
    return nil
end

local function clearTarget(silent)
    local t = S.target
    S.target = nil
    if t then
        local u = findById(t.id)
        if u then pcall(u.ClearBountyMark, u) end
        if not silent then say("mark cleared.") end
    end
end

local function trySelect(frame)
    local cands = collectCandidates()
    if #cands == 0 then
        S.nextSelect = frame + TUNING.SELECT_EVERY
        return false
    end
    local total = 0
    for _, c in ipairs(cands) do total = total + c.weight end
    local r = math.random() * total
    local pick = cands[#cands]
    for _, c in ipairs(cands) do
        r = r - c.weight
        if r < 0 then pick = c; break end
    end

    -- The visual mark is the point (Test A diagnostic: VISUAL_ENABLED=false
    -- skips registration but keeps full selection/reward/lifecycle).
    -- NOTE: pcall returns true even when MarkBounty itself returns false,
    -- so both the call status AND the binding result are checked.
    local marked = true
    if TUNING.VISUAL_ENABLED then
        local okCall, okMark = pcall(pick.obj.MarkBounty, pick.obj, TUNING.COLOR)
        marked = okCall and okMark
    end
    if not marked then
        S.nextSelect = frame + TUNING.SELECT_EVERY
        return false
    end

    local mult = TUNING.MULT[pick.vet] or TUNING.MULT.rookie
    local reward = math.floor(pick.cost * mult)
    local p = pick.obj:GetPosition()
    S.target = {
        id = pick.id, type = pick.type, vet = pick.vet,
        mult = mult, cost = pick.cost, reward = reward,
        ownerName = houseNameOf(pick.obj:GetOwner()),
        pos = (p and p.x) and { x = p.x, y = p.y } or nil,
    }
    say(string.format("WANTED: %s (%s, %s) — $%d. Destroy it!",
        pick.type, pick.vet, S.target.ownerName or "?", reward))
    return true
end

-- Returns true while the target is still live on the map.
local function watchTarget(frame)
    local t = S.target
    if not t then return false end
    local u = findById(t.id)
    if u then
        -- Capture/ownership change ends the bounty without reward.
        local oname = houseNameOf(u:GetOwner())
        if oname ~= t.ownerName then
            pcall(u.ClearBountyMark, u)
            S.target = nil
            S.nextSelect = frame + TUNING.COOLDOWN_INVALID
            say(string.format("%s changed hands — bounty void.", t.type))
            return false
        end
        local p = u:GetPosition()
        if p and p.x then t.pos = { x = p.x, y = p.y } end
        return true
    end

    -- Absent from the roster: destroyed → payout.
    S.target = nil
    local paid = "no claimant"
    if t.pos then
        local killer = nearestHostileOwner(t.ownerName, t.pos.x, t.pos.y)
        if killer then
            if creditHouse(killer, t.reward) then
                paid = (houseNameOf(killer) or "?") .. " +" .. "$" .. t.reward
            end
        else
            local ph = playerHouse()
            if ph and houseNameOf(ph) ~= t.ownerName
                and not areAllied(houseNameOf(ph), t.ownerName) then
                if creditHouse(ph, t.reward) then
                    paid = (houseNameOf(ph) or "?") .. " +" .. "$" .. t.reward
                end
            end
        end
    end
    say(string.format("CLAIMED: %s (%s) — %s.", t.type, t.vet, paid))
    S.nextSelect = frame + TUNING.COOLDOWN_AFTER_KILL
    return false
end

function BountyHunter.Update(frame)
    if not S.seeded then
        math.randomseed(TUNING.RNG_SEED)
        S.seeded = true
    end
    if not S.playerName then
        local pname = houseNameOf(playerHouse())
        if pname then
            S.playerName = pname
            S.nextSelect = frame + TUNING.FIRST_SELECT
            say("hunter active. Targets marked soon.")
        else
            return
        end
    end

    -- Match restart: frame counter went backwards.
    if frame < S.lastFrame then
        clearTarget(true)
        if Engine.ClearBountyMarks then
            pcall(Engine.ClearBountyMarks)
        end
        S.playerName = nil
        S.nextSelect = TUNING.FIRST_SELECT
        S.lastFrame = frame
        return
    end
    S.lastFrame = frame

    if S.target then
        if frame % TUNING.SCAN_EVERY == 0 then
            watchTarget(frame)
        end
    elseif frame >= S.nextSelect then
        if trySelect(frame) then
            -- nextSelect stays past: reselection happens after clear.
            S.nextSelect = frame + TUNING.SELECT_EVERY
        end
    end
end

return BountyHunter
