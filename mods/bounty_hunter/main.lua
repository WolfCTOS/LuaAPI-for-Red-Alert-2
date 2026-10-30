-- Bounty Hunter v3: single active Bounty Target with visual mark.
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
--   total = sum(weights); r = rng_next() * total; id-sorted walk, first to
--   drive r < 0 wins. RNG is an isolated xorshift32 (no math.random global
--   pollution); seeded with a fixed constant and mixed with the logical
--   frame at each pick, so all MP clients derive the same sequence.
--
-- REWARD (from live GetCost, never hardcoded prices):
--   rookie  = floor(cost * 1.50)
--   veteran = floor(cost * 1.75)
--   elite   = floor(cost * 2.00)
--   Paid via house:AddCredits to the top damage-attributed house
--   (GetTarget match observed on HP-drop scans); fallback is the nearest
--   hostile house at the victim's last known position. No player-house bias:
--   the player is paid only when actually attributed or nearest. No
--   claimant (empty map / all-allied) → no payout.
--   The killer UNIT itself gets a gold "+$<reward>" caption for
--   REWARD_MARK_FRAMES (per-unit votes tracked alongside house scores;
--   most ticks wins, lowest id breaks ties). Best-effort: skipped when
--   visuals are off or the killer is gone/non-Unit.
--
-- ATTRIBUTION (OnUnitDestroyed is never dispatched — ID-diff polling instead):
--   target HP is sampled each SCAN_EVERY tick. On an HP drop, every alive
--   unit whose GetTarget() resolves to the bounty id votes once for its
--   owner house (scores[house] += 1). At death the top scorer wins; ties
--   break alphabetically for determinism. GetTarget() is engine Target
--   (may lag one tick behind damage); scores accumulate over the whole
--   bounty lifetime so a single missed tick does not lose the killer.
--
-- LIFECYCLE (ID-diff polling):
--   target tracked by UniqueID; each SCAN_EVERY frames ONE World.GetUnits()
--   scan re-resolves the id, samples HP, collects attacker votes and the
--   nearest hostile in the same pass. Absent → destroyed → payout → clear
--   → cooldown. Owner change (capture) → cleared WITHOUT reward.
--   HOLD[vet] expiry → mark cleared WITHOUT reward ("expired — new
--   target"), then rotate cooldown. Lifetime, caption color and reward all
--   lock to the selection-time vet (rookie 30s / veteran 60s / elite 90s).
--   A live target is therefore never held forever. Match restart
--   (frame < lastFrame) → full reset + Engine.ClearBountyMarks().
--
-- SCOPE: UnitClass vehicles/ships only — the DrawAsVXL detour covers exactly
-- this path (infantry/buildings/aircraft return false from MarkBounty).
-- ALLOW_KINDS gates candidates in Lua so the C++ limit is enforced before
-- any mark call; widening scope needs new native draw detours, not a Lua
-- flag flip. No combo system (future phase).

local BountyHunter = {}

local TUNING = {
    FIRST_SELECT      = 20 * 60, -- first selection delay (frames)
    SELECT_EVERY      = 30 * 60, -- idle reselection period (frames)
    COOLDOWN_AFTER_KILL = 15 * 60, -- pause after a payout (frames)
    COOLDOWN_INVALID  = 10 * 60, -- pause after invalid/capture clear (frames)
    COOLDOWN_ROTATE   = 10 * 60, -- pause after lifetime expiry (frames)
    SCAN_EVERY        = 15,      -- target-watch cadence (frames)
    HOUSE_CACHE_EVERY = 60,      -- house/alliance cache refresh (frames)
    COLOR             = 0x00FF00, -- overlay color: green = money (COLORREF)
    -- Rank colors ("heat" scale, NOT faction colors): rookie keeps the
    -- original green; higher ranks read hotter. Black outline (C++) keeps
    -- all three readable on any terrain.
    VET_COLOR = { rookie = 0x00FF00, veteran = 0x0099FF, elite = 0x0000FF },
    -- Bounty lifetime per rank (frames). Higher rank = longer hunt.
    -- Easy tuning: change these three numbers only; everything (expiry,
    -- timer caption) derives from t.hold, fixed at selection from t.vet.
    HOLD = { rookie = 30 * 60, veteran = 60 * 60, elite = 90 * 60 },
    REWARD_COLOR      = 0xFFD700, -- killer reward mark: gold = earned (COLORREF)
    REWARD_MARK_FRAMES = 5 * 60, -- reward caption lifetime on the killer (frames)
    VISUAL_ENABLED    = true,    -- Test A diagnostic: false skips MarkBounty
                                 -- registration (full Lua lifecycle otherwise)
    RNG_SEED          = 1337,    -- xorshift seed: reproducible selections

    WEIGHT = { rookie = 10, veteran = 25, elite = 40 },
    MULT   = { rookie = 1.50, veteran = 1.75, elite = 2.00 },
    ALLOW_KINDS = { unit = true }, -- visual scope: DrawAsVXL path only
}
BountyHunter.TUNING = TUNING

local S = {
    lastFrame = 0,
    nextSelect = TUNING.FIRST_SELECT,
    playerName = nil,
    target = nil, -- {id,type,vet,mult,cost,reward,ownerName,pos,since,hold,shownSec,color,lastHp,scores,unitHits,unitHouse}
    rng_state = TUNING.RNG_SEED,
    selectCount = 0,
    houseCache = { atFrame = -1000000, names = {}, allied = {} },
}
BountyHunter._S = S -- harness read access (tests read, never write)
BountyHunter._TEST_ROLL = nil -- harness hook: when set to [0,1), trySelect
                               -- uses it instead of rng_next() (gameplay: nil)

local NON_COMBATANT = { Neutral = true, Special = true }

local function say(msg) Engine.PrintMessage("[BOUNTY] " .. msg) end

-- Isolated deterministic RNG (xorshift32). Never touches math.random, so
-- other mods cannot perturb the bounty sequence and vice versa.
local function rng_next()
    local x = S.rng_state & 0xFFFFFFFF
    if x == 0 then x = 0x9E3779B9 end
    x = (x ~ ((x << 13) & 0xFFFFFFFF)) & 0xFFFFFFFF
    x = (x ~ ((x >> 17) & 0xFFFFFFFF)) & 0xFFFFFFFF
    x = (x ~ ((x << 5) & 0xFFFFFFFF)) & 0xFFFFFFFF
    if x == 0 then x = 0x9E3779B9 end
    S.rng_state = x
    return x / 4294967296.0
end

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

-- House/alliance cache: one House.GetCount/GetByIndex sweep per
-- HOUSE_CACHE_EVERY frames instead of per-unit lookups (old O(n*h) path).
local function refreshHouses(frame)
    local c = S.houseCache
    if frame - c.atFrame < TUNING.HOUSE_CACHE_EVERY and c.atFrame >= 0 then
        return c
    end
    local names, objs = {}, {}
    local okN, n = pcall(House.GetCount)
    if okN and type(n) == "number" and n > 0 then
        for i = 0, n - 1 do
            local okH, h = pcall(House.GetByIndex, i)
            if okH and h then
                local nm = houseNameOf(h)
                if nm then
                    names[#names + 1] = nm
                    objs[nm] = h
                end
            end
        end
    end
    local allied = {}
    for _, a in ipairs(names) do
        for _, b in ipairs(names) do
            if a == b then
                allied[a .. "|" .. b] = true
            elseif isCombatName(a) and isCombatName(b) and objs[a] and objs[b] then
                local ok, res = pcall(objs[a].IsAlliedWith, objs[a], objs[b])
                allied[a .. "|" .. b] = (ok and res == true)
            else
                allied[a .. "|" .. b] = false
            end
        end
    end
    S.houseCache = { atFrame = frame, names = names, allied = allied }
    return S.houseCache
end

local function areAlliedCached(nameA, nameB)
    if nameA == nameB then return true end
    if not isCombatName(nameA) or not isCombatName(nameB) then return false end
    local v = S.houseCache.allied[nameA .. "|" .. nameB]
    if v ~= nil then return v end
    return false
end

local function resolveHouseByName(name)
    if not name then return nil end
    local okN, n = pcall(House.GetCount)
    if not okN or type(n) ~= "number" then return nil end
    for i = 0, n - 1 do
        local okH, h = pcall(House.GetByIndex, i)
        if okH and h and houseNameOf(h) == name then return h end
    end
    return nil
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

local function healthOf(u)
    local ok, hp = pcall(u.GetHealth, u)
    if ok and type(hp) == "number" and hp >= 0 then return math.floor(hp) end
    return nil
end

local function kindOf(u)
    local ok, k = pcall(u.GetKind, u)
    if ok and type(k) == "string" then return k end
    return nil
end

local function idOf(u)
    local ok, uid = pcall(u.GetId, u)
    if ok and type(uid) == "number" then return uid end
    return nil
end

local function posOf(u)
    local ok, p = pcall(u.GetPosition, u)
    if ok and type(p) == "table" and type(p.x) == "number" and type(p.y) == "number" then
        return { x = p.x, y = p.y }
    end
    return nil
end

local function targetIdOf(u)
    local okT, tgt = pcall(u.GetTarget, u)
    if not okT or tgt == nil then return nil end
    return idOf(tgt)
end

local function collectCandidates(units)
    local out = {}
    units = units or World.GetUnits()
    if not units then return out end
    for _, u in ipairs(units) do
        if u then
            local aliveOk, alive = pcall(u.IsAlive, u)
            if aliveOk and alive == true and TUNING.ALLOW_KINDS[kindOf(u)] then
                local ownerOk, owner = pcall(u.GetOwner, u)
                local oname = (ownerOk and owner) and houseNameOf(owner) or nil
                if oname and isCombatName(oname)
                    and oname ~= S.playerName
                    and not areAlliedCached(oname, S.playerName) then
                    local vet = vetOf(u)
                    out[#out + 1] = {
                        obj = u,
                        id = idOf(u),
                        type = u:GetTypeName(),
                        vet = vet,
                        weight = TUNING.WEIGHT[vet] or TUNING.WEIGHT.rookie,
                        cost = costOf(u),
                    }
                end
            end
        end
    end
    table.sort(out, function(a, b) return (a.id or 0) < (b.id or 0) end)
    return out
end

local function creditHouseByName(name, amount)
    if not name or amount <= 0 then return false end
    local h = resolveHouseByName(name)
    if not h then return false end
    local ok = pcall(h.AddCredits, h, amount)
    return ok == true
end

-- BEFORE caption: "BOUNTY $<reward> / <Ss>", single line (the C++ text
-- API has no multiline). Built only from frozen selection state: reward,
-- and remaining seconds derived from (hold, since) — the same pair the
-- expiry check uses, so caption and expiration cannot disagree.
local function bountyLabel(reward, remainSec)
    return string.format("BOUNTY $%d / %ds",
        math.floor(reward or 0), math.max(0, math.floor(remainSec or 0)))
end

local function topScorer(scores)
    local best, bestHits = nil, 0
    for name, hits in pairs(scores or {}) do
        if hits > bestHits or (hits == bestHits and best ~= nil and name < best) then
            best, bestHits = name, hits
        elseif best == nil and hits > 0 then
            best, bestHits = name, hits
        end
    end
    return best
end

-- Live unit lookup by UniqueID (single World scan; nil when gone).
-- Never caches userdata: engine objects die within frames.
local function findById(id)
    if not id then return nil end
    local units = World.GetUnits()
    if not units then return nil end
    for _, u in ipairs(units) do
        if u and idOf(u) == id then
            local aliveOk, alive = pcall(u.IsAlive, u)
            if aliveOk and alive == true then return u end
            return nil
        end
    end
    return nil
end

-- Top damage-dealing UNIT id: most observed ticks, lowest id wins ties
-- (deterministic across MP clients). Returns nil when nobody voted.
local function topKillerUnit(t)
    local best, bestHits = nil, 0
    for uid, hits in pairs(t.unitHits or {}) do
        if hits > bestHits or (hits == bestHits and (best == nil or uid < best)) then
            best, bestHits = uid, hits
        end
    end
    return best
end

-- Reward caption on the killer: gold "+$<reward>" overlay for
-- REWARD_MARK_FRAMES, so the earner sees exactly what the kill paid.
-- Best-effort and silent: skipped when visuals are off, the killer is
-- gone, or the kind has no draw path (non-Unit MarkBounty returns false).
local function markKillerReward(frame, t)
    if not TUNING.VISUAL_ENABLED then return false end
    local kuid = topKillerUnit(t)
    if not kuid then return false end
    local killer = findById(kuid)
    if not killer then return false end
    local caption = "+$" .. tostring(t.reward or 0)
    local okCall, okMark = pcall(killer.MarkBounty, killer,
        TUNING.REWARD_COLOR, TUNING.REWARD_MARK_FRAMES, caption)
    return okCall and okMark == true
end

local function clearTargetMark(t)
    if not t then return end
    -- Re-resolve live to avoid touching a stale pointer: scan by id once.
    local units = World.GetUnits()
    if units then
        for _, u in ipairs(units) do
            if u and idOf(u) == t.id then
                pcall(u.ClearBountyMark, u)
                break
            end
        end
    end
end

local function clearTarget(silent)
    local t = S.target
    S.target = nil
    if t then
        clearTargetMark(t)
        if not silent then say("mark cleared.") end
    end
end

local function trySelect(frame)
    local units = World.GetUnits()
    local cands = collectCandidates(units)
    if #cands == 0 then
        S.nextSelect = frame + TUNING.SELECT_EVERY
        return false
    end
    -- Deterministic per-pick mix: same logical frame + same roster =
    -- same pick on every MP client, without touching global math.random.
    S.rng_state = (S.rng_state ~ (frame & 0xFFFFFFFF)) & 0xFFFFFFFF
    if S.rng_state == 0 then S.rng_state = TUNING.RNG_SEED end
    local total = 0
    for _, c in ipairs(cands) do total = total + c.weight end
    local roll = BountyHunter._TEST_ROLL
    if type(roll) ~= "number" or roll < 0 or roll >= 1 then
        roll = rng_next()
    end
    local r = roll * total
    local pick = cands[#cands]
    for _, c in ipairs(cands) do
        r = r - c.weight
        if r < 0 then pick = c; break end
    end

    -- The visual mark is the point (Test A diagnostic: VISUAL_ENABLED=false
    -- skips registration but keeps full selection/reward/lifecycle).
    -- NOTE: pcall returns true even when MarkBounty itself returns false,
    -- so both the call status AND the binding result are checked.
    local mult = TUNING.MULT[pick.vet] or TUNING.MULT.rookie
    local reward = math.floor(pick.cost * mult)
    -- Rank-locked presentation: color + lifetime derive from the SAME
    -- frozen pick.vet as the reward (never re-read live veterancy).
    local color = TUNING.VET_COLOR[pick.vet] or TUNING.COLOR
    local hold = TUNING.HOLD[pick.vet] or TUNING.HOLD.rookie
    local marked = true
    if TUNING.VISUAL_ENABLED then
        local okCall, okMark = pcall(pick.obj.MarkBounty, pick.obj,
            color, 0, bountyLabel(reward, math.floor(hold / 60)))
        marked = okCall and okMark
    end
    if not marked then
        S.nextSelect = frame + TUNING.SELECT_EVERY
        return false
    end

    local p = posOf(pick.obj)
    local ownerOk, owner = pcall(pick.obj.GetOwner, pick.obj)
    S.selectCount = S.selectCount + 1
    S.target = {
        id = pick.id, type = pick.type, vet = pick.vet,
        mult = mult, cost = pick.cost, reward = reward,
        ownerName = (ownerOk and owner) and houseNameOf(owner) or nil,
        pos = p,
        since = frame,
        hold = hold,
        shownSec = math.floor(hold / 60),
        color = color,
        lastHp = healthOf(pick.obj),
        scores = {},
        unitHits = {}, -- attacker UniqueID -> observed damage ticks
        unitHouse = {}, -- attacker UniqueID -> owner house name
    }
    say(string.format("WANTED: %s (%s, %s) — $%d. Destroy it!",
        pick.type, pick.vet, S.target.ownerName or "?", reward))
    return true
end

-- Single-pass watch: one World.GetUnits() scan resolves the target, samples
-- HP, collects attacker votes (HP dropped) and the nearest hostile for the
-- death fallback. Returns true while the target is still live.
local function watchTarget(frame)
    local t = S.target
    if not t then return false end
    local units = World.GetUnits()
    if not units then return true end -- no data: keep target, retry next tick

    local found = nil
    local nearestName, nearestD2 = nil, math.huge
    for _, u in ipairs(units) do
        if u then
            local aliveOk, alive = pcall(u.IsAlive, u)
            if aliveOk and alive == true then
                local uid = idOf(u)
                if uid == t.id then found = u end
                -- Nearest-hostile bookkeeping for the death fallback.
                local ownerOk, owner = pcall(u.GetOwner, u)
                local oname = (ownerOk and owner) and houseNameOf(owner) or nil
                if oname and isCombatName(oname) and t.ownerName
                    and oname ~= t.ownerName
                    and not areAlliedCached(oname, t.ownerName)
                    and t.pos then
                    local p = posOf(u)
                    if p then
                        local dx, dy = p.x - t.pos.x, p.y - t.pos.y
                        local d2 = dx * dx + dy * dy
                        if d2 < nearestD2 then nearestD2, nearestName = d2, oname end
                    end
                end
            end
        end
    end

    if found then
        -- Capture/ownership change ends the bounty without reward.
        local ownerOk, owner = pcall(found.GetOwner, found)
        local oname = (ownerOk and owner) and houseNameOf(owner) or nil
        if oname ~= t.ownerName then
            pcall(found.ClearBountyMark, found)
            S.target = nil
            S.nextSelect = frame + TUNING.COOLDOWN_INVALID
            say(string.format("%s changed hands — bounty void.", t.type))
            return false
        end
        local p = posOf(found)
        if p then t.pos = p end

        -- Damage attribution: on HP drop, every unit currently targeting
        -- the bounty votes once for its owner. Votes accumulate; repair
        -- (HP up) is ignored, unknown HP keeps the old baseline.
        local hp = healthOf(found)
        if hp ~= nil and t.lastHp ~= nil and hp < t.lastHp then
            for _, u in ipairs(units) do
                if u then
                    local aliveOk, alive = pcall(u.IsAlive, u)
                    local uid = idOf(u)
                    if aliveOk and alive == true and uid and uid ~= t.id then
                        if targetIdOf(u) == t.id then
                            local oOk, o = pcall(u.GetOwner, u)
                            local an = (oOk and o) and houseNameOf(o) or nil
                            if an and isCombatName(an) and t.ownerName
                                and an ~= t.ownerName
                                and not areAlliedCached(an, t.ownerName) then
                                t.scores[an] = (t.scores[an] or 0) + 1
                                -- Per-unit votes identify the killer OBJECT
                                -- for the reward caption (house scores alone
                                -- cannot point at a unit to mark).
                                t.unitHits[uid] = (t.unitHits[uid] or 0) + 1
                                t.unitHouse[uid] = an
                            end
                        end
                    end
                end
            end
        end
        if hp ~= nil then t.lastHp = hp end

        -- Timer caption refresh: remaining derives from the same
        -- (hold, since) pair as expiry below. Re-marked only when the
        -- displayed second changes (once/sec, never per-frame); dur=0
        -- keeps C++-side infinite, Lua owns expiration as before.
        local hold = t.hold or TUNING.HOLD.rookie
        local remainSec = math.floor((hold - (frame - (t.since or frame))) / 60)
        if remainSec < 0 then remainSec = 0 end
        if TUNING.VISUAL_ENABLED and remainSec ~= (t.shownSec or -1) then
            t.shownSec = remainSec
            pcall(found.MarkBounty, found,
                t.color or TUNING.COLOR, 0, bountyLabel(t.reward, remainSec))
        end

        -- Lifetime expiry: rotate even a live target, never hold forever.
        if frame - (t.since or frame) >= hold then
            pcall(found.ClearBountyMark, found)
            S.target = nil
            S.nextSelect = frame + TUNING.COOLDOWN_ROTATE
            say(string.format("EXPIRED: %s (%s) survived — new target soon.", t.type, t.vet))
            return false
        end
        return true
    end

    -- Absent from the roster: destroyed → payout to the top scorer,
    -- else nearest hostile, else no claimant (never a biased gift).
    -- Plus a gold "+$<reward>" caption on the killer unit itself, so the
    -- earner sees exactly what the kill paid (best-effort, silent).
    S.target = nil
    local killer = topScorer(t.scores)
    local paid = "no claimant"
    if killer and isCombatName(killer) then
        if creditHouseByName(killer, t.reward) then
            paid = killer .. " +$" .. t.reward
        end
    elseif nearestName then
        if creditHouseByName(nearestName, t.reward) then
            paid = nearestName .. " +$" .. t.reward
        end
    end
    markKillerReward(frame, t)
    say(string.format("CLAIMED: %s (%s) — %s.", t.type, t.vet, paid))
    S.nextSelect = frame + TUNING.COOLDOWN_AFTER_KILL
    return false
end

function BountyHunter.Update(frame)
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
        S.rng_state = TUNING.RNG_SEED
        S.selectCount = 0
        S.houseCache = { atFrame = -1000000, names = {}, allied = {} }
        S.lastFrame = frame
        return
    end
    S.lastFrame = frame

    refreshHouses(frame)

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
