-- Harness for SmartAI Commander + Officers (escort + garrison-screen).
-- Stubs houses/units/engine; drives SmartAI.Update over scripted frames.
-- Verifies: V3 escort assignment (nearest idle, exclusions), engaged
-- units never yanked, order-on-transition only (no churn), V3-death
-- release, garrison directives + cooldown, match-restart reset, and a
-- flank-rally integration smoke.
--
-- Run: buildlua_check.exe tools/tmp/smartai_officer_test.lua
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
local HOUSE_LIST = { HOUSES.P, HOUSES.A }

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

local function step(frame)
    CURRENT_FRAME = frame
    SAI.Update(frame)
end

local function runTo(target)
    for f = CURRENT_FRAME + 1, target do step(f) end
end

local function kill(u) u._alive = false end
local function movesTo(u, x, y)
    for _, m in ipairs(u.moves) do
        if m.x == x and m.y == y then return true end
    end
    return false
end

-- T1 roster: A owns a V3 + mixed company; P owns nothing relevant.
local v3  = mkUnit("A", "V3", "unit", 50, 50, true)
local h1  = mkUnit("A", "HTNK", "unit", 52, 52, true)
local h2  = mkUnit("A", "HTNK", "unit", 60, 60, true)
local h3  = mkUnit("A", "HTNK", "unit", 51, 51, false) -- engaged: never yank
local sm1 = mkUnit("A", "SMIN", "unit", 53, 53, true)  -- harvester: excluded
local mcv = mkUnit("A", "SMCV", "unit", 54, 54, true)  -- mcv: excluded
local v3b = mkUnit("A", "V3", "unit", 100, 100, true)  -- 2nd V3: no guards left

runTo(30) -- first full scan (SCAN_INTERVAL)

T(movesTo(h1, 50, 50), "T1 escort nearest guard h1 -> V3")
T(movesTo(h2, 50, 50), "T1 escort second guard h2 -> V3")
T(#h3.moves == 0, "T1 engaged h3 never yanked")
T(#sm1.moves == 0, "T1 harvester excluded from escort")
T(#mcv.moves == 0, "T1 MCV excluded from escort")
T(#v3.moves == 0 and #v3b.moves == 0, "T1 artillery never ordered itself")
local movesAfterT1 = #h1.moves + #h2.moves

-- T3 churn: no new orders before refresh caps.
runTo(59)
T(#h1.moves + #h2.moves == movesAfterT1, "T3 no orders between scans")
runTo(60)
T(#h1.moves + #h2.moves == movesAfterT1, "T3 no churn at next scan (ESCORT_EVERY)")

-- T4: both V3s die -> state dropped silently, guards get no new orders.
kill(v3); kill(v3b)
runTo(90)
T(#h1.moves + #h2.moves == movesAfterT1, "T4 V3 death releases guards, no orders")

-- T5 garrison: idle infantry near NAPILL ordered on; far/engaged ignored.
local b1 = mkBuilding("A", "NAPILL", 20, 20, 1.0)
local i1 = mkUnit("A", "E1", "infantry", 22, 22, true)
local i2 = mkUnit("A", "E1", "infantry", 21, 23, true)
local i3 = mkUnit("A", "E1", "infantry", 60, 60, true)  -- out of radius
local i4 = mkUnit("A", "E1", "infantry", 21, 21, false) -- engaged
runTo(120)
T(movesTo(i1, 20, 20), "T5 garrison i1 -> NAPILL")
T(movesTo(i2, 20, 20), "T5 garrison i2 -> NAPILL")
T(#i3.moves == 0, "T5 far infantry ignored")
T(#i4.moves == 0, "T5 engaged infantry never yanked")
local garrisonMoves = #i1.moves + #i2.moves
runTo(150)
T(#i1.moves + #i2.moves == garrisonMoves, "T5 garrison cooldown blocks re-issue")

-- T6 restart: frame backwards clears officer state; fresh V3 re-escorted.
step(5)
local v3c = mkUnit("A", "V3", "unit", 70, 70, true)
local h9 = mkUnit("A", "HTNK", "unit", 72, 72, true)
runTo(35)
T(movesTo(h9, 70, 70), "T6 post-restart escort works")

-- T7 rally integration smoke: damaged AI building rallies idle reserve.
local depot = mkBuilding("A", "NAWEAP", 10, 10, 0.5)
local res = mkUnit("A", "HTNK", "unit", 90, 90, true)
runTo(65)
T(movesTo(res, 10, 10) and res.hunts > 0, "T7 rally intact alongside officers")

-- T8 Commander<->Officer: breach appears next to an escorted V3.
depot._hp = depot._maxhp -- repair old breach
local v8 = mkUnit("A", "V3", "unit", 30, 30, true)
local g8a = mkUnit("A", "HTNK", "unit", 31, 31, true)
local g8b = mkUnit("A", "HTNK", "unit", 33, 33, true)
runTo(90) -- scan: no breach yet -> escort assigns
T(movesTo(g8a, 30, 30) and movesTo(g8b, 30, 30), "T8 escort assigned before breach")
local b2 = mkBuilding("A", "NAWEAP", 32, 32, 0.5) -- breach near v8
runTo(120) -- scan: Officer assignments stand off the rally
T(not movesTo(g8a, 32, 32) and not movesTo(g8b, 32, 32),
    "T8 O->C: escort guards skipped by rally")
T(movesTo(h1, 32, 32), "T8 unassigned reserves still rally")
runTo(150) -- scan: breach blackboard fresh -> escort stands down + logs
local released = false
for _, line in ipairs(LOG) do
    if string.find(line, "escort released", 1, true) then released = true end
end
T(released, "T8 C->O: escort released on breach priority")
b2._hp = b2._maxhp -- old breach repaired; new far breach:
local b3 = mkBuilding("A", "NAWEAP", 200, 200, 0.5)
runTo(180) -- scan: released guards are rally-eligible again
T(movesTo(g8a, 200, 200) and movesTo(g8b, 200, 200),
    "T8 handoff: released guards rallied to new breach")

-- T9 distant V3 holds escort while its house has a far breach.
local v9 = mkUnit("A", "V3", "unit", 230, 230, true)
local n9a = mkUnit("A", "HTNK", "unit", 231, 231, true)
local n9b = mkUnit("A", "HTNK", "unit", 232, 232, true)
runTo(210) -- scan: escort assigns far from breach; rally skips them
T(movesTo(n9a, 230, 230) and movesTo(n9b, 230, 230), "T9 distant V3 escorted")
T(not movesTo(n9a, 32, 32) and not movesTo(n9b, 32, 32),
    "T9 O->C: fresh guards skipped by same-scan rally")

print(string.format("officer harness: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
