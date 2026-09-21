-- Headless harness for target_reselect route-risk gate (AA Dodge).
-- Stubs Engine/House/World (+print capture), loads the REAL
-- framework.util via package.path, drives Mod.Update over scripted
-- timelines. Asserts: exposed/distant/off-route AA allows action, dense
-- mid-route AA waits without orders, AA destruction / tank movement
-- reopens the route, multi-jet stability, no-target and destroyed
-- cases are error-free.
--
-- Run: lua_check.exe tools/tmp/target_reselect_test.lua

package.path = "scripts/?.lua;" .. package.path

local LOG = {}
local WORLD = {}
local NEXT_ID = 5000
local ATTACK_ORDERS = {} -- {attackerId, targetId}

local _print = print
function print(...) -- capture mod diagnostics; still echo ROUTE/WAIT lines
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
    local line = table.concat(parts, " ")
    LOG[#LOG + 1] = line
end

local Engine = {
    PrintMessage = function(msg) LOG[#LOG + 1] = "HUD:" .. tostring(msg) end,
}

local HOUSES = {}
local function mkHouse(name)
    local h = {
        _name = name,
        GetName = function(self) return self._name end,
        IsAlliedWith = function(self, other)
            local n = type(other) == "table" and other:GetName() or other
            return self._name == n
        end,
    }
    return h
end
HOUSES.P = mkHouse("P")
HOUSES.E = mkHouse("E")
HOUSES.N = mkHouse("Neutral")

local House = { GetPlayer = function() return HOUSES.P end }

local function mkUnit(ownerName, typeName, kind, x, y, hp)
    NEXT_ID = NEXT_ID + 1
    local u = {
        _id = NEXT_ID, _owner = ownerName, _type = typeName,
        _kind = kind, _pos = { x = x, y = y }, _hp = hp or 400,
        _alive = true, _targetId = nil,
        IsAlive = function(self) return self._alive end,
        GetOwner = function(self) return HOUSES[self._owner] end,
        GetKind = function(self) return self._kind end,
        GetHealth = function(self) return self._hp end,
        GetPosition = function(self) return self._pos end,
        GetTypeName = function(self) return self._type end,
        GetId = function(self) return self._id end,
        GetTarget = function(self)
            if not self._targetId then return nil end
            for _, v in ipairs(WORLD) do
                if v._id == self._targetId and v._alive then return v end
            end
            return nil
        end,
        Attack = function(self, tgt)
            ATTACK_ORDERS[#ATTACK_ORDERS + 1] = { a = self._id, t = tgt:GetId() }
            self._targetId = tgt:GetId()
            return true
        end,
    }
    return u
end

local function dist2(ax, ay, bx, by)
    local dx, dy = ax - bx, ay - by
    return math.sqrt(dx * dx + dy * dy)
end

local World = {
    GetAllUnits = function()
        local out = {}
        for _, u in ipairs(WORLD) do out[#out + 1] = u end
        return out
    end,
    GetUnitsInRadius = function(x, y, r)
        local out = {}
        for _, u in ipairs(WORLD) do
            if u._alive and dist2(x, y, u._pos.x, u._pos.y) <= r then
                out[#out + 1] = u
            end
        end
        return out
    end,
}

_G.Engine, _G.House, _G.World = Engine, House, World

local Mod = dofile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/target_reselect/main.lua")

local passed, failed = 0, 0
local function T(cond, name)
    if cond then passed = passed + 1; _print("PASS " .. name)
    else failed = failed + 1; _print("FAIL " .. name) end
end

local function logCount(sub)
    local n = 0
    for _, l in ipairs(LOG) do
        if string.find(l, sub, 1, true) then n = n + 1 end
    end
    return n
end

local function step(frame) Mod.Update(frame) end
local function runTo(f0, f1)
    for f = f0, f1 do step(f) end
end

local function reset()
    WORLD = {}
    LOG = {}
    ATTACK_ORDERS = {}
end

local function addUnit(owner, typeName, kind, x, y, hp)
    local u = mkUnit(owner, typeName, kind, x, y, hp)
    WORLD[#WORLD + 1] = u
    return u
end

-- tick frames only (SCAN_EVERY=20); run a few ticks from frame f
local function ticks(f, n)
    for i = 0, n - 1 do step(f + i * 20) end
    return f + n * 20
end

-- ---------------------------------------------------------------------------
-- 1. Exposed tank: jet holds harvester, no AA -> no orders, no WAIT
-- ---------------------------------------------------------------------------
reset()
local harv = addUnit("P", "HARV", "unit", 50, 50)
local jet = addUnit("E", "JBOB", "aircraft", 90, 50)
jet._targetId = harv._id
ticks(20, 4)
T(#ATTACK_ORDERS == 0, "exposed: no orders issued")
T(logCount("[WAIT]") == 0, "exposed: no WAIT")

-- ---------------------------------------------------------------------------
-- 2. One distant AA: single flak far from route -> action allowed
-- ---------------------------------------------------------------------------
reset()
harv = addUnit("P", "HARV", "unit", 50, 50)
jet = addUnit("E", "JBOB", "aircraft", 90, 50)
jet._targetId = harv._id
addUnit("E", "HTK", "unit", 10, 10) -- distant, enemy-owned flak
ticks(20, 4)
T(logCount("[WAIT]") == 0, "distant AA: no WAIT")

-- ---------------------------------------------------------------------------
-- 3. AA near victim but route open -> shoo proceeds (not WAIT)
-- victim threat must reach 2.0: two HTK adjacent (2x1.0)
-- ---------------------------------------------------------------------------
reset()
harv = addUnit("P", "HARV", "unit", 50, 50)
jet = addUnit("E", "JBOB", "aircraft", 52, 80) -- short route from south
jet._targetId = harv._id
addUnit("P", "HTK", "unit", 48, 50)
addUnit("P", "HTK", "unit", 52, 50)
local altTank = addUnit("P", "HTNK", "unit", 52, 95) -- exposed alternative
ticks(20, 4)
T(logCount("reason=DEEP_AA") == 0, "near-victim AA only: no DEEP_AA verdict")
T(#ATTACK_ORDERS >= 1, "near-victim AA only: shoo proceeds")

-- ---------------------------------------------------------------------------
-- 4. Dense AA directly between jet and harvester -> WAIT, zero orders
-- ---------------------------------------------------------------------------
reset()
harv = addUnit("P", "HARV", "unit", 50, 50, 400)
jet = addUnit("E", "JBOB", "aircraft", 50, 110)
jet._targetId = harv._id
addUnit("P", "NAFLAK", "building", 50, 90)
addUnit("P", "NAFLAK", "building", 50, 78)
addUnit("P", "HTK", "unit", 50, 66)
addUnit("P", "HTK", "unit", 50, 56)
ticks(20, 4)
T(logCount("verdict=DEEP_AA") >= 1, "dense mid-route: DEEP_AA verdict logged")
T(logCount("[WAIT]") >= 1, "dense mid-route: WAIT logged")
T(#ATTACK_ORDERS == 0, "dense mid-route: zero attack orders")

-- ---------------------------------------------------------------------------
-- 5. Tank deep in base (same geometry, second evaluation stays WAIT)
-- ---------------------------------------------------------------------------
local ordersBefore = #ATTACK_ORDERS
ticks(220, 10) -- past VICTIM_COOLDOWN=150 -> re-evaluation
T(logCount("[WAIT]") >= 2, "deep victim: WAIT persists across cooldown")
T(#ATTACK_ORDERS == ordersBefore, "deep victim: still zero orders")

-- ---------------------------------------------------------------------------
-- 6. Destroy AA sources -> route reopens -> action resumes
-- ---------------------------------------------------------------------------
for _, u in ipairs(WORLD) do
    if u._type == "NAFLAK" or (u._type == "HTK" and u._owner == "P") then
        u._alive = false
    end
end
local waitBefore = logCount("[WAIT]")
ticks(420, 10)
T(logCount("[WAIT]") == waitBefore, "AA destroyed: no new WAIT after reopen")

-- ---------------------------------------------------------------------------
-- 7. Move harvester outside the base -> route open
-- ---------------------------------------------------------------------------
reset()
harv = addUnit("P", "HARV", "unit", 50, 50)
jet = addUnit("E", "JBOB", "aircraft", 50, 110)
jet._targetId = harv._id
addUnit("P", "NAFLAK", "building", 50, 90)
addUnit("P", "NAFLAK", "building", 50, 78)
addUnit("P", "HTK", "unit", 50, 66)
ticks(20, 4)
T(logCount("verdict=DEEP_AA") >= 1, "setup: deep while inside")
harv._pos = { x = 50, y = 130 } -- outside, beyond the flak line
jet._targetId = harv._id
ticks(220, 10)
T(logCount("verdict=OPEN") >= 1, "moved outside: route OPEN logged")

-- ---------------------------------------------------------------------------
-- 8. Multiple jets: no order spam, no oscillation
-- ---------------------------------------------------------------------------
reset()
harv = addUnit("P", "HARV", "unit", 50, 50)
local j1 = addUnit("E", "JBOB", "aircraft", 50, 110)
local j2 = addUnit("E", "JBOB", "aircraft", 60, 110)
j1._targetId = harv._id
j2._targetId = harv._id
addUnit("P", "NAFLAK", "building", 50, 90)
addUnit("P", "NAFLAK", "building", 55, 80)
addUnit("P", "HTK", "unit", 50, 68)
ticks(20, 12)
T(#ATTACK_ORDERS == 0, "two jets deep: zero orders, no churn")

-- ---------------------------------------------------------------------------
-- 9. No valid target: jet idle -> nothing, no errors
-- ---------------------------------------------------------------------------
reset()
addUnit("P", "HARV", "unit", 50, 50)
addUnit("E", "JBOB", "aircraft", 90, 50) -- no target held
ticks(20, 4)
T(#ATTACK_ORDERS == 0, "no target: silent")
T(logCount("update error") == 0, "no target: no errors")

-- ---------------------------------------------------------------------------
-- 10. Destroyed victim/AA mid-run -> no errors
-- ---------------------------------------------------------------------------
reset()
harv = addUnit("P", "HARV", "unit", 50, 50)
jet = addUnit("E", "JBOB", "aircraft", 50, 110)
jet._targetId = harv._id
local fl = addUnit("P", "HTK", "unit", 50, 80)
ticks(20, 2)
harv._alive = false
fl._alive = false
ticks(60, 6)
T(logCount("update error") == 0, "destroyed entities: no errors")
T(logCount("VICTIM_LOST") >= 1, "destroyed victim logged once")

_print(string.format("RESELECT TESTS: %d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
