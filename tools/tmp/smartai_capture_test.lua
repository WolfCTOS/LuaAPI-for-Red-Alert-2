-- Harness for SmartAI capture-aware valuables guard (MVP).
-- Stubs houses/units/engine; drives SmartAI.Update over scripted frames.
-- Verifies: no-threat preservation, type-specific threat response,
-- pullback toward own group, order-on-transition only (no churn),
-- owner-flip observation without decisions, match-restart reset, and
-- existing flank-rally regression.
-- T10-T12: ownership-authority regression (mirror-country): distinct
-- house OBJECTS share one GetName(); only the true aiHouse may be
-- commanded. Covers player-side/allied/other-AI same-country REJECT,
-- capture flip, fresh spawn ALLOW, dead-unit silence, Special exclusion.
--
-- Run: buildlua_check.exe tools/tmp/smartai_capture_test.lua
-- (from the repo root; package.path below resolves framework.util
-- the same way the in-game loader does for smart_ai/main.lua)

package.path = "scripts/?.lua;" .. package.path

local LOG = {}
local CURRENT_FRAME = 0

local Engine = {
    PrintMessage = function(msg) LOG[#LOG + 1] = tostring(msg) end,
}

local HOUSES = {}
local WORLD = {}
local nextId = 100

local function mkHouse(name, isHuman)
    local h = {
        _name = name, _human = isHuman, _allies = {},
        GetName = function(self) return self._name end,
        IsHuman = function(self) return self._human end,
        IsAlliedWith = function(self, other)
            local on = type(other) == "table" and other._name or tostring(other)
            return self._allies[on] == true
        end,
    }
    return h
end

function mkUnit(ownerName, typeName, kind, x, y, idle)
    nextId = nextId + 1
    local u = {
        _id = nextId, _owner = ownerName, _type = typeName,
        _kind = kind or "unit", _pos = { x = x, y = y },
        _alive = true, _idle = (idle == nil) and true or idle,
        _hp = 400, _maxhp = 400, moves = {}, hunts = 0,
        IsAlive = function(self) return self._alive end,
        GetOwner = function(self) return HOUSES[self._owner] end,
        GetKind = function(self) return self._kind end,
        GetTypeName = function(self) return self._type end,
        GetHealth = function(self) return self._hp end,
        GetMaxHealth = function(self) return self._maxhp end,
        GetPosition = function(self) return self._pos end,
        GetId = function(self) return self._id end,
        IsIdle = function(self) return self._idle end,
        GetDistanceTo = function(self, other)
            local p = other:GetPosition()
            local dx, dy = self._pos.x - p.x, self._pos.y - p.y
            return math.sqrt(dx * dx + dy * dy)
        end,
        MoveTo = function(self, x, y)
            self.moves[#self.moves + 1] = { x = x, y = y, frame = CURRENT_FRAME }
            return true
        end,
        Hunt = function(self) self.hunts = self.hunts + 1 end,
    }
    WORLD[#WORLD + 1] = u
    return u
end

function mkBuilding(ownerName, typeName, x, y, hpFrac)
    local b = mkUnit(ownerName, typeName, "building", x, y, true)
    b._hp = math.floor(b._maxhp * (hpFrac or 1.0))
    return b
end

HOUSES.P = mkHouse("P", true)
HOUSES.A = mkHouse("A", false)
HOUSES.Y = mkHouse("Y", false)
local HOUSE_LIST = { HOUSES.P, HOUSES.A, HOUSES.Y }

local House = {
    GetPlayer = function() return HOUSES.P end,
    GetCount = function() return #HOUSE_LIST end,
    GetByIndex = function(i) return HOUSE_LIST[i + 1] end,
}

local World = {
    GetUnits = function()
        local out = {}
        for _, u in ipairs(WORLD) do
            if u:IsAlive() and u:GetKind() ~= "building" then out[#out + 1] = u end
        end
        return out
    end,
    GetBuildings = function()
        local out = {}
        for _, u in ipairs(WORLD) do
            if u:IsAlive() and u:GetKind() == "building" then out[#out + 1] = u end
        end
        return out
    end,
}

_G.Engine, _G.House, _G.World = Engine, House, World

local SAI = dofile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/smart_ai/main.lua")

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
    SAI.Update(frame)
end

local function runTo(target)
    for f = CURRENT_FRAME + 1, target do step(f) end
end

local function kill(u) u._alive = false end

-- Roster: A owns lone APOC + 2 group tanks; Y owns a MIND far away;
-- P owns nothing relevant. Rhino = non-threat control.
local apoc = mkUnit("A", "APOC", "unit", 100, 100, true)
local g1 = mkUnit("A", "HTNK", "unit", 140, 140, true)
local g2 = mkUnit("A", "HTNK", "unit", 142, 140, true)
local mind = mkUnit("Y", "MIND", "unit", 300, 300, true)
local rhino = mkUnit("A", "HTNK", "unit", 110, 105, true)

-- T1: no threat in range -> no guard orders (behavior preserved).
runTo(30)
T(#apoc.moves == 0, "T1 no threat: lone APOC gets no pullback")
T(logCount("capture risk") == 0, "T1: no threat message")

-- T2: MIND within 9 of lone APOC -> pullback toward own centroid.
-- (Placed to threaten ONLY the APOC: the HTNK at (110,105) must stay out.)
mind._pos = { x = 93, y = 95 } -- dist to APOC ~8.6, to HTNK ~19.7
runTo(60)
T(#apoc.moves == 1, "T2 threat: exactly one pullback order")
T(apoc.moves[1].x == 123 and apoc.moves[1].y == 121,
    "T2: pullback destination is own centroid (123,121)")
T(logCount("capture risk") == 1, "T2: threat message logged once")

-- T2b: churn — 300 more frames, no duplicate orders.
runTo(360)
T(#apoc.moves == 1, "T2b: no order churn across 10 scans")

-- T3: APOC rejoins group (<=4 of centroid) with MIND still near -> no order.
apoc._pos = { x = 133, y = 132 }
runTo(390)
T(#apoc.moves == 1, "T3 with-group APOC: no new order despite nearby MIND")

-- T4: non-threat enemy near lone APOC -> no order (type-specificity).
mind._pos = { x = 300, y = 300 }
apoc._pos = { x = 100, y = 100 }
local foe = mkUnit("Y", "HTNK", "unit", 102, 102, true) -- dist ~2.8, not capturer
runTo(420)
T(#apoc.moves == 1, "T4 Rhino at 2.8 cells: no pullback (not a capturer)")
kill(foe)

-- T5: owner flip observed as observation only.
runTo(449)
rhino._owner = "Y"
runTo(450)
T(logCount("Ownership change observed") == 1, "T5: flip logged as observation")
T(#rhino.moves == 0, "T5: flip drives no orders by itself")
rhino._owner = "A" -- restore (flip back also logs; not asserted)

-- T6: match restart clears guard state -> same threat re-orders.
mind._pos = { x = 93, y = 95 } -- threat back near the lone APOC only
step(5) -- backwards frame = restart
runTo(30)
T(#apoc.moves == 2, "T6 restart: cleared state re-orders on same threat")

-- T7: existing flank-rally regression (damaged A building + idle far tank).
local fact = mkBuilding("A", "NAWEAP", 500, 500, 1.0)
local res = mkUnit("A", "HTNK", "unit", 100, 400, true)
runTo(59)
fact._hp = 100 -- <85%: breach
runTo(60)
T(res.hunts == 1, "T7 rally intact: idle far tank ordered to Hunt")
T(#res.moves == 1, "T7 rally intact: MoveTo issued")

-- T8: engaged valuable (not idle) near MIND -> never yanked.
fact._hp = 1000 -- heal: silence the rally path for threat-isolation below
local apoc2 = mkUnit("A", "APOC", "unit", 600, 600, false)
local g3 = mkUnit("A", "HTNK", "unit", 700, 700, true)
local mind2 = mkUnit("Y", "MIND", "unit", 604, 604, true)
runTo(90)
T(#apoc2.moves == 0, "T8 engaged APOC near MIND: no yank order")

-- T9: threat far outside the envelope -> no orders regardless of value.
local g3moves = #g3.moves
runTo(120)
T(#g3.moves == g3moves, "T9 MIND at 135 cells: no orders outside envelope")

-- T10-T12: ownership-authority regression (mirror-country).
-- Distinct house OBJECTS share GetName() "Russians". The old
-- name-based filter commanded all of them; identity must isolate.
HOUSES.AM = mkHouse("Russians", false)  -- mirror AI
HOUSES.PM = mkHouse("Russians", true)   -- player-side, same country (human: never an aiHouse)
HOUSES.AM2 = mkHouse("Russians", false) -- second AI, same country
HOUSES.ALR = mkHouse("Russians", false) -- player-allied, same country
HOUSES.ALR._allies["P"] = true
HOUSES.SP = mkHouse("Special", false)   -- non-combatant: must stay excluded
HOUSE_LIST[#HOUSE_LIST + 1] = HOUSES.AM
HOUSE_LIST[#HOUSE_LIST + 1] = HOUSES.PM
HOUSE_LIST[#HOUSE_LIST + 1] = HOUSES.AM2
HOUSE_LIST[#HOUSE_LIST + 1] = HOUSES.ALR
HOUSE_LIST[#HOUSE_LIST + 1] = HOUSES.SP

local amFact = mkBuilding("AM", "NAWEAP", 800, 800, 1.0)
local amTank = mkUnit("AM", "LTNK", "unit", 100, 100, true)
local pmTank = mkUnit("PM", "LTNK", "unit", 120, 100, true)
local am2Tank = mkUnit("AM2", "LTNK", "unit", 140, 100, true)
local alrTank = mkUnit("ALR", "LTNK", "unit", 160, 100, true)
local yTank = mkUnit("Y", "LTNK", "unit", 180, 100, true)
local spFact = mkBuilding("SP", "NAWEAP", 900, 900, 1.0)
local spTank = mkUnit("SP", "LTNK", "unit", 950, 900, true)

-- T10: AM breach -> only the true AM unit is commanded.
runTo(149)
amFact._hp = 100 -- breach (<85% of 400)
spFact._hp = 100 -- breach for Special (must be ignored: not an aiHouse)
runTo(150)
T(#amTank.moves == 1 and amTank.hunts == 1, "T10 AI-owned unit: ALLOW (MoveTo+Hunt)")
T(#pmTank.moves == 0 and pmTank.hunts == 0, "T10 same-country player-side unit: REJECT")
T(#am2Tank.moves == 0 and am2Tank.hunts == 0, "T10 same-country other-AI unit: REJECT")
T(#alrTank.moves == 0 and alrTank.hunts == 0, "T10 same-country allied unit: REJECT")
T(#yTank.moves == 0 and yTank.hunts == 0, "T10 enemy-of-AI unit: REJECT as member")
T(#spTank.moves == 0 and spTank.hunts == 0, "T10 Special-house unit: REJECT (non-combatant)")

-- T10b: reverse — AM2 breach commands only AM2's own unit.
amFact._hp = 1000 -- heal AM breach
local am2Fact = mkBuilding("AM2", "NAWEAP", 820, 820, 1.0)
runTo(179)
am2Fact._hp = 100
runTo(180)
T(#am2Tank.moves == 1 and am2Tank.hunts == 1, "T10b second AI controls only its own unit")
T(#amTank.moves == 1, "T10b first AI unit not re-ordered by second AI breach")
T(#pmTank.moves == 0, "T10b player-side unit still uncontrolled")

-- T11: capture flip + fresh spawn under an active AM breach.
amTank._owner = "PM" -- captured by player-side: follows current GetOwner
local amTank2 = mkUnit("AM", "LTNK", "unit", 110, 110, true) -- fresh AI spawn: ALLOW
am2Fact._hp = 1000 -- silence AM2
amFact._hp = 100 -- AM breach again
runTo(210)
T(#amTank.moves == 1, "T11 captured unit follows new owner: no new AI orders")
T(#amTank2.moves == 1 and amTank2.hunts == 1, "T11 newly spawned AI unit: ALLOW")

-- T12: dead units stay silent across scans with an active breach.
kill(amTank2)
local deadMoves = #amTank2.moves
runTo(240)
T(#amTank2.moves == deadMoves, "T12 dead unit receives no further orders")
T(#amTank.moves == 1 and #pmTank.moves == 0, "T12 roster stable across scan with dead member")

print(string.format("\n%d passed, %d failed%s", passed, failed,
    failed == 0 and " - ALL CHECKS PASSED" or ""))
if failed > 0 then os.exit(1) end
