-- Placement fix verification: lastKillPos stays the primary frontline source,
-- but a victim cell inside enemy eco/army is displaced ~8 cells toward own
-- forces (existing force=true + terrain spiral handle the rest); if the
-- displaced point is still contested, the own-force centroid is used.
-- Supplements (does NOT modify) command_authority_test.lua and
-- command_authority_neutral_test.lua.
--
-- Stub surface copied from the existing harnesses; adds buildings
-- (World.GetBuildings) and Neutral/Special houses.
--
-- Verifies:
--   S1. safe lastKillPos -> spawn exactly there ("at the front").
--   S2. enemy harvester+refinery at the point -> displaced toward own forces,
--       farther from eco ("at the front, adjusted").
--   S3a. 3+ enemy combat units within 5 (army, no eco) -> displaced.
--   S3b. 2 enemy units within 5 + parked civilian car -> NOT displaced
--        (normal contested frontline preserved; civilians don't contest).
--   S4. displaced point still contested -> own centroid ("with your forces").
--   S5. symmetric Human case: player Z-reinforce at AI eco -> displaced
--       toward player forces, charged normally.
--
-- Frame discipline (same as existing harnesses): mutations/keys only on
-- frames == 7 (mod 15); cleanup kills + settle scan between scenarios;
-- step(0) restarts (ledgers wiped, WORLD persists with dead filtered).
--
-- Run: buildlua_check.exe tools/tmp/command_authority_frontline_test.lua

local LOG = {}
local KEYS_PRESSED = {}
local CURRENT_FRAME = 0

local Engine = {
    PrintMessage = function(msg) LOG[#LOG + 1] = tostring(msg) end,
}

local HOUSES = {}
local HOUSE_LIST = {}
local WORLD = {}

local function mkHouse(name, isHuman)
    local h = {
        _name = name,
        _human = isHuman,
        _allies = {},
        spawns = {},
        GetName = function(self) return self._name end,
        IsHuman = function(self) return self._human end,
        IsAlliedWith = function(self, other)
            return self._allies[tostring(other)] == true
        end,
        SpawnUnit = function(self, typeId, count, x, y)
            for _ = 1, (count or 1) do
                WORLD[#WORLD + 1] = mkUnit(self._name, typeId or "HTNK", 400, x or 0, y or 0)
            end
            self.spawns[#self.spawns + 1] = { type = typeId, x = x, y = y }
            return count or 1
        end,
    }
    return h
end

function mkUnit(ownerName, typeName, maxhp, x, y)
    local id = #WORLD + 1000
    local u = {
        _id = id, _owner = ownerName, _type = typeName,
        _maxhp = maxhp, _hp = maxhp, _pos = { x = x, y = y },
        _alive = true, _kind = "unit", disabledAt = nil,
        IsAlive = function(self) return self._alive end,
        GetOwner = function(self) return HOUSES[self._owner] end,
        GetKind = function(self) return self._kind end,
        GetHealth = function(self) return self._hp end,
        GetMaxHealth = function(self) return self._maxhp end,
        SetHealthRatio = function(self, r) self._hp = math.floor(self._maxhp * r + 0.5) end,
        Disable = function(self, _frames) self.disabledAt = CURRENT_FRAME end,
        GetPosition = function(self) return self._pos end,
        GetTypeName = function(self) return self._type end,
        GetId = function(self) return self._id end,
    }
    return u
end

function mkBuilding(ownerName, typeName, x, y)
    local id = #WORLD + 5000
    local b = {
        _id = id, _owner = ownerName, _type = typeName,
        _pos = { x = x, y = y }, _alive = true,
        IsAlive = function(self) return self._alive end,
        GetOwner = function(self) return HOUSES[self._owner] end,
        GetKind = function(self) return "building" end,
        GetHealth = function(self) return 1000 end,
        GetMaxHealth = function(self) return 1000 end,
        GetPosition = function(self) return self._pos end,
        GetTypeName = function(self) return self._type end,
        GetId = function(self) return self._id end,
    }
    WORLD[#WORLD + 1] = b
    return b
end

HOUSES.P = mkHouse("P", true)
HOUSES.A = mkHouse("A", false)
HOUSES.Neutral = mkHouse("Neutral", false)
HOUSES.Special = mkHouse("Special", false)
HOUSE_LIST = { HOUSES.P, HOUSES.A, HOUSES.Neutral, HOUSES.Special }

local House = {
    GetPlayer = function() return HOUSES.P end,
    GetCount = function() return #HOUSE_LIST end,
    GetByIndex = function(i) return HOUSE_LIST[i + 1] end,
}

local function liveList()
    local out = {}
    for _, u in ipairs(WORLD) do
        if u:IsAlive() then out[#out + 1] = u end
    end
    return out
end

local World = {
    GetUnits = function()
        local out = {}
        for _, u in ipairs(liveList()) do
            if u:GetKind() ~= "building" then out[#out + 1] = u end
        end
        return out
    end,
    GetBuildings = function()
        local out = {}
        for _, u in ipairs(liveList()) do
            if u:GetKind() == "building" then out[#out + 1] = u end
        end
        return out
    end,
}

local Input = {
    WasKeyPressed = function(code) return KEYS_PRESSED[code] == true end,
}

_G.Engine, _G.House, _G.World, _G.Input = Engine, House, World, Input

local AUTH = dofile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/command_authority/main.lua")

local passed, failed = 0, 0
local function T(cond, name)
    if cond then
        passed = passed + 1
        print("PASS " .. name)
    else
        failed = failed + 1
        print("FAIL " .. name)
    end
end

local function logCount(sub)
    local n = 0
    for _, line in ipairs(LOG) do
        if string.find(line, sub, 1, true) then n = n + 1 end
    end
    return n
end

local function press(code) KEYS_PRESSED[code] = true end

local function step(frame)
    CURRENT_FRAME = frame
    AUTH.Update(frame)
    KEYS_PRESSED = {}
end

local function runTo(target)
    for f = CURRENT_FRAME + 1, target do step(f) end
end

local function addUnit(owner, typeName, maxhp, x, y)
    local u = mkUnit(owner, typeName, maxhp, x, y)
    WORLD[#WORLD + 1] = u
    return u
end

local function kill(u) u._alive = false end
local function cpOf(name) return AUTH._S.cp[name] end

-- Kill every live object (pre-restart cleanup; credits hit the old ledger,
-- which the restart wipes), then settle one scan.
local function clearLive()
    for _, u in ipairs(liveList()) do kill(u) end
end

local function dist(ax, ay, bx, by)
    local dx, dy = ax - bx, ay - by
    return math.sqrt(dx * dx + dy * dy)
end

-- ---------------------------------------------------------------------------
-- S1: safe lastKillPos -> exact point, "at the front"
-- ---------------------------------------------------------------------------

local ax1 = addUnit("A", "APOC", 800, 500, 500)
local v1 = addUnit("P", "GI", 400, 290, 290)
local v2 = addUnit("P", "GI", 400, 300, 300)

step(0)
runTo(6); kill(v1); runTo(21); kill(v2); runTo(30)
T(cpOf("A") == 10, "S1 setup: A fed to 10 CP")
local s0 = #HOUSES.A.spawns
local adj0 = logCount("at the front, adjusted")
runTo(300) -- think at 241: repair no-op (ax1 full HP) -> reinforce
T(#HOUSES.A.spawns == s0 + 1, "S1: director reinforced once")
T(HOUSES.A.spawns[s0 + 1].x == 300 and HOUSES.A.spawns[s0 + 1].y == 300,
    "S1 safe lastKillPos: spawn exactly at (300,300)")
T(logCount("at the front, adjusted") == adj0, "S1: no adjustment on safe point")

runTo(306); clearLive(); runTo(322)

-- ---------------------------------------------------------------------------
-- S2: enemy harvester + refinery at the point -> displaced toward own forces
-- Expected: eco centroid (202,200), own (210,210),
--   displaced = (205,206), message "at the front, adjusted".
-- ---------------------------------------------------------------------------

local ax2 = addUnit("A", "APOC", 800, 210, 210)
local w1 = addUnit("P", "GI", 400, 190, 190)
local w2 = addUnit("P", "GI", 400, 200, 200)
local ph = addUnit("P", "SMIN", 400, 200, 200)
mkBuilding("P", "YAREFN", 204, 200)

step(0)
runTo(6); kill(w1); runTo(21); kill(w2); runTo(30)
T(cpOf("A") == 10, "S2 setup: A fed to 10 CP")
T(cpOf("Neutral") == nil, "S2 setup: Neutral still ledger-free")
s0 = #HOUSES.A.spawns
adj0 = logCount("at the front, adjusted")
runTo(300)
T(#HOUSES.A.spawns == s0 + 1, "S2: director reinforced once")
local sp = HOUSES.A.spawns[s0 + 1]
T(sp.x == 205 and sp.y == 206, "S2 eco contact: displaced to (205,206), not stacked")
T(dist(sp.x, sp.y, 200, 200) > dist(200, 200, 200, 200),
    "S2: spawn farther from the harvester than the victim cell was")
T(logCount("at the front, adjusted") == adj0 + 1, "S2: adjusted-front message")

runTo(306); clearLive(); runTo(322)

-- ---------------------------------------------------------------------------
-- S3a: enemy army (3 combat units, no eco) -> displaced to (406,406)
-- ---------------------------------------------------------------------------

local ax3 = addUnit("A", "APOC", 800, 600, 600)
local u1 = addUnit("P", "GI", 400, 390, 390)
local u2 = addUnit("P", "GI", 400, 400, 400)
local e1 = addUnit("P", "LTNK", 400, 401, 401)
local e2 = addUnit("P", "LTNK", 400, 402, 400)
local e3 = addUnit("P", "LTNK", 400, 400, 402)

step(0)
runTo(6); kill(u1); runTo(21); kill(u2); runTo(30)
T(cpOf("A") == 10, "S3a setup: A fed to 10 CP")
s0 = #HOUSES.A.spawns
adj0 = logCount("at the front, adjusted")
runTo(300)
T(#HOUSES.A.spawns == s0 + 1, "S3a: director reinforced once")
sp = HOUSES.A.spawns[s0 + 1]
T(sp.x == 406 and sp.y == 406, "S3a army contact: displaced to (406,406)")
T(logCount("at the front, adjusted") == adj0 + 1, "S3a: adjusted-front message")

runTo(306); clearLive(); runTo(322)

-- ---------------------------------------------------------------------------
-- S3b: 2 enemies + parked civilian car -> NOT displaced (normal frontline)
-- ---------------------------------------------------------------------------

local ax4 = addUnit("A", "APOC", 800, 600, 600)
local q1 = addUnit("P", "GI", 400, 490, 490)
local q2 = addUnit("P", "GI", 400, 500, 500)
local f1 = addUnit("P", "LTNK", 400, 502, 501)
local f2 = addUnit("P", "LTNK", 400, 501, 503)
local nc = addUnit("Neutral", "CAR", 100, 500, 500) -- parked civilians don't contest

step(0)
runTo(6); kill(q1); runTo(21); kill(q2); runTo(30)
T(cpOf("A") == 10, "S3b setup: A fed to 10 CP")
s0 = #HOUSES.A.spawns
adj0 = logCount("at the front, adjusted")
runTo(300)
T(#HOUSES.A.spawns == s0 + 1, "S3b: director reinforced once")
sp = HOUSES.A.spawns[s0 + 1]
T(sp.x == 500 and sp.y == 500, "S3b normal frontline: spawn exactly at (500,500)")
T(logCount("at the front, adjusted") == adj0, "S3b: no adjustment, civilians ignored")
T(cpOf("Neutral") == nil, "S3b: parked car earned nothing")

runTo(306); clearLive(); runTo(322)

-- ---------------------------------------------------------------------------
-- S4: displaced point still contested -> own centroid ("with your forces")
-- Own unit O at (0,0); eco at LP and at the would-be displaced cell (94,94).
-- ---------------------------------------------------------------------------

local oz = addUnit("A", "APOC", 800, 0, 0)
local r1 = addUnit("P", "GI", 400, 90, 90)
local r2 = addUnit("P", "GI", 400, 100, 100)
local hh = addUnit("P", "SMIN", 400, 102, 102)
mkBuilding("P", "YAREFN", 94, 94)

step(0)
runTo(6); kill(r1); runTo(21); kill(r2); runTo(30)
T(cpOf("A") == 10, "S4 setup: A fed to 10 CP")
s0 = #HOUSES.A.spawns
local mf0 = logCount("with your forces")
adj0 = logCount("at the front, adjusted")
runTo(300)
T(#HOUSES.A.spawns == s0 + 1, "S4: director reinforced once")
sp = HOUSES.A.spawns[s0 + 1]
T(sp.x == 0 and sp.y == 0, "S4 double-contested: fell back to centroid (0,0)")
T(logCount("at the front, adjusted") == adj0, "S4: no adjusted spawn happened")
T(logCount("with your forces") == mf0 + 1, "S4: muster-point message")

runTo(306); clearLive(); runTo(322)

-- ---------------------------------------------------------------------------
-- S5: symmetric Human case - player Z at AI eco -> displaced toward player
-- Own centroid (145,145); eco centroid (201.5,200.5);
--   displaced = (194,194), charged 10 CP.
-- ---------------------------------------------------------------------------

local pt5 = addUnit("P", "HTNK", 900, 150, 150)
local p5b = addUnit("P", "GI", 400, 140, 140)
local av1 = addUnit("A", "GI", 400, 195, 195)
local av2 = addUnit("A", "GI", 400, 200, 200)
local ah = addUnit("A", "HARV", 400, 200, 200)
mkBuilding("A", "GAREFN", 203, 201)

step(0)
runTo(6); kill(av1); runTo(21); kill(av2); runTo(30)
T(cpOf("P") == 13, "S5 setup: player fed to 13 CP")
local ps0 = #HOUSES.P.spawns
adj0 = logCount("at the front, adjusted")
runTo(36)
press(0x5A); step(37) -- Z
T(#HOUSES.P.spawns == ps0 + 1, "S5: player reinforced once")
local psp = HOUSES.P.spawns[ps0 + 1]
T(psp.x == 194 and psp.y == 194, "S5 AI eco: displaced to (194,194), not stacked")
T(dist(psp.x, psp.y, 200, 200) > dist(200, 200, 200, 200),
    "S5: spawn farther from the AI harvester than the victim cell was")
T(logCount("at the front, adjusted") == adj0 + 1, "S5: adjusted-front message")
press(0x54); step(39)
T(logCount("CP -10 [reinforce] = 3") == 1, "S5: charged normally (13 -> 3)")

-- ---------------------------------------------------------------------------
-- Summary
-- ---------------------------------------------------------------------------

print(string.format("\n%d passed, %d failed%s", passed, failed,
    failed == 0 and " - ALL CHECKS PASSED" or ""))
if failed > 0 then
    print("--- log tail ---")
    for i = math.max(1, #LOG - 12), #LOG do print(LOG[i]) end
    os.exit(1)
end
