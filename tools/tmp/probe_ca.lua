-- Probe 2: EXACT harness main-scenario replication, dump after think 1440.
local LOG = {}
local KEYS_PRESSED = {}
local CURRENT_FRAME = 0

local Engine = { PrintMessage = function(msg) LOG[#LOG + 1] = tostring(msg) end }

local HOUSES = {}
local HOUSE_LIST = {}
local WORLD = {}

function mkUnit(ownerName, typeName, maxhp, x, y)
    local id = #WORLD + 1000
    local u = {
        _id = id, _owner = ownerName, _type = typeName,
        _maxhp = maxhp, _hp = maxhp, _pos = { x = x, y = y },
        _alive = true, _kind = "ground", disabledAt = nil,
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

local function mkHouse(name, isHuman)
    return {
        _name = name, _human = isHuman, _allies = {}, spawns = {},
        GetName = function(self) return self._name end,
        IsHuman = function(self) return self._human end,
        IsAlliedWith = function(self, other) return self._allies[tostring(other)] == true end,
        SpawnUnit = function(self, typeId, count, x, y)
            for _ = 1, (count or 1) do
                WORLD[#WORLD + 1] = mkUnit(self._name, typeId or "HTNK", 400, x or 0, y or 0)
            end
            self.spawns[#self.spawns + 1] = { type = typeId, x = x, y = y }
            return count or 1
        end,
    }
end

HOUSES.P = mkHouse("P", true)
HOUSES.A = mkHouse("A", false)
HOUSE_LIST = { HOUSES.P, HOUSES.A }

_G.Engine = Engine
_G.House = {
    GetPlayer = function() return HOUSES.P end,
    GetCount = function() return #HOUSE_LIST end,
    GetByIndex = function(i) return HOUSE_LIST[i + 1] end,
}
_G.World = { GetUnits = function()
    local out = {}
    for _, u in ipairs(WORLD) do if u:IsAlive() then out[#out + 1] = u end end
    return out
end }
_G.Input = { WasKeyPressed = function(code) return KEYS_PRESSED[code] == true end }

local AUTH = dofile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/command_authority/main.lua")
AUTH.TUNING.CP_SURVIVE_PAY = 0

local function step(f) CURRENT_FRAME = f; AUTH.Update(f); KEYS_PRESSED = {} end
local function runTo(t) for f = CURRENT_FRAME + 1, t do step(f) end end
local function addUnit(o, t, hp, x, y) local u = mkUnit(o, t, hp, x, y); WORLD[#WORLD + 1] = u; return u end

-- exact harness main scenario
runTo(500)
local p1 = addUnit("P", "HTNK", 900, 100, 100)
local p2 = addUnit("P", "GI", 400, 110, 100)
local p3 = addUnit("P", "GI", 400, 105, 105)
local p4 = addUnit("P", "GI", 400, 108, 102)
local p5 = addUnit("P", "GI", 400, 112, 98)
local a1 = addUnit("A", "APOC", 800, 200, 100)
local a2 = addUnit("A", "GI", 400, 210, 110)
local a3 = addUnit("A", "APOC", 1000, 300, 100)
step(0)

runTo(6); a2._alive = false; runTo(15)
runTo(21); a1._hp = 400; runTo(37); a1._hp = 50; runTo(45)

runTo(51); p2._alive = false; runTo(60)
runTo(1211); p3._alive = false; runTo(1215)

print("a1 hp right before 1440:", a1._hp, "/", a1._maxhp)
print(string.format("PROBE frame=%d a1hp=%d | cpP=%s cpA=%s | nextThinkA=%s | announced=%s",
    CURRENT_FRAME, a1._hp, tostring(AUTH._S.cp.P), tostring(AUTH._S.cp.A),
    tostring(AUTH._S.nextThink.A), tostring(AUTH._S.announced)))
runTo(1440)
print("a1 hp after 1440:", a1._hp)
print(string.format("POST frame=%d cpP=%s cpA=%s | nextThinkA=%s",
    CURRENT_FRAME, tostring(AUTH._S.cp.P), tostring(AUTH._S.cp.A),
    tostring(AUTH._S.nextThink.A)))
for i, sp in ipairs(HOUSES.A.spawns) do
    print(string.format("A spawn[%d] %s @(%d,%d)", i, sp.type, sp.x, sp.y))
end
print("=== log ===")
for _, l in ipairs(LOG) do print(l) end
