-- Headless verification harness for Command Authority v0.3 (iteration 3).
-- Stubs Engine/House/World/Input, drives AUTH.Update over a scripted
-- timeline, asserts: economy, director restraint (NO unsolicited EMP),
-- retaliation doctrine, directives (HUNT success/fail, DEFEND
-- success/fail), front-line reinforcements, refusal rules, MP gate.
--
-- Frame discipline: world mutations happen only on frames == 7 (mod 15),
-- i.e. strictly between scan frames (scan runs on frames % 15 == 0), and
-- key presses likewise. runTo() advances frame-by-frame so every Update
-- sees a monotonically increasing counter (the mod treats a backwards
-- frame as a match restart).
--
-- Run: lua_check.exe tools/tmp/command_authority_test.lua

-- ---------------------------------------------------------------------------
-- Stub surface
-- ---------------------------------------------------------------------------

local LOG = {}
local KEYS_PRESSED = {}
local CURRENT_FRAME = 0

local Engine = {
    PrintMessage = function(msg) LOG[#LOG + 1] = tostring(msg) end,
}

local HOUSES = {}          -- name -> house stub
local HOUSE_LIST = {}      -- index order
local WORLD = {}           -- unit stubs (dead ones filtered out on read)

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

-- forward-declared above mkHouse on purpose; define now
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
HOUSE_LIST = { HOUSES.P, HOUSES.A }

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

-- ---------------------------------------------------------------------------
-- Harness plumbing
-- ---------------------------------------------------------------------------

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

-- Kill = mark dead; the next scan observes the disappearance.
local function kill(u) u._alive = false end

-- ---------------------------------------------------------------------------
-- Scenario 1: streak income in isolation (no deaths at all)
-- ---------------------------------------------------------------------------

runTo(1)
press(0x54); step(2)
T(logCount("CP you 3 |") == 1, "intro: start CP 3")
T(logCount("DIRECTIVE") == 0, "no directives before DIRECTIVE_FIRST")

runTo(422) -- survive tick at 360 fires for both houses (zero losses)
press(0x54); step(424)
T(logCount("+1 [streak]") == 1, "streak income +1 for the player")
T(logCount("CP you 4 | A 1 |") == 1, "streak: you 4 | A 1")

-- ---------------------------------------------------------------------------
-- Scenario 2: main run (restart; streak pay disabled to keep arithmetic exact)
-- ---------------------------------------------------------------------------

AUTH.TUNING.CP_SURVIVE_PAY = 0

runTo(500)
local p1 = addUnit("P", "HTNK", 900, 100, 100)
local p2 = addUnit("P", "GI", 400, 110, 100)
local p3 = addUnit("P", "GI", 400, 105, 105)
local p4 = addUnit("P", "GI", 400, 108, 102)
local p5 = addUnit("P", "GI", 400, 112, 98)
local a1 = addUnit("A", "APOC", 800, 200, 100)
local a2 = addUnit("A", "GI", 400, 210, 110)
local a3 = addUnit("A", "APOC", 1000, 300, 100)

step(0) -- frame went backwards: match restart, full re-init, scan at 0

-- Economy: kill
runTo(6); kill(a2); runTo(15)
press(0x54); step(17)
T(logCount("+5 [kill]") == 1, "kill credit +5 [kill]")
T(logCount("CP you 8 |") == 1, "CP 3+5 = 8 after kill")

-- Economy: damage (fractional bank)
runTo(21); a1._hp = 400; runTo(37); a1._hp = 50; runTo(45)
press(0x54); step(47)
T(logCount("+1 [dmg]") == 1, "damage bank: 400 dmg = +1 CP")
T(logCount("CP you 9 |") == 1, "CP 9 after damage award (bank .875 held)")

-- Director restraint: rich-enough AI must NEVER EMP unasked
runTo(51); kill(p2); runTo(60)      -- A +5 -> 5
runTo(1211); kill(p3); runTo(1215)  -- A +5 -> 10
runTo(1441)                          -- A's 2nd think (1st at frame 1): repair a1 (6 CP)
T(a1._hp == 800, "director repairs its wounded a1 (50 -> 800)")
T(logCount("DIRECTOR: FIELD REPAIR") == 1, "director repair labeled DIRECTOR:")
runTo(1689); kill(p4); runTo(1695)  -- A +5 -> 9
runTo(1719); kill(p5); runTo(1725)  -- A +5 -> 14
runTo(2881)                          -- A's 3rd think: repair no-op, reinforce (10)
T(#HOUSES.A.spawns == 1 and HOUSES.A.spawns[1].x == 112 and HOUSES.A.spawns[1].y == 98,
    "director reinforces at ITS last kill site (112,98)")
T(p1.disabledAt == nil, "restraint: no player unit disabled by rich AI")
T(logCount("SABOTAGE") == 0, "restraint: zero sabotage without player's V")

-- Directive 1: HUNT (index 1 = hunt), target = enemy's priciest ground (a3)
runTo(3015)
T(logCount("DIRECTIVE: HUNT") == 1, "directive 1: HUNT announced at 50 s")
runTo(3021); kill(a3); runTo(3030)
T(logCount("+8 [directive]") == 1, "hunt bounty +8 to the killer")
T(logCount("DIRECTIVE fulfilled: enemy APOC destroyed") == 1, "hunt fulfilled message")
press(0x54); step(3032)
T(logCount("CP you 22 |") == 1, "CP 9 +5 kill +8 bounty = 22")

-- Directive 2: DEFEND (index 2), target = own priciest ground (p1)
runTo(3945)
T(logCount("DIRECTIVE: DEFEND") == 1, "directive 2: DEFEND announced")
runTo(8445) -- expires fulfilled at 8430
T(logCount("DIRECTIVE fulfilled: your HTNK survived") == 1, "defend fulfilled message")
press(0x54); step(8447)
T(logCount("CP you 30 |") == 1, "CP 22 +8 = 30 on defend expiry")

-- Directive 3: HUNT again (index 3), target a1; let it expire -> fail
runTo(9345)
T(logCount("DIRECTIVE: HUNT") == 2, "directive 3: HUNT (alternating)")
runTo(13847) -- expires at 13845
T(logCount("DIRECTIVE failed: the APOC escaped") == 1, "hunt expiry = failure, no bounty")
press(0x54); step(13849)
T(logCount("CP you 30 |") == 2, "no CP on failed hunt")

-- Directive 4: DEFEND (index 4); kill the target -> killer profits
runTo(14751)
T(logCount("DIRECTIVE: DEFEND") == 2, "directive 4: DEFEND (alternating)")
kill(p1); runTo(14760)
T(logCount("DIRECTIVE lost: your HTNK was destroyed - the killer banks +8 CP") == 1,
    "defend failure message")
press(0x54); step(14762)
T(logCount("CP you 30 | A 17 |") == 1, "A banks kill 5 + bounty 8 = 17; player stays 30")

-- Sabotage -> scheduled retaliation (doctrine under test)
runTo(15001)
local p6 = addUnit("P", "HTNK", 900, 150, 150)
runTo(15006)
press(0x56); step(15007) -- V
T(a1.disabledAt == 15007, "player sabotage disables enemy priciest a1")
T(logCount("SABOTAGE by P: APOC (A) disabled for 6 s!") == 1, "sabotage message")
T(logCount("CP -8 [sabotage] = 22") == 1, "sabotage costs 8 (30 -> 22)")
T(logCount("retaliation incoming - impact in 5 s") == 1, "retaliation announced")
runTo(15307) -- retaliation due (per-frame loop, no think needed)
T(logCount("DIRECTOR RETALIATION!") == 1, "retaliation executed on time")
T(p6.disabledAt == 15307, "retaliation hits the player's priciest unit")
T(logCount("SABOTAGE by A: HTNK (P) disabled for 6 s!") == 1, "retaliation message")

-- Front-line reinforcement for the player + refusal rules
press(0x5A); step(15322) -- Z
T(HOUSES.P.spawns[1] ~= nil and HOUSES.P.spawns[1].x == 300,
    "player reinforcements land at their last kill site (300,100)")
T(logCount("CP -10 [reinforce] = 12") == 1, "reinforce costs 10 (22 -> 12)")
press(0x54); step(15324)
T(logCount("CP you 12 |") == 1, "CP 12 after reinforce")
press(0x43); step(15326) -- C blitz
T(logCount("CP -4 [blitz] = 8") == 1, "blitz costs 4 (12 -> 8)")
press(0x58); step(15327) -- X repair: nothing wounded -> no charge
press(0x54); step(15329)
T(logCount("CP you 8 | A 7 |") == 1, "failed repair charges nothing (CP stays 8)")
T(logCount("FIELD REPAIR") == 1, "no second repair message")

runTo(15841) -- A's next think: reinforce lands at p1's death site
T(HOUSES.A.spawns[2] ~= nil and HOUSES.A.spawns[2].x == 100,
    "second director reinforce lands at p1's death site (100,100)")

-- MP gate: restart with two human houses -> powers lock
runTo(15900)
HOUSES.A._human = true
step(5) -- restart
press(0x56); step(7) -- V pressed in MP: must do nothing
T(a1.disabledAt == 15007 and p6.disabledAt == 15307, "MP: no new disables from hotkey")
T(logCount("SABOTAGE by P") == 1, "MP: sabotage hotkey blocked")
press(0x54); step(9)
T(logCount("CP you 3 |") == 2, "MP: fresh economy, T-status still works")
T(logCount("powers locked") == 1, "MP intro announces locked powers")

-- ---------------------------------------------------------------------------
-- Summary
-- ---------------------------------------------------------------------------

print(string.format("\n%d passed, %d failed%s", passed, failed,
    failed == 0 and " - ALL CHECKS PASSED" or ""))
if failed > 0 then
    print("--- log tail ---")
    for i = math.max(1, #LOG - 12), #LOG do print(LOG[i]) end
end
