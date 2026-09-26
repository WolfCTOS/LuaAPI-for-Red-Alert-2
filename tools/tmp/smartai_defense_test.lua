-- Harness for SmartAI M2-C4 adaptive defense (severity slice).
-- Proves: LOW scout -> 1 defender (reserve kept), HIGH push ->
-- defendersN+1 on top intruder, downgrade re-evaluates, deterministic
-- replay, reset clean. No new abstraction: severity scales the existing
-- Tier-1 allocation.
--
-- Run: buildlua_check.exe tools/tmp/smartai_defense_test.lua (repo root)

package.path = "scripts/?.lua;" .. package.path

local LOG = {}
local CURRENT_FRAME = 0

local Engine = {
    PrintMessage = function(msg) LOG[#LOG + 1] = tostring(msg) end,
}

local HOUSES = {}
local WORLD = {}
local nextId = 400

local function mkHouse(name, isHuman)
    local h = {
        _name = name, _human = isHuman, _allies = {},
        _credits = 1000, _po = 200, _pd = 150,
        GetName = function(self) return self._name end,
        IsHuman = function(self) return self._human end,
        IsAlliedWith = function(self, other)
            local on = type(other) == "table" and other._name or tostring(other)
            return self._allies[on] == true
        end,
        GetCredits = function(self) return self._credits end,
        GetPowerOutput = function(self) return self._po end,
        GetPowerDrain = function(self) return self._pd end,
    }
    return h
end

-- opts (7th arg, optional, additive): { noPos = true } makes GetPosition
-- return nil so snapOf sets hasPos=false. Existing 6-arg calls unaffected.
function mkUnit(ownerName, typeName, kind, x, y, idle, opts)
    nextId = nextId + 1
    opts = opts or {}
    local u = {
        _id = nextId, _owner = ownerName, _type = typeName,
        _kind = kind or "unit", _pos = { x = x, y = y },
        _alive = true, _idle = (idle == nil) and true or idle,
        _hp = 400, _maxhp = 400, _cost = 700, _mission = "Guard",
        moves = {}, hunts = 0, attacks = {},
        IsAlive = function(self) return self._alive end,
        GetOwner = function(self) return HOUSES[self._owner] end,
        GetKind = function(self) return self._kind end,
        GetTypeName = function(self) return self._type end,
        GetHealth = function(self) return self._hp end,
        GetMaxHealth = function(self) return self._maxhp end,
        GetCost = function(self) return self._cost end,
        GetPosition = function(self)
            if opts.noPos then return nil end
            return self._pos
        end,
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
        IsAttacking = function(self) return self._attacking == true end,
        GetMission = function(self) return self._mission end,
        Sell = function(self)
            if self._kind ~= "building" then return false end
            self.sold = true
            self._alive = false
            return true
        end,
        Attack = function(self, target)
            -- opts.attackFails simulates an engine that refuses the order, so
            -- the Act-side `orderFail` counter can be exercised. Stub-only:
            -- production orderAttack() is untouched.
            if opts.attackFails then return false end
            self.attacks[#self.attacks + 1] = { target = target, frame = CURRENT_FRAME }
            local tid = nil
            if type(target) == "table" and target.GetId then
                local ok, id = pcall(target.GetId, target)
                if ok then tid = id end
            elseif type(target) == "number" then
                tid = target
            end
            self._targetId = tid
            return true
        end,
        GetTarget = function(self)
            if self._targetId == nil then return nil end
            for _, v in ipairs(WORLD) do
                if v._id == self._targetId and v._alive then return v end
            end
            return nil
        end,
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
    GetAllUnits = function()
        local out = {}
        for _, u in ipairs(WORLD) do
            if u:IsAlive() then out[#out + 1] = u end
        end
        return out
    end,
    GetUnitsInRadius = function(x, y, r)
        local out = {}
        for _, u in ipairs(WORLD) do
            if u:IsAlive() then
                local pos = u:GetPosition()
                if pos then
                    local dx, dy = pos.x - x, pos.y - y
                    if dx * dx + dy * dy <= r * r then out[#out + 1] = u end
                end
            end
        end
        return out
    end,
}

_G.Engine, _G.House, _G.World = Engine, House, World

-- main.lua's dlog() writes through print(). Intercept it (before the mod is
-- loaded) so the harness can assert HOW MANY events a single resolved contact
-- produces. The ledger counters were already correct, but the event used to
-- fire once per defending unit, so a five-unit squad trading with one intruder
-- printed five identical CONTACT lines. Visible output is preserved.
--
-- The sink is a named function, not a bare `print` swap, because captureCensus()
-- also swaps print for the duration of a census step. Anything that wants log
-- lines must hook consolePrint(); a locally captured `realPrint` gets clobbered
-- by the first census and silently records nothing afterwards.
local CONTACT_EVENTS = {}
local realPrint = print
local function consolePrint(...)
    local n = select("#", ...)
    local parts = {}
    for i = 1, n do parts[i] = tostring((select(i, ...))) end
    local line = table.concat(parts, " ")
    local tag = line:match("^%[SMARTAI%]%[([%a_]+)%]")
    if tag == "CONTACT" then
        CONTACT_EVENTS[#CONTACT_EVENTS + 1] = {
            outcome = line:match("outcome=([%a_]+)"),
            intruder = line:match("intruder=(%S+)"),
        }
    end
    realPrint(line)
end
_G.print = consolePrint

-- Count CONTACT events for one intruder id with one outcome.
local function contactCount(intruderId, outcome)
    local c = 0
    for _, ev in ipairs(CONTACT_EVENTS) do
        if ev.intruder == tostring(intruderId) and ev.outcome == outcome then
            c = c + 1
        end
    end
    return c
end

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

local function attacksOn(u)
    local n = 0
    for _, w in ipairs(WORLD) do
        for _, a in ipairs(w.attacks) do
            if a.target == u then n = n + 1 end
        end
    end
    return n
end

-- Roster: A anchors + 5 idle tanks at staggered distances (nearest first
-- is deterministic) + single E1 scout (0.5 -> LOW).
mkBuilding("A", "NAHAND", 20, 20, 1.0)
mkBuilding("A", "NAWEAP", 22, 20, 1.0)
mkBuilding("A", "NACNST", 21, 22, 1.0)
local d1 = mkUnit("A", "HTNK", "unit", 30, 30, true)
local d2 = mkUnit("A", "HTNK", "unit", 31, 31, true)
local d3 = mkUnit("A", "HTNK", "unit", 32, 32, true)
local d4 = mkUnit("A", "HTNK", "unit", 33, 33, true)
local d5 = mkUnit("A", "HTNK", "unit", 34, 34, true)
local scout = mkUnit("P", "E1", "infantry", 25, 25, true)

-- T1 LOW: one scout -> exactly 1 defender (old code sent defendersN=2).
runTo(35)
T(#d1.attacks == 1, "T1 LOW scout gets one defender")
T(#d2.attacks == 0 and #d3.attacks == 0 and #d4.attacks == 0
    and #d5.attacks == 0, "T1 reserve kept (4 untouched, no overcommit)")

-- T2 HIGH: kill scout, send push (4 pricey HTNK + DTRUCK, score 11) ->
-- top intruder (DTRUCK) gets defendersN+1 = 3.
kill(scout)
local push = {}
for i = 1, 4 do
    local p = mkUnit("P", "HTNK", "unit", 25 + i, 25, true)
    p._cost = 1500
    push[#push + 1] = p
end
local dtruck = mkUnit("P", "DTRUCK", "unit", 26, 26, true)
dtruck._cost = 1000
runTo(95) -- scans at 60, 90
T(attacksOn(dtruck) == 3, "T2 HIGH push: 3 defenders on suicide truck")

-- T3 reserve at HIGH is spent, not hoarded: all 5 drafted across threats.
local drafted = 0
for _, d in ipairs({ d1, d2, d3, d4, d5 }) do
    if #d.attacks > 0 then drafted = drafted + 1 end
end
T(drafted == 5, "T3 HIGH commits the pool (reserve spent on push)")

-- T4 downgrade: push gone, lone scout again -> re-evaluates to LOW.
for _, p in ipairs(push) do kill(p) end
kill(dtruck)
local scout2 = mkUnit("P", "E1", "infantry", 25, 25, true)
local before = 0
for _, d in ipairs({ d1, d2, d3, d4, d5 }) do before = before + #d.attacks end
runTo(155) -- scans at 120, 150
local after = 0
for _, d in ipairs({ d1, d2, d3, d4, d5 }) do after = after + #d.attacks end
T(after - before == 1, "T4 downgrade re-evaluates to LOW (one new attack)")
T(attacksOn(scout2) == 1, "T4 the new attack is on the scout")

-- T5 determinism + reset: full restart, identical scout -> same defender.
step(5)
kill(scout2)
local scout3 = mkUnit("P", "E1", "infantry", 25, 25, true)
local b5 = #d1.attacks
runTo(65)
T(#d1.attacks - b5 == 1 and attacksOn(scout3) == 1,
    "T5 replay reproduces LOW (same nearest defender)")

-- T6 lease-lite: hijacked defender re-asserts bounded, then releases.
-- Clean world so the lease window (60f, shorter than Tier-1 refresh 150f)
-- is the only re-issue path.
step(5)
for _, u in ipairs(WORLD) do
    if u:IsAlive() and u:GetKind() ~= "building" then kill(u) end
end
local pX = mkUnit("P", "HTNK", "unit", 25, 25, true)
local pY = mkUnit("P", "HTNK", "unit", 100, 100, true) -- alive, far, non-intruder
local dX = mkUnit("A", "HTNK", "unit", 30, 30, true)
runTo(35) -- scan 30: dX attacks pX (LOW, exact 1)
T(#dX.attacks == 1, "T6 lease baseline engages")
dX._targetId = pY._id -- vanilla yanks the defender off-target
runTo(95) -- scans 60 (Tier-1 holds, lease window shut), 90 (retry 1)
T(#dX.attacks == 2, "T6 first yank re-asserted once (lease first responder)")
dX._targetId = pY._id -- yanked again
runTo(200) -- retry 2 at ~150+
T(#dX.attacks == 3, "T6 second yank re-asserted (last retry)")
dX._targetId = pY._id -- yanked a third time
runTo(400)
T(#dX.attacks == 3, "T6 third yank released (bounded, stable)")

-- T7-T9 retaliate (user case): AI Apoc chasing a chopper while a hostile
-- Apoc chews it -> turns onto the shooter (target corrected, stays fighting).
step(5)
for _, u in ipairs(WORLD) do
    if u:IsAlive() and u:GetKind() ~= "building" then kill(u) end
end
local hunter = mkUnit("A", "APOC", "unit", 50, 50, true)
local chopper = mkUnit("P", "PDPLANE", "aircraft", 50, 62, true)
local shooter = mkUnit("P", "APOC", "unit", 52, 52, true)
runTo(35) -- scan 30: stores hp, no order without a drop
T(#hunter.attacks == 0, "T7 no retaliation without damage")
hunter._hp = 350
hunter._targetId = chopper._id -- holding the chopper while shot
runTo(95) -- scans 60, 90
T(#hunter.attacks == 1 and hunter.attacks[1].target == shooter,
    "T7 unit under fire turns onto its attacker")

-- T8: stable HP -> no churn, and the corrected target holds.
runTo(200)
T(#hunter.attacks == 1, "T8 no re-issue without new damage")

-- T9: cooldown Р В Р вЂ Р В РІР‚С™Р Р†Р вЂљРЎСљ fresh drop inside the window is ignored, after it orders.
hunter._targetId = chopper._id -- yanked back onto the chopper
hunter._hp = 300
runTo(260) -- scans 210-260: retalFrame=60ish, window shut
T(#hunter.attacks == 1, "T9 drop inside cooldown ignored")
runTo(370) -- window opens at retalFrame+300, hp stable (no event yet)
hunter._hp = 250
runTo(450)
T(#hunter.attacks == 2, "T9 drop after cooldown re-orders")

-- =========================================================================
-- T31-T42: C4 DETECTOR DIAGNOSTIC (diagnostic only, zero gameplay change)
-- Verifies the mirror attributes every unit to the FIRST failing predicate
-- of the detector chain, and that distPass == intr (the guard that catches
-- mirror/detector divergence instead of hiding it).
-- The census gate is frame % 1800 < 30 (SMARTAI_CENSUS_EVERY=1800 in
-- main.lua, P.scan=30 for the medium preset). No gameplay assertion here is
-- new: T1-T30 already pin the real orders; T41 re-checks one.
-- =========================================================================
-- NOTE: no forward step here. captureTo() runs frame-by-frame, so jumping
-- the cursor past the census frame would leave the loop empty (a previous
-- revision stepped to 2000 then targeted 1800 and silently ran no frames).
for _, u in ipairs(WORLD) do
    if u:IsAlive() then kill(u) end
end
-- Bystander owner, NOT an AI house: exercises the ALLIED rejection without
-- touching HOUSE_LIST (which would add a second C4DIAG emitter).
-- Direction matters: the detector asks allied(A, candOwner), i.e.
-- A:IsAlliedWith(Q) -> reads HOUSES.A._allies["Q"].
HOUSES.Q = mkHouse("Q", false)
HOUSES.A._allies["Q"] = true

mkBuilding("A", "NAHAND", 20, 20, 1.0)
local dD1 = mkUnit("A", "HTNK", "unit", 30, 30, true)
local okIntr = mkUnit("P", "HTNK", "unit", 24, 24, true)   -- valid intruder
local farAway = mkUnit("P", "HTNK", "unit", 200, 200, true) -- DISTANCE reject
local allyU = mkUnit("Q", "HTNK", "unit", 25, 25, true)   -- ALLIED reject
local civU = mkUnit("P", "CAR", "unit", 26, 26, true)     -- CIVIL reject
local airU = mkUnit("P", "ZEP", "aircraft", 27, 27, true) -- KIND reject
local noPosU = mkUnit("P", "HTNK", "unit", 0, 0, true, { noPos = true })

local CAP = {}
local realPrint = print
local function captureTo(target)
    _G.print = function(...)
        local n = select("#", ...)
        local parts = {}
        for i = 1, n do parts[i] = tostring((select(i, ...))) end
        CAP[#CAP + 1] = table.concat(parts, " ")
    end
    runTo(target)
    _G.print = realPrint
end
local function c4diagFor(tag, house)
    local pre = "[SMARTAI][" .. tag .. "] frame="
    for _, l in ipairs(CAP) do
        if l:sub(1, #pre) == pre and l:find("house=" .. house .. " ", 1, true) then
            return l
        end
    end
    return nil
end
-- ADAPTSTATE is emitted on the census gate (`frame % 1800 < P.scan`), and the
-- adaptation counters are PER SCAN, so sample exactly that one scan.
--
-- `setup`, when given, runs AFTER runTo(target-1) and BEFORE the census step.
-- The Act-side diagnostic is evaluated at the top of the Act block, while the
-- C4DIAG line is emitted at the bottom of it -- so anything Tier 1 drafted
-- earlier in the match is already in defenseState and counts as OFFICER-held.
-- Building the scenario on target-1 is therefore the only way to observe a
-- freshly eligible unit: on the census frame it is seen before it is drafted.
local function captureCensus(setup)
    local target = CURRENT_FRAME + (1800 - (CURRENT_FRAME % 1800))
    runTo(target - 1)
    if setup then setup() end
    CAP = {}
    -- Restore whatever was installed before this step, NOT `realPrint`:
    -- `realPrint` is the raw console, so restoring it would drop the CONTACT
    -- event recorder permanently.
    local rp = _G.print
    _G.print = function(...)
        local n = select("#", ...)
        local parts = {}
        for i = 1, n do parts[i] = tostring((select(i, ...))) end
        CAP[#CAP + 1] = table.concat(parts, " ")
    end
    step(target)
    _G.print = rp
end
-- ADAPT is an EVENT: unthrottled, so the newest line is the current one.
local function adaptLine()
    for i = #CAP, 1, -1 do
        if CAP[i]:find("[SMARTAI][ADAPT]", 1, true) then return CAP[i] end
    end
    return nil
end
local function adaptState()
    return c4diagFor("ADAPTSTATE", "A")
end
local function fld(line, key)
    local v = line:match(key .. "=(%S+)")
    return v and tonumber(v) or v
end
local function movesTo(u, x, y)
    for _, m in ipairs(u.moves) do
        if m.x == x and m.y == y then return true end
    end
    return false
end

CAP = {}
captureTo(1800)
local dg = c4diagFor("C4DIAG", "A")

T(dg ~= nil, "T31 C4DIAG emitted for the AI house on a census frame")
if dg then
    T(fld(dg, "home") == 1, "T32 home counts own buildings with a position")
    T(fld(dg, "cands") == 7, "T32b cands counts every mobile in the snapshot")
    T(fld(dg, "R") == 18, "T32c intruderR reported from the live preset")
    T(fld(dg, "own") == 1, "T33 OWN reject: the house's own defender")
    T(fld(dg, "allied") == 1, "T34 ALLIED reject: allied hostile skipped")
    T(fld(dg, "civil") == 1, "T35 CIVIL reject: CAR is a civilian type")
    T(fld(dg, "kind") == 1, "T36 KIND reject: aircraft is not a DEFENSE_KIND")
    T(dg:find("kindsRej={aircraft=1}", 1, true) ~= nil,
        "T36b KIND reject reports the offending kind histogram")
    T(fld(dg, "nopos") == 1, "T37 NOPOS reject: unit without a position")
    T(fld(dg, "kindIn") == 4, "T38 kindIn counts units that reached the KIND test")
    T(fld(dg, "pass") == 2, "T39 pass counts units clearing every filter")
    T(fld(dg, "distRej") == 1, "T40 DISTANCE reject separated from FILTER reject")
    T(fld(dg, "distPass") == 1 and fld(dg, "intr") == 1,
        "T41 distPass == intr (mirror agrees with the real detector)")
    T(dg:find("mismatch=false", 1, true) ~= nil, "T41b mismatch flag is false")
    T(dg:find("note=MIXED", 1, true) ~= nil, "T42 note classifies the mixed case")
    T(fld(dg, "kind") + fld(dg, "nopos") + fld(dg, "pass") == fld(dg, "kindIn"),
        "T42b funnel identity: kindIn = kind + nopos + pass")
end
T(#dD1.attacks == 1 and dD1.attacks[1].target == okIntr,
    "T43 diagnostic changed no defense order (1 Attack, on the real intruder)")

local smp = c4diagFor("C4DIAG_SAMPLE", "A")
T(smp ~= nil, "T44 coordinate sample emitted for the distance reject")
if smp then
    T(smp:find("why=DISTANCE", 1, true) ~= nil, "T44b sample names the reason")
    T(smp:find("cand=(200,200)", 1, true) ~= nil, "T44c sample shows candidate coords")
    T(smp:find("bpos=(20,20)", 1, true) ~= nil, "T44d sample shows building coords")
    T(smp:find("d2=64800", 1, true) ~= nil, "T44e sample shows computed distance^2")
    T(smp:find("R2=324", 1, true) ~= nil, "T44f sample shows intruderR^2 (unit check)")
end

-- CASE D: every filter-clearing unit is in range -> detector healthy.
kill(farAway)
CAP = {}
captureTo(3600)
dg = c4diagFor("C4DIAG", "A")
T(dg ~= nil and dg:find("note=INTRUDERS_OK", 1, true) ~= nil
    and fld(dg, "intr") == 1 and fld(dg, "distRej") == 0,
    "T45 CASE D distinguished: detector does produce intruders")

-- CASE C: filters pass, distance empties the set.
kill(okIntr)
local far2 = mkUnit("P", "HTNK", "unit", 210, 210, true)
CAP = {}
captureTo(5400)
dg = c4diagFor("C4DIAG", "A")
T(dg ~= nil and dg:find("note=DISTANCE_REJECT_ALL", 1, true) ~= nil
    and fld(dg, "pass") == 1 and fld(dg, "intr") == 0,
    "T46 CASE C distinguished: filters pass, distance empties the set")

-- CASE A/B: no unit clears the filter chain.
kill(far2)
CAP = {}
captureTo(7200)
dg = c4diagFor("C4DIAG", "A")
T(dg ~= nil and dg:find("note=FILTER_REJECT_ALL", 1, true) ~= nil
    and fld(dg, "pass") == 0 and fld(dg, "intr") == 0,
    "T47 CASE A/B distinguished: no unit clears the filter chain")

-- Empty base: #home == 0 must be reported, not silently skipped.
for _, u in ipairs(WORLD) do
    if u:IsAlive() and u:GetKind() == "building" then kill(u) end
end
CAP = {}
captureTo(9000)
dg = c4diagFor("C4DIAG", "A")
T(dg ~= nil and dg:find("note=NO_HOME_BUILDINGS", 1, true) ~= nil
    and fld(dg, "home") == 0,
    "T48 a house with no buildings is reported, not silently skipped")

-- C4PRESET (emitted above the gates). This harness house has no
-- GetAIDifficulty binding, which is the fallback-to-medium path.
CAP = {}
captureTo(10800)
local pg = c4diagFor("C4PRESET", "A")
T(pg ~= nil, "T49 C4PRESET emitted above the gate")
if pg then
    T(pg:find("difficulty=unbound", 1, true) ~= nil,
        "T50 unbound difficulty reported distinctly (not 'nil', not 'error')")
    T(pg:find("preset=medium", 1, true) ~= nil,
        "T51 unbound difficulty falls back to the global preset")
    T(pg:find("state=ON", 1, true) ~= nil and pg:find("R=18", 1, true) ~= nil,
        "T52 enabled house reports state=ON and a resolved intruderR")
    T(pg:find("diverge=false", 1, true) ~= nil,
        "T53 inner and outer preset agree -> diverge=false")
end

-- =========================================================================
-- T54-T63: M2-C4 ADAPTATION - Act -> Observe result -> Adapt.
-- Severity re-evaluates the CURRENT threat every tick; that is
-- re-evaluation, not adaptation. Loop under test:
--   Act      Tier 1 drafts idle defenders onto intruders (existing)
--   Observe  a FINISHED assignment resolves to a win or a loss
--   Adapt    2 consecutive losses arm a pull of distant marchers
--
-- Read the loop from ADAPTSTATE (census-gated, per-house ledger) and from
-- the issued MoveTo. The ADAPT event line is cooldown-gated
-- (adaptUntil = frame + defenseEvery), so it need NOT land on a census
-- frame -- asserting it there would be sampling luck, not a contract.
-- fld() returns a NUMBER for numeric fields; only 'armed' is a string.
-- =========================================================================
step(5) -- restart clears the ledger (officerReset) and the latch
for _, u in ipairs(WORLD) do if u:IsAlive() then kill(u) end end
mkBuilding("A", "NAHAND", 100, 100, 1.0)
local h1 = mkUnit("A", "HTNK", "unit", 101, 101, true)
local h2 = mkUnit("A", "HTNK", "unit", 102, 102, true)
local foe = mkUnit("P", "HTNK", "unit", 103, 103, true)
runTo(35)
-- h2 is 2 cells from the intruder, h1 four. Tier 1 is nearest-first
-- (pre-existing behaviour), so h2 is the one that gets drafted.
T(#h2.attacks >= 1 and h2.attacks[1].target == foe,
    "T54 Act: nearest idle defender drafted onto the intruder")

-- T55: both alive -> the episode is unresolved: w and l must stay 0.
captureCensus()
local as = adaptState()
T(as ~= nil and fld(as, "w") == 0 and fld(as, "l") == 0
    and fld(as, "streak") == 0,
    "T55 an unfinished exchange counts as neither win nor loss")

-- T56: intruder dies, defender lives -> WIN, streak untouched.
kill(foe)
runTo(70)
captureCensus()
as = adaptState()
T(as ~= nil and fld(as, "w") == 1 and fld(as, "l") == 0,
    "T56 one engagement won, counted ONCE for the whole squad")
T(contactCount(foe._id, "WIN") == 1,
    "T56b the WIN is announced ONCE, not once per defending unit")

-- From here on the frame cursor is already at a census frame (1800), so every
-- step must move FORWARD. `runTo(n)` with n < CURRENT_FRAME is a silent
-- no-op loop -- that bug cost several iterations of this section.
local function adv(n) runTo(CURRENT_FRAME + n) end

-- T57: two losses arm the adaptation. Two rules make a loss observable:
--   * the DEFENDER must die while the intruder is still alive (once the
--     intruder dies the assignment resolves, and a later defender death is
--     deliberately not a failure);
--   * exactly ONE idle defender may exist, otherwise Tier 1 drafts the
--     nearest one and the unit under test never holds a record.
kill(h1); kill(h2)          -- remove the section's original pair
adv(30)                     -- let the previous world settle one scan
local dA = mkUnit("A", "HTNK", "unit", 101, 101, true)
local foe2 = mkUnit("P", "HTNK", "unit", 103, 103, true)
adv(60)
T(#dA.attacks >= 1, "T57a the lone defender holds the assignment")
kill(dA)                    -- defender died, intruder alive -> LOSS 1
adv(60)
captureCensus()
as = adaptState()
T(as ~= nil and fld(as, "l") == 1 and fld(as, "streak") == 1
    and fld(as, "armed") == "false",
    "T57b LOSE #1 -> streak=1 but NOT armed (single loss is not a signal)")
-- LOSS 2 must land on a DIFFERENT contact. Tier 1 always feeds the first
-- (nearest, lowest-id) intruder, so the previous one is removed first --
-- otherwise dB is also drafted onto it and the per-contact dedup correctly
-- refuses to call that a second loss.
kill(foe2)
adv(30)
local foe3 = mkUnit("P", "HTNK", "unit", 104, 104, true)
local dB = mkUnit("A", "HTNK", "unit", 101, 101, true)
adv(60)
kill(dB)                    -- LOSS 2 on foe3 -> streak = 2
adv(60)
captureCensus()
as = adaptState()
T(as ~= nil and fld(as, "l") == 2 and fld(as, "streak") == 2
    and fld(as, "armed") == "true",
    "T57 two losses on separate contacts arm the adaptation (streak=2)")

-- T57c: two defenders dying on ONE intruder is ONE lost contact, not two.
-- Without the per-contact dedup on the loss side, streak would reach 2 inside
-- a single fight and arm the adaptation on one failed engagement.
kill(foe3)
adv(30)
local foe4 = mkUnit("P", "HTNK", "unit", 106, 106, true)
local dC = mkUnit("A", "HTNK", "unit", 101, 101, true)
local dD = mkUnit("A", "HTNK", "unit", 101, 103, true)
adv(60)
local held = 0
if #dC.attacks > 0 then held = held + 1 end
if #dD.attacks > 0 then held = held + 1 end
T(held >= 1, "T57c both defenders were drafted onto the same intruder")
local lBefore = fld(adaptState() or "", "l")
kill(dC); kill(dD)          -- both die, the intruder stays alive
adv(60)
as = adaptState()
T(as ~= nil and fld(as, "l") == lBefore,
    "T57d two deaths on one contact count as ONE loss, streak unchanged")
T(contactCount(foe4._id, "LOSE") == 1,
    "T57e that squad wipe is announced ONCE, not once per defender")

-- T58-T60: the pull takes only DISTANT non-attacking units. The 25-cell
-- floor is proven by what got ORDERED, not by a counter on a line that
-- is not sampled every frame.
adv(40)
local farMarcher = mkUnit("A", "HTNK", "unit", 300, 300, false)
farMarcher._attacking = false
local nearMarcher = mkUnit("A", "HTNK", "unit", 104, 104, false)
nearMarcher._attacking = false
adv(60)
T(movesTo(farMarcher, 100, 100),
    "T58 Act->Adapt: the distant marcher is pulled to the base anchor")
T(not movesTo(nearMarcher, 100, 100),
    "T59 the near marcher fails the 25-cell floor and is left alone")
captureCensus()
as = adaptState()
T(as ~= nil and fld(as, "totPulls") >= 1,
    "T60 the ledger records that the adaptation fired at least once")

-- T61-T63: a WIN stands the adaptation down. foe2 must die first: while a
-- live intruder remains, Tier 1 correctly defends against IT, so expecting a
-- draft onto the newcomer would be asserting the wrong thing.
kill(foe2); kill(foe3); kill(foe4)
adv(60)
local dW = mkUnit("A", "HTNK", "unit", 101, 101, true)
local foe5 = mkUnit("P", "HTNK", "unit", 105, 105, true)
adv(60)
local engaged = false
for _, at in ipairs(dW.attacks) do if at.target == foe5 then engaged = true end end
T(engaged, "T61 a fresh defender is drafted onto the new threat")
adv(60)
kill(foe5)     -- WIN -> streak reset
adv(60)
captureCensus()
as = adaptState()
T(as ~= nil and fld(as, "streak") == 0 and fld(as, "armed") == "false",
    "T62 a win resets the streak and disarms the adaptation")
T(as ~= nil and fld(as, "w") == 2,
    "T63 both won engagements counted once each (no squad-size inflation)")
-- Non-vacuity guard: this contact resolves at the very end of the run, after
-- every captureCensus() swap. If the event recorder had been clobbered by the
-- census (the bug this guards), it would report 0 here and T56b/T57e would
-- still be passing on events recorded only before frame 1800.
T(contactCount(foe5._id, "WIN") == 1,
    "T63b the final WIN is still recorded, so the event recorder survived every census swap")

-- =========================================================================
-- T64: interleaved resolution inside ONE cleanup sweep.
-- The loop resolves every defence record in a single `pairs(defenseState)`
-- sweep keyed by DEFENDER id, so the visiting order depends on the hash of
-- those numbers. With a single "last target" slot as the dedup memory, the
-- order
--     [d1 -> t1]  [d2 -> t2]  [d3 -> t1]
-- credits t1 twice: d2 overwrites the slot with t2, so d3 no longer sees t1 as
-- settled. Observed live as w=2 then w=4 for one intruder inside one frame.
--
-- The order is NOT controllable from a test, so this asserts the INVARIANT
-- ("one target id yields at most one credit") across many id sets instead of
-- betting on one lucky ordering. Killing both intruders in the SAME tick is
-- what forces every record to resolve in a single sweep. A single id set is
-- not enough -- the first version of this test passed against the buggy code.
-- =========================================================================
local seeds, seedOK, seedBad, seedThin = 16, 0, 0, 0
for seed = 1, seeds do
    step(5)                   -- backwards frame => officerReset, w back to 0
    for _, u in ipairs(WORLD) do if u:IsAlive() then kill(u) end end
    mkBuilding("A", "NAHAND", 100, 100, 1.0)
    -- Equidistant and identical, so neither intruder outranks the other:
    -- Tier 1 is nearest-first, and an off-centre pair lets the closer one
    -- absorb the whole defender pool, leaving the second with no record at
    -- all -- which cannot exercise the interleaving.
    local ia = mkUnit("P", "HTNK", "unit", 103, 100, true)
    local ib = mkUnit("P", "HTNK", "unit", 97, 100, true)
    local gs = {}
    for k = 1, 4 do gs[k] = mkUnit("A", "HTNK", "unit", 100, 100 + k, true) end
    adv(60)
    local onA, onB = 0, 0
    for _, g in ipairs(gs) do
        for _, at in ipairs(g.attacks) do
            if at.target == ia then onA = onA + 1 end
            if at.target == ib then onB = onB + 1 end
        end
    end
    if onA == 0 or onB == 0 or (onA + onB) < 3 then
        seedThin = seedThin + 1 -- cannot interleave: <3 records over <2 targets
    else
        local evBefore = #CONTACT_EVENTS
        kill(ia); kill(ib)     -- both die in the SAME tick => one sweep
        adv(60)
        local ca, cb = 0, 0
        for i = evBefore + 1, #CONTACT_EVENTS do
            local ev = CONTACT_EVENTS[i]
            if ev.outcome == "WIN" then
                if ev.intruder == tostring(ia._id) then ca = ca + 1 end
                if ev.intruder == tostring(ib._id) then cb = cb + 1 end
            end
        end
        -- The contract: one target id yields AT MOST one credit.
        if ca <= 1 and cb <= 1 then
            seedOK = seedOK + 1
        else
            seedBad = seedBad + 1
        end
    end
end
T(seedOK >= 8, "T64a enough id sets exercised the interleaved sweep ("
    .. seedOK .. " conclusive, " .. seedThin .. " thin)")
T(seedBad == 0, "T64b no id set ever credits one intruder twice "
    .. "(violations: " .. seedBad .. ")")

-- =========================================================================
-- T65-T70: Act-side DIAGNOSTIC counters (observability only).
--
-- These assert that the C4DIAG counters COUNT what they claim to count. They
-- deliberately assert NO gameplay contract: the allocation policy, severity,
-- sevAssignR, the claim arbiter and orderAttack are all unchanged, and the
-- production predicates are reused verbatim by the diagnostic pass.
--
-- The census only emits on a census frame, so every case below ends with
-- captureCensus() and reads the resulting C4DIAG line.
-- =========================================================================
-- Newest C4DIAG for the house (c4diagFor returns the FIRST match, which is
-- enough after a CAP reset but wrong if a capture ever spans two scans).
local function c4diagLast(house)
    local pre = "[SMARTAI][C4DIAG] frame="
    local found
    for _, l in ipairs(CAP) do
        if l:sub(1, #pre) == pre and l:find("house=" .. house .. " ", 1, true) then
            found = l
        end
    end
    return found
end
-- Numeric field out of the C4DIAG line, nil-safe.
local function actFld(key)
    return fld(c4diagLast("A") or "", key)
end
local function actRej(key)
    local line = c4diagLast("A") or ""
    local v = line:match(key .. "={([^}]*)}")
    return v
end

-- --- T65: HARVESTER / MCV / ARTILLERY land in ownNonGround, not kindRej ----
-- All four are created as kind="unit", so they all PASS the production kind
-- predicate (a harvester is a ground vehicle) and are excluded by the
-- separate type gates. That distinction is the point of ownNonGround.
step(5)
for _, u in ipairs(WORLD) do if u:IsAlive() then kill(u) end end
mkBuilding("A", "NAHAND", 100, 100, 1.0)
local gH = mkUnit("A", "HARV", "unit", 101, 100, true)
local gM = mkUnit("A", "YMCV", "unit", 102, 100, true)
local gR = mkUnit("A", "V3", "unit", 103, 100, true)
local gT = mkUnit("A", "HTNK", "unit", 104, 100, true)
mkUnit("P", "HTNK", "unit", 106, 100, true)      -- the intruder
adv(60)
captureCensus()
T(actFld("ownTotal") == 4,
    "T65a ownTotal counts every own unit Act examined")
T(actFld("ownIdle") == 4,
    "T65b ownIdle counts the units that passed the idle gate")
T(actFld("ownNonGround") == 3,
    "T65c HARV+MCV+ART are counted as ownNonGround, not as kind rejections")
T(actFld("ownKindRejected") == 0,
    "T65d a ground harvester/MCV/artillery is NOT a kind rejection")
local ng = actRej("actNonGroundRej") or ""
T(ng:find("HARV=1", 1, true) ~= nil and ng:find("MCV=1", 1, true) ~= nil
    and ng:find("ART=1", 1, true) ~= nil,
    "T65e the non-ground histogram separates HARV / MCV / ART")

-- --- T66/T67: eligible vs inAssignR are different questions --------------
-- One tank right next to the intruder, one tank 60 cells away. Both are
-- eligible (own, idle, ground combat kind, positioned); only the near one is
-- inside sevAssignR. eligible=2, inAssignR=1 is the whole distinction.
step(5)
for _, u in ipairs(WORLD) do if u:IsAlive() then kill(u) end end
mkBuilding("A", "NAHAND", 100, 100, 1.0)
local n1 = mkUnit("A", "HTNK", "unit", 102, 100, true)   -- ~4 cells from intruder
local f1 = mkUnit("A", "HTNK", "unit", 160, 100, true)   -- ~54 cells away
captureCensus(function() mkUnit("P", "HTNK", "unit", 106, 100, true) end)
T(actFld("ownEligible") == 2,
    "T66 ownEligible counts candidates that cleared every gate before distance")
T(actFld("inAssignR") == 1,
    "T67 an eligible unit outside sevAssignR is eligible but NOT inAssignR")

-- --- T68: one defender, three intruders, counted once ---------------------
-- Tier 1 re-evaluates every own unit once per intruder. A naive mirror would
-- report inAssignR=3 here. The census tallies each unit once.
step(5)
for _, u in ipairs(WORLD) do if u:IsAlive() then kill(u) end end
mkBuilding("A", "NAHAND", 100, 100, 1.0)
local one = mkUnit("A", "HTNK", "unit", 103, 100, true)
captureCensus(function()
    mkUnit("P", "HTNK", "unit", 105, 100, true)
    mkUnit("P", "HTNK", "unit", 105, 102, true)
    mkUnit("P", "HTNK", "unit", 105, 104, true)
end)
T(actFld("ownEligible") == 1 and actFld("inAssignR") == 1,
    "T68 one defender near three intruders is counted once, not three times")

-- --- T69 + T70: the real tryClaim / orderAttack results are reported ------
-- claimTick is declared INSIDE Update, so it is per-tick: the Act block is the
-- first consumer in a tick, which is why a plain first claim never fails. The
-- reachable path is a claim that SUCCEEDS while the ORDER fails -- takenDef is
-- then not set, so the next intruder retries the same unit and the real
-- tryClaim refuses it. No mocking beyond the engine refusing the order.
step(5)
for _, u in ipairs(WORLD) do if u:IsAlive() then kill(u) end end
mkBuilding("A", "NAHAND", 100, 100, 1.0)
local stub = mkUnit("A", "HTNK", "unit", 102, 100, true, { attackFails = true })
captureCensus(function()
    mkUnit("P", "HTNK", "unit", 105, 100, true)
    mkUnit("P", "HTNK", "unit", 105, 102, true)
end)
T(actFld("orderFail") >= 1,
    "T69a the existing orderAttack failure is reported in orderFail")
T(actFld("claimDenied") >= 1,
    "T69b the existing tryClaim refusal on the retry is reported in claimDenied")
T(actFld("inAssignR") >= 1,
    "T69c the refused candidate was still counted as inAssignR (it was tried)")

print(string.format("DEFENSE-HARNESS done=%d failed=%d", passed + failed, failed))
os.exit(failed == 0 and 0 or 1)
