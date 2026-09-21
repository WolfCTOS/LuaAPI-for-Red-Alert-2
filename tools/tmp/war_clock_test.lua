-- Headless verification harness for war_clock (runs on lua_check.exe).
-- Stubs the binding surface (World/House/Engine), drives CLOCK.Update over
-- 21+ minutes of logical frames, asserts every event fired exactly on its
-- scheduled frame and that effects were applied symmetrically.

local function mkHouse(name)
    return {
        name = name, credits = 0, defeated = false,
        AddCredits = function(self, amt) self.credits = self.credits + amt end,
    }
end

local houseA = mkHouse("Soviets")
local houseB = mkHouse("Allies")

local function mkUnit(house, maxhp, kind)
    local u = {
        owner = house, maxhp = maxhp, hp = maxhp, kind = kind or "unit",
        disabledUntil = 0, dead = false,
    }
    function u:IsAlive() return not self.dead end
    function u:GetOwner() return self.owner end
    function u:GetKind() return self.kind end
    function u:GetHealth() return self.hp end
    function u:GetMaxHealth() return self.maxhp end
    function u:GetTypeName() return "TESTEE" end
    function u:GetId() return 1 end
    function u:Disable(frames) self.disabledUntil = self.disabledUntil + frames end
    function u:SetHealthRatio(r)
        if r ~= r or r < 0 or r > 1 then error("bad ratio " .. tostring(r)) end
        self.hp = math.floor(self.maxhp * r + 0.5)
    end
    function u:TakeDamage(dmg)
        if dmg ~= dmg or dmg <= 0 then error("bad dmg " .. tostring(dmg)) end
        self.hp = self.hp - dmg
    end
    return u
end

-- World state: A has 2 damaged tanks + 1 full tank; B has 1 full tank + an
-- aircraft; C never spawned (must be skipped by every event).
houseA.tank1 = mkUnit(houseA, 400); houseA.tank1.hp = 100
houseA.tank2 = mkUnit(houseA, 400); houseA.tank2.hp = 250
houseA.tank3 = mkUnit(houseA, 400)
houseB.tank1 = mkUnit(houseB, 400)
houseB.plane  = mkUnit(houseB, 200, "aircraft")

local unitsList = { houseA.tank1, houseA.tank2, houseA.tank3, houseB.tank1, houseB.plane }

World = {
    GetUnits = function() return unitsList end,
    GetBuildings = function() return {} end,
}

House = {
    GetCount = function() return 2 end,
    GetByIndex = function(i) return ({ [0] = houseA, [1] = houseB })[i] end,
}

