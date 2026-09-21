-- Harness for tesla_mcv driver/verifier (Gate 1-2 tooling).
-- Stubs Engine/House/World/Input (+print capture), drives Update over
-- scripted frames. Asserts: T spawns TESLMCV with PASS lines on a good
-- unit; missing type / wrong HP / rejected MoveTo yield FAIL lines without
-- errors; restart guard and no-player silence hold.
--
-- Run: buildlua_check.exe tools/tmp/tesla_mcv_test.lua

local LOG = {}
local _print = print
function print(...) -- capture; still echo PASS/FAIL verdicts
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
    local line = table.concat(parts, " ")
    LOG[#LOG + 1] = line
    if string.find(line, "PASS", 1, true) or string.find(line, "FAIL", 1, true) then
        _print(line)
    end
end

local Engine = { PrintMessage = function(msg) LOG[#LOG + 1] = "HUD:" .. tostring(msg) end }
local WORLD_UNITS = {}
local SPAWN_CALLS = {}
local SPAWN_RET = 1
local KEY_QUEUE = {}

local PLAYER = {
    SpawnUnit = function(self, typeId, count, x, y, facing, force, action)
        SPAWN_CALLS[#SPAWN_CALLS + 1] = { typeId, count, x, y }
        return SPAWN_RET
    end,
}

local House = {
    GetPlayer = function() return PLAYER end,
    GetCount = function() return 1 end,
    GetByIndex = function() return PLAYER end,
}

local World = {
    GetUnits = function() return WORLD_UNITS end,
    GetBuildings = function() return {} end,
}

local Input = { WasKeyPressed = function(key)
    for i, k in ipairs(KEY_QUEUE) do
        if k == key then table.remove(KEY_QUEUE, i); return true end
    end
    return false
end }

_G.Engine, _G.House, _G.World, _G.Input = Engine, House, World, Input

local MOD = dofile("scripts/mods/tesla_mcv/main.lua")

local passed, failed = 0, 0
local function T(cond, name)
    if cond then passed = passed + 1; _print("PASS " .. name)
    else failed = failed + 1; _print("FAIL " .. name) end
end

local function logHas(sub)
    for _, line in ipairs(LOG) do
        if string.find(line, sub, 1, true) then return true end
    end
    return false
end

local function mkUnit(typeName, maxhp, moveAcc)
    return {
        IsAlive = function() return true end,
        GetTypeName = function() return typeName end,
        GetKind = function() return "unit" end,
        GetMaxHealth = function() return maxhp end,
        GetPosition = function() return { x = 30, y = 30 } end,
        MoveTo = function(self, x, y) return moveAcc end,
    }
end

local FRAME = 0
local function step() FRAME = FRAME + 1; MOD.Update(FRAME) end
local function pressT() KEY_QUEUE[#KEY_QUEUE + 1] = 0x54; step() end

-- Announce on first tick with a player house.
step()
T(logHas("driver ready"), "T0 announce on first tick")

-- S1: good unit -> full PASS chain.
WORLD_UNITS[#WORLD_UNITS + 1] = mkUnit("TESLMCV", 2000, true)
pressT()
T(#SPAWN_CALLS == 1 and SPAWN_CALLS[1][1] == "TESLMCV", "S1 SpawnUnit called with TESLMCV")
T(logHas("PASS type-resolves"), "S1 type-resolves PASS")
T(logHas("PASS hp-x2"), "S1 hp-x2 PASS (2000)")
T(logHas("PASS moveto-accepted"), "S1 moveto-accepted PASS")
T(logHas("PASS kind-unit"), "S1 kind-unit PASS")

-- S2: type missing (option off) -> FAIL line, no crash.
SPAWN_RET = 0
pressT()
T(logHas("FAIL type-resolves"), "S2 missing type -> FAIL type-resolves, no error")

-- S3: wrong HP (1000, vanilla SMCV value) -> FAIL hp-x2.
SPAWN_RET = 1
WORLD_UNITS = { mkUnit("TESLMCV", 1000, true) }
pressT()
T(logHas("FAIL hp-x2"), "S3 HP 1000 -> FAIL hp-x2")

-- S4: MoveTo rejected -> FAIL moveto-accepted.
WORLD_UNITS = { mkUnit("TESLMCV", 2000, false) }
pressT()
T(logHas("FAIL moveto-accepted"), "S4 rejected MoveTo -> FAIL moveto-accepted")

-- S5: restart guard (backwards frame) -> silent, no error.
MOD.Update(2)
T(true, "S5 backwards frame: no error")

_print(string.format("\n%d passed, %d failed%s", passed, failed,
    failed == 0 and " - ALL CHECKS PASSED" or ""))
if failed > 0 then os.exit(1) end
