-- Headless harness for Bounty Hunter v2 (Gate 2B).
-- Stubs Engine/House/World, drives BountyHunter.Update over a scripted
-- timeline, asserts: candidate filtering, weighted selection, mark
-- create/clear, moving target, capture-void, destruction payout, reward
-- math, reselection, match reset. Draw pixels (DrawRect/DrawText) are C++
-- side and NOT asserted here — the harness asserts the Mark/Clear triggers.
--
-- Run: lua_check.exe tools/tmp/bounty_hunter_test.lua

local LOG = {}
local CURRENT_FRAME = 0
local MARKS = {}   -- unitId -> {color, dur}
local CLEARS = {}  -- unitId -> count
local GLOBAL_CLEARS = 0

local Engine = {
    PrintMessage = function(msg) LOG[#LOG + 1] = tostring(msg) end,
    ClearBountyMarks = function() GLOBAL_CLEARS = GLOBAL_CLEARS + 1 end,
}

local KEYS_DOWN = {}
local Input = {
    WasKeyPressed = function(code) return KEYS_DOWN[code] == true end,
}

local HOUSES = {}
local HOUSE_LIST = {}
local WORLD = {}
local NEXT_ID = 1000

local function mkHouse(name, isHuman)
    local h = {
        _name = name, _human = isHuman, _allies = {}, _credits = 0,
        GetName = function(self) return self._name end,
        IsHuman = function(self) return self._human end,
        IsAlliedWith = function(self, other)
            local n = other
            if type(other) == "table" and other.GetName then
                n = other:GetName()
            end
            return self._allies[n] == true
        end,
        AddCredits = function(self, amount) self._credits = self._credits + amount end,
        GetCredits = function(self) return self._credits end,
    }
    return h
end

local function mkUnit(ownerName, typeName, kind, vet, cost, x, y)
    NEXT_ID = NEXT_ID + 1
    local u = {
        _id = NEXT_ID, _owner = ownerName, _type = typeName,
        _kind = kind or "unit", _vet = vet or "rookie",
        _cost = cost or 700, _pos = { x = x or 0, y = y or 0 },
        _alive = true, _markOk = true,
        IsAlive = function(self) return self._alive end,
        GetOwner = function(self) return HOUSES[self._owner] end,
        GetKind = function(self) return self._kind end,
        GetHealth = function(self) return 400 end,
        GetMaxHealth = function(self) return 400 end,
        GetPosition = function(self) return self._pos end,
        GetTypeName = function(self) return self._type end,
        GetId = function(self) return self._id end,
        GetVeterancy = function(self) return self._vet end,
        GetCost = function(self) return self._cost end,
        MarkBounty = function(self, color, dur)
            if not self._markOk then return false end
            MARKS[self._id] = { color = color, dur = dur }
            return true
        end,
        ClearBountyMark = function(self)
            CLEARS[self._id] = (CLEARS[self._id] or 0) + 1
            MARKS[self._id] = nil
        end,
    }
    return u
end

HOUSES.P = mkHouse("P", true)
HOUSES.E = mkHouse("E", false)
HOUSES.N = mkHouse("Neutral", false)
HOUSE_LIST = { HOUSES.P, HOUSES.E, HOUSES.N }

local House = {
    GetPlayer = function() return HOUSES.P end,
    GetCount = function() return #HOUSE_LIST end,
    GetByIndex = function(i) return HOUSE_LIST[i + 1] end,
}

local World = {
    GetUnits = function()
        local out = {}
        for _, u in ipairs(WORLD) do
            if u:IsAlive() then out[#out + 1] = u end
        end
        return out
    end,
}

_G.Engine, _G.House, _G.World, _G.Input = Engine, House, World, Input

local BH = dofile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/bounty_hunter/main.lua")
local TUN = BH.TUNING
local S = BH._S

-- Deterministic RNG script (values in [0,1)).
local RAND_SCRIPT = {}
local function scriptRandom() return table.remove(RAND_SCRIPT, 1) or 0.0 end
math.random = scriptRandom

local passed, failed = 0, 0
local function T(cond, name)
    if cond then passed = passed + 1; print("PASS " .. name)
    else failed = failed + 1; print("FAIL " .. name) end
end

local function logCount(sub)
    local n = 0
    for _, line in ipairs(LOG) do
        if string.find(line, sub, 1, true) then n = n + 1 end
    end
    return n
end

local function step(frame)
    CURRENT_FRAME = frame
    BH.Update(frame)
end

local function runTo(target)
    for f = CURRENT_FRAME + 1, target do step(f) end
end

local function addUnit(owner, typeName, kind, vet, cost, x, y)
    local u = mkUnit(owner, typeName, kind, vet, cost, x, y)
    WORLD[#WORLD + 1] = u
    return u
end

local function kill(u) u._alive = false end

-- Reset mod state via the match-restart path, then wipe the world.
local function freshMatch()
    step(0) -- backwards frame => restart guard (unless already at 0)
    if CURRENT_FRAME ~= 0 then
        CURRENT_FRAME = 0
        BH.Update(0)
    end
    WORLD = {}
    MARKS = {}
    CLEARS = {}
    LOG = {}
end

-- ---------------------------------------------------------------------------
-- 0. Tuning sanity (design contract Elite > Veteran > Rookie)
-- ---------------------------------------------------------------------------
T(TUN.WEIGHT.elite > TUN.WEIGHT.veteran and TUN.WEIGHT.veteran > TUN.WEIGHT.rookie,
    "weights elite>veteran>rookie")
T(TUN.WEIGHT.rookie == 10 and TUN.WEIGHT.veteran == 25 and TUN.WEIGHT.elite == 40,
    "weights are 10/25/40")
T(TUN.MULT.rookie == 1.5 and TUN.MULT.veteran == 1.75 and TUN.MULT.elite == 2.0,
    "multipliers are 1.5/1.75/2.0")

-- ---------------------------------------------------------------------------
-- 1. No candidates: only player units -> no target, no crash
-- ---------------------------------------------------------------------------
freshMatch()
addUnit("P", "HTNK", "unit", "rookie", 700, 10, 10)
runTo(TUN.FIRST_SELECT + 100)
T(S.target == nil, "no candidates -> no target")
T(logCount("WANTED") == 0, "no candidates -> no announcement")

-- ---------------------------------------------------------------------------
-- 2. Forced elite pick + exact reward math (cost 900 -> 1800)
-- ---------------------------------------------------------------------------
freshMatch()
addUnit("P", "HTNK", "unit", "rookie", 700, 5, 5)
local r1 = addUnit("E", "HTNK", "unit", "rookie", 900, 50, 50)
local v1 = addUnit("E", "APOC", "unit", "veteran", 900, 60, 60)
local e1 = addUnit("E", "APOC", "unit", "elite", 900, 70, 70)
-- id order: r1 < v1 < e1; weights 10/25/40 total 75.
-- elite segment: [35,75)/75 = [0.4667,1). Force r=0.9.
RAND_SCRIPT = { 0.9 }
runTo(TUN.FIRST_SELECT + 10)
T(S.target ~= nil and S.target.vet == "elite", "forced roll picks elite")
T(S.target ~= nil and S.target.reward == 1800, "elite reward = floor(900*2.0) = 1800")
T(MARKS[e1._id] ~= nil and MARKS[e1._id].color == TUN.COLOR, "mark created on elite with gold color")
T(logCount("WANTED: APOC (elite, E)") == 1, "WANTED announcement")

-- ---------------------------------------------------------------------------
-- 3. Moving target stays live, pos snapshot follows
-- ---------------------------------------------------------------------------
e1._pos = { x = 71, y = 72 }
runTo(CURRENT_FRAME + TUN.SCAN_EVERY)
T(S.target ~= nil and S.target.id == e1._id, "moving target retained")
T(S.target.pos.x == 71 and S.target.pos.y == 72, "pos snapshot follows unit")

-- ---------------------------------------------------------------------------
-- 4. Destruction -> payout to nearest hostile (player) + CLAIMED
-- ---------------------------------------------------------------------------
local pup = addUnit("P", "HTNK", "unit", "rookie", 700, 72, 73) -- closest to e1
kill(e1)
runTo(CURRENT_FRAME + TUN.SCAN_EVERY)
T(S.target == nil, "target cleared after kill")
T(HOUSES.P._credits == 1800, "player credited exactly 1800")
T(logCount("CLAIMED: APOC (elite)") == 1, "CLAIMED announcement")
T(pup ~= nil, "killer unit untouched")

-- ---------------------------------------------------------------------------
-- 5. New target after kill cooldown (forced rookie -> 1350 on 900)
-- ---------------------------------------------------------------------------
RAND_SCRIPT = { 0.05 } -- rookie segment [0,10)/75
runTo(CURRENT_FRAME + TUN.COOLDOWN_AFTER_KILL + 10)
T(S.target ~= nil and S.target.vet == "rookie", "reselect after cooldown (rookie)")
T(S.target.reward == 1350, "rookie reward = floor(900*1.5) = 1350")
local rTarget = S.target.id

-- ---------------------------------------------------------------------------
-- 6. Capture (owner change) voids without reward
-- ---------------------------------------------------------------------------
local before = HOUSES.E._credits + HOUSES.P._credits
local capUnit = nil
for _, u in ipairs(WORLD) do
    if u:IsAlive() and u:GetId() == rTarget then capUnit = u end
end
T(capUnit ~= nil, "captured unit resolved")
capUnit._owner = "P"
runTo(CURRENT_FRAME + TUN.SCAN_EVERY)
T(S.target == nil, "capture clears target")
T(HOUSES.E._credits + HOUSES.P._credits == before, "capture pays nothing")
T(logCount("bounty void") == 1, "void announcement")

-- ---------------------------------------------------------------------------
-- 7. Filters: infantry / civilians / allies / self never candidates
-- ---------------------------------------------------------------------------
freshMatch()
addUnit("P", "HTNK", "unit", "elite", 2000, 5, 5)      -- self, elite, pricey
addUnit("E", "E1", "infantry", "elite", 200, 50, 50)   -- enemy infantry
addUnit("N", "HTNK", "unit", "elite", 2000, 55, 55)    -- civilian car-ish
RAND_SCRIPT = { 0.99 }
runTo(TUN.FIRST_SELECT + 10)
T(S.target == nil, "infantry/civilian/self excluded -> no target")

-- ---------------------------------------------------------------------------
-- 8. Mark failure -> no target
-- ---------------------------------------------------------------------------
freshMatch()
local stubborn = addUnit("E", "HTNK", "unit", "rookie", 700, 50, 50)
stubborn._markOk = false
RAND_SCRIPT = { 0.0 }
runTo(TUN.FIRST_SELECT + 10)
T(S.target == nil, "mark failure -> no bounty")

-- ---------------------------------------------------------------------------
-- 9. Veteran exact math: floor(900*1.75) = 1575
-- ---------------------------------------------------------------------------
freshMatch()
addUnit("P", "HTNK", "unit", "rookie", 700, 5, 5)
local vv = addUnit("E", "APOC", "unit", "veteran", 900, 60, 60)
RAND_SCRIPT = { 0.5 } -- only candidate, any roll wins
runTo(TUN.FIRST_SELECT + 10)
T(S.target ~= nil and S.target.vet == "veteran", "single veteran selected")
T(S.target.reward == 1575, "veteran reward = floor(900*1.75) = 1575")
T(vv._id == S.target.id, "target id matches")

-- ---------------------------------------------------------------------------
-- 10. Match reset clears state + global marks
-- ---------------------------------------------------------------------------
local gc0 = GLOBAL_CLEARS
runTo(CURRENT_FRAME + 500)
step(10) -- backwards => restart
T(S.target == nil, "reset clears target")
T(GLOBAL_CLEARS == gc0 + 1, "reset calls Engine.ClearBountyMarks")
runTo(TUN.FIRST_SELECT + 1200 + 10)
T(true, "post-reset match runs without errors")

-- ---------------------------------------------------------------------------
-- 11. Test A diagnostic: VISUAL_ENABLED=false -> full Lua lifecycle,
--     zero native registrations, payout still works
-- ---------------------------------------------------------------------------
freshMatch()
BH.TUNING.VISUAL_ENABLED = false
addUnit("P", "HTNK", "unit", "rookie", 700, 5, 5)
local nv = addUnit("E", "APOC", "unit", "veteran", 900, 60, 60)
addUnit("P", "HTNK", "unit", "rookie", 700, 61, 61) -- killer nearby
RAND_SCRIPT = { 0.5 }
runTo(TUN.FIRST_SELECT + 10)
T(S.target ~= nil, "visual-off: selection still happens")
T(next(MARKS) == nil, "visual-off: zero native registrations")
kill(nv)
local creditsBefore = HOUSES.P._credits
runTo(CURRENT_FRAME + TUN.SCAN_EVERY)
T(S.target == nil, "visual-off: destruction detected")
T(HOUSES.P._credits - creditsBefore == 1575, "visual-off: payout still works")
BH.TUNING.VISUAL_ENABLED = true

print(string.format("BOUNTY TESTS: %d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