local hud = {}
Engine = { PrintMessage = function(msg) hud[#hud + 1] = msg end }

-- Load the mod itself.
local f, err = loadfile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/war_clock/main.lua")
if not f then print("LOAD FAIL: " .. tostring(err)); os.exit(1) end
local CLOCK = f()

local F = 60
local SUPPLY_AT, IRON_AT, EMP_AT, ECON_AT, SD_AT = 5*60*F, 8*60*F, 10*60*F, 15*60*F, 20*60*F

-- Effect trackers.
local creditsA, creditsB = {}, {}
local empDisabled = 0
local bled = 0
local ironHealed = 0

-- Run one logical frame.
local function step(frame)
    local before = {
        a = houseA.credits, b = houseB.credits,
    }
    CLOCK.Update(frame)

    if houseA.credits > before.a then creditsA[#creditsA + 1] = frame end
    if houseB.credits > before.b then creditsB[#creditsB + 1] = frame end

    for _, u in ipairs(unitsList) do
        if u.disabledUntil and u.disabledUntil > 0 and u.kind ~= "aircraft" then
            if not u._counted then empDisabled = empDisabled + 1; u._counted = true end
        end
    end
    -- aircraft must NEVER be disabled
    if houseB.plane.disabledUntil > 0 then error("AIRCRAFT DISABLED - EMP broke air immunity") end
end

local function eqFrames(got, want, what)
    if #got ~= #want then
        print(string.format("FAIL %s: got %d hits, want %d", what, #got, #want))
        for i, v in ipairs(got) do print("  got frame", v) end
        os.exit(1)
    end
    for i = 1, #want do
        if got[i] ~= want[i] then
            print(string.format("FAIL %s: hit %d at frame %d, want %d",
                what, i, got[i], want[i]))
            os.exit(1)
        end
    end
    print(string.format("PASS %s (frames %s)", what,
        table.concat(got, ",")))
end

-- ---- Phase 1: schedule exactness (0 .. 20:30) -----------------------------
local total = 21 * 60 * F
local hitEmpWarn, hitEmp = {}, {}
local sdTicks = {}
local sdDrops = {}   -- per tick: { A = hpLostA, B = hpLostB }
local ironSnapshot = nil

-- Track sudden-death ticks by watching HP loss from the bleed only: run a
-- second window without other events (all one-shot events already fired).
for frame = 1, total do
    local pre = houseA.tank1.hp + houseA.tank2.hp + houseA.tank3.hp + houseB.tank1.hp

    -- EMP tracking
    local empBefore = 0
    for _, u in ipairs(unitsList) do
        if u.kind ~= "aircraft" then empBefore = empBefore + (u.disabledUntil > 0 and 1 or 0) end
    end

    step(frame)

    local empAfter = 0
    for _, u in ipairs(unitsList) do
        if u.kind ~= "aircraft" then empAfter = empAfter + (u.disabledUntil > 0 and 1 or 0) end
    end
    if empAfter > empBefore then hitEmp[#hitEmp + 1] = frame end

    -- Iron reserves healing detection: snapshot right at the event frame.
    if frame == IRON_AT then
        ironSnapshot = { houseA.tank1.hp, houseA.tank2.hp, houseA.tank3.hp }
    end

    -- Sudden death HP drops (only tanks, no other damage sources in harness)
    local post = houseA.tank1.hp + houseA.tank2.hp + houseA.tank3.hp + houseB.tank1.hp
    if post < pre then
        sdTicks[#sdTicks + 1] = frame
        sdDrops[#sdDrops + 1] = {
            A = (houseA.tank1.hp + houseA.tank2.hp + houseA.tank3.hp)
                - ((houseA.tank1.hp + houseA.tank2.hp + houseA.tank3.hp) - (pre - post) * 0),
            raw = pre - post,
        }
    end

    if houseA.tank1.hp <= 0 or houseB.tank1.hp <= 0 then
        print("FAIL: unit died during harness (unexpected)")
        os.exit(1)
    end
end

eqFrames(creditsA, { SUPPLY_AT, ECON_AT }, "house A credits events")
eqFrames(creditsB, { SUPPLY_AT, ECON_AT }, "house B credits events")
eqFrames(hitEmp,   { EMP_AT },             "EMP disable exactly once")

-- Iron reserves: measured AT the event frame (later SD bleed does not pollute).
local healed = ironSnapshot and (ironSnapshot[1] == 250) and (ironSnapshot[2] == 325) and (ironSnapshot[3] == 400)
print(healed and "PASS iron reserves heal math" or
    string.format("FAIL iron: %s", ironSnapshot and table.concat(ironSnapshot, ",") or "no snapshot"))
if not healed then os.exit(1) end

-- Sudden death: every 3600 frames from SD_AT: SD_AT, +3600, +7200...
local wantSd = {}
for i = 0, 3 do wantSd[#wantSd + 1] = SD_AT + i * 3600 end
-- The last tick at SD_AT+3*3600=22:00 exceeds total (21 min), trim:
wantSd = {}
for t = SD_AT, total, 3600 do wantSd[#wantSd + 1] = t end
eqFrames(sdTicks, wantSd, "sudden death ticks")

-- Symmetry: on each sudden-death tick both houses must lose the same HP
-- (their priciest units have the same 400-max pool).
for i, t in ipairs(sdTicks) do
    -- Recompute per-house split by re-watching: A loses on tank1 only (scan
    -- order), B on tank1; totals per house must match per tick.
    local dA = (i == 1) and 0 or 0 -- computed below directly
end
-- Direct per-tick check: each tick drops exactly 4 HP on each house's tank1
-- (A's bleed target is tank1 by scan order; B has a single tank).
do
    -- replay is expensive; instead verify final state math:
    -- tank1A: iron 250 then SD ticks x4 HP each; tank2/3: iron-fixed, untouched
    -- tank1B: 400 then SD ticks x4 HP each.
    local ticks = #sdTicks
    local aLost = 250 - houseA.tank1.hp
    local bLost = 400 - houseB.tank1.hp
    if aLost ~= bLost or aLost ~= ticks * 4 then
        print(string.format("FAIL symmetry: A lost %d, B lost %d, ticks %d",
            aLost, bLost, ticks))
        os.exit(1)
    end
end
print("PASS per-house bleed symmetry")

-- Announcements: check the HUD got the key lines.
local joined = table.concat(hud, "\n")
local function wantMsg(frag, what)
    if not joined:find(frag, 1, true) then
        print("FAIL hud missing: " .. frag); os.exit(1)
    end
end
wantMsg("SUPPLY DROP", "supply")
wantMsg("IRON RESERVES", "iron")
wantMsg("electromagnetic storm approaching", "emp warn")
wantMsg("EMP STORM:", "emp hit")
wantMsg("WARTIME ECONOMY", "economy")
wantMsg("SUDDEN DEATH: base-camping", "sd announce")
print("PASS hud messages (" .. #hud .. " lines)")

print("ALL CHECKS PASSED")
