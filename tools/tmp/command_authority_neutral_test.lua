-- RCA Fix A verification: Neutral/Special are not combat houses.
-- Supplements (does NOT modify) tools/tmp/command_authority_test.lua, which
-- must keep passing unchanged (P/A behavior intact).
--
-- Stub surface copied from the existing harness; adds Neutral + Special
-- houses and civilian cars. Uses AUTH._S read-only (live state, never write).
--
-- Verifies:
--   1. CP seeding skips Neutral/Special (no ledger entries).
--   2. Survival income skips Neutral/Special.
--   3. nearestHostileUnit never returns Neutral/Special (killer-gate).
--   4. Fallback kill-split never credits Neutral/Special.
--   5. Director never spends for Neutral/Special (spend-gate).
--   6. Retaliation is never scheduled from nor executed as Neutral/Special,
--      while the normal A-house retaliation path still works.
--
-- Frame discipline (same as existing harness): world mutations and key
-- presses happen only on frames == 7 (mod 15); runTo() advances
-- monotonically; step(0) triggers a match restart.
--
-- Run: buildlua_check.exe tools/tmp/command_authority_neutral_test.lua

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

local World = {
    GetUnits = function()
        local out = {}
        for _, u in ipairs(WORLD) do
            if u:IsAlive() then out[#out + 1] = u end
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

-- ---------------------------------------------------------------------------
-- Scenario 1: seeding + survival skip Neutral/Special
-- ---------------------------------------------------------------------------

runTo(1)
T(cpOf("P") == 3, "seed: player starts with 3 CP")
T(cpOf("A") == 0, "seed: AI combat house in ledger at 0")
T(cpOf("Neutral") == nil, "seed: Neutral has no CP ledger entry")
T(cpOf("Special") == nil, "seed: Special has no CP ledger entry")

runTo(422) -- survival tick at 360 (default pay 1), zero losses
T(cpOf("P") == 4, "survival: player 3 -> 4")
T(cpOf("A") == 1, "survival: AI 0 -> 1")
T(cpOf("Neutral") == nil, "survival: Neutral still has no ledger entry")
T(cpOf("Special") == nil, "survival: Special still has no ledger entry")

-- ---------------------------------------------------------------------------
-- Scenario 2: killer-gate (Neutral car nearest to the victim must not earn)
-- ---------------------------------------------------------------------------

local pt = addUnit("P", "HTNK", 900, 100, 100)
local av = addUnit("A", "GI", 400, 200, 100)
local nc = addUnit("Neutral", "CAR", 100, 201, 100) -- nearest to av, must be skipped
local sc = addUnit("Special", "CAR", 100, 900, 900)

step(0) -- restart, scan at 0 observes the new roster
runTo(6); kill(av); runTo(15)
T(cpOf("P") == 3 + 5, "killer-gate: combatant P credited +5 despite farther distance")
T(cpOf("Neutral") == nil, "killer-gate: nearest Neutral car earns nothing")
T(cpOf("Special") == nil, "killer-gate: Special earns nothing")
T(AUTH._S.lastKillPos.P ~= nil and AUTH._S.lastKillPos.P.x == 200,
    "killer-gate: lastKillPos recorded for P at victim site")
T(AUTH._S.lastKillPos.Neutral == nil, "killer-gate: no lastKillPos for Neutral")

-- ---------------------------------------------------------------------------
-- Scenario 3: fallback split (no hostile unit alive -> ledger-only split)
-- ---------------------------------------------------------------------------

local pv = addUnit("P", "GI", 400, 100, 100)
local nc2 = addUnit("Neutral", "CAR", 100, 101, 100)
local sc2 = addUnit("Special", "CAR", 100, 102, 100)
-- NOTE: no live A units at all; only cars are near the victim.

step(0) -- restart
runTo(6); kill(pv); runTo(15)
T(cpOf("A") == 5, "fallback: combatant A (unit-less) credited +5 via ledger split")
T(cpOf("P") == 3, "fallback: victim house gains nothing extra")
T(cpOf("Neutral") == nil, "fallback: Neutral earns nothing")
T(cpOf("Special") == nil, "fallback: Special earns nothing")

-- ---------------------------------------------------------------------------
-- Scenario 4: Director spend-gate (A fed to 10 CP, Neutral owns cars)
-- ---------------------------------------------------------------------------

local ax = addUnit("A", "APOC", 800, 500, 500) -- full HP: repair no-op
local pv1 = addUnit("P", "GI", 400, 100, 100)
local pv2 = addUnit("P", "GI", 400, 110, 110)
local nc3 = addUnit("Neutral", "CAR", 100, 101, 100) -- nearest to pv1, must be skipped
local nc4 = addUnit("Neutral", "CAR", 100, 111, 110) -- nearest to pv2, must be skipped

step(0) -- restart
runTo(6); kill(pv1); runTo(21); kill(pv2); runTo(30)
T(cpOf("A") == 10, "director setup: A fed to exactly 10 CP")
T(cpOf("Neutral") == nil, "director setup: Neutral still at nil")
runTo(300) -- A's think at 241: repair no-op (ax full HP) -> reinforce (10 CP)
T(#HOUSES.A.spawns == 1 and HOUSES.A.spawns[1].x == 110 and HOUSES.A.spawns[1].y == 110,
    "director spends for combatant A at its last kill site (110,110)")
T(#HOUSES.Neutral.spawns == 0, "director never spawns for Neutral")
T(#HOUSES.Special.spawns == 0, "director never spawns for Special")
T(cpOf("Neutral") == nil and cpOf("Special") == nil,
    "director: no ledger entries created as a side effect")

-- Cleanup: remove the two spawned A HTNKs so scenario 5 starts with a
-- controlled roster (kills credit P, but ledgers reset at the restart).
runTo(306)
for _, u in ipairs(World.GetUnits()) do
    if u:GetOwner() == HOUSES.A and u:GetTypeName() == "HTNK" then kill(u) end
end
runTo(315)

-- ---------------------------------------------------------------------------
-- Scenario 5: retaliation never sources from Neutral/Special
-- NOTE: WORLD carries live leftovers across restarts (ax, pt, cars), so
-- this scenario first feeds P via ax's death, then works with that roster.
-- ---------------------------------------------------------------------------

step(0) -- restart; live roster: pt(P), ax(A), nc/sc/nc2/sc2/nc3/nc4 cars
runTo(6); kill(ax); runTo(15) -- killer P (cars skipped) -> P = 3 + 5 = 8
T(cpOf("P") == 8, "retaliation setup: P fed to 8 CP via ax kill")
T(cpOf("Neutral") == nil, "retaliation setup: Neutral earns nothing from ax")

runTo(21)
local rc = addUnit("Neutral", "CAR", 100, 100, 100)
press(0x56); step(22) -- V: sabotage; only cars are enemies -> best is a car
T(nc.disabledAt == 22, "sabotage still disables any enemy unit (first max-HP car)")
T(logCount("SABOTAGE by P") == 1, "sabotage message unchanged")
T(AUTH._S.retaliation == nil, "retaliation NOT scheduled from Neutral victim")
T(logCount("retaliation incoming") == 0, "no retaliation warning for Neutral source")
T(cpOf("P") == 0, "sabotage still charged 8 CP (8 -> 0)")

runTo(51)
local av2 = addUnit("A", "GI", 400, 600, 600)
local av3 = addUnit("A", "GI", 400, 610, 610)
local ab = addUnit("A", "APOC", 800, 400, 400)
runTo(66); kill(av2); kill(av3); runTo(75) -- kills on frame 67, seen at 75
T(cpOf("P") == 10, "retaliation setup: P refed to 10 CP")
runTo(81)
press(0x56); step(82) -- V: sabotage now hits the priciest (A tank, 800 HP)
T(logCount("retaliation incoming") == 1, "normal retaliation still scheduled vs combatant")
T(AUTH._S.retaliation ~= nil and AUTH._S.retaliation.from == "A",
    "retaliation source is the combatant house A")
runTo(382) -- due = 82 + 300 (survival tick at 360 fires in between, harmless)
T(logCount("DIRECTOR RETALIATION!") == 1, "normal retaliation still executes")
T(pt.disabledAt == 382, "retaliation still hits the player's priciest unit")
T(#HOUSES.Neutral.spawns == 0 and #HOUSES.Special.spawns == 0,
    "retaliation path spawned nothing for Neutral/Special")

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
