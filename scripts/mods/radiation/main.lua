-- ===========================================================================
-- RADIATION -- weather hazard mod for LuaAPI (Yuri's Revenge 1.001)
--
-- Goal: during a Radiation condition the player must be able to TELL there is
-- radiation, and infantry caught in the open must be punished for it. The
-- garrison is the answer, so the warning has to arrive before the harm.
--
-- Cycle:  IDLE -> WARNING -> ACTIVE -> RECOVERY -> IDLE
--   WARNING  one per second from 10s, then a 3 / 2 / 1 count, then impact
--   ACTIVE   tick damage + hold the irradiated ground
--   RECOVERY the cloud dissipates
--
-- Ground colour is deliberately NOT drawn by this mod. The engine owns the
-- picture: writing CellClass::RadLevel makes the engine apply the RadSite
-- warhead to the cell ([Radiation] RadSiteWarhead=RadSite) and build the green
-- Rad Site itself. Two earlier attempts to build a RadSiteClass from LuaAPI
-- were removed - they faulted the renderer twice at 0xC0000005 / 0x71C0E752.
--
-- API used, all verified against this build:
--   House.GetPlayer / World.GetUnits / unit:GetOwner,GetPosition,GetTypeName,
--   GetHealth / unit:TakeDamage(dmg, warhead) / World.GetCellRadLevel /
--   World.SetCellRadLevel / World.IsCellRadiated / Engine.WarheadExists /
--   Engine.PrintMessage / Input.WasKeyPressed
-- ===========================================================================

-- util is NOT a global. It comes from the shared framework module, and the
-- `require` is load-bearing: without it every util.is_alive call raised
-- "attempt to index a nil value (global 'util')" and Mod.Update died on the
-- first frame that reached a unit.
local util = require("framework.util")

local Mod = {}

-- --------------------------------------------------------------------------
-- Configuration
-- --------------------------------------------------------------------------
local CFG = {
    warnFrames    = 720,    -- 12s of warning before the blast
    activeFrames  = 1500,   -- 25s of active radiation
    recoveryFrames= 300,    -- 5s  of dissipating
    firstDelay    = 900,    -- no hazard on the match-start infantry
    gapFrames     = 3000,   -- quiet period between events

    tickEvery     = 15,     -- sweep cadence, in frames
    blastRadius   = 6,      -- cells

    announceSecs  = 10,     -- "10 seconds until it hits"
    finalCountSecs= 3,      -- then 3 / 2 / 1

    warhead       = "RadBeamWarhead",
    -- SetCellRadLevel is a DELTA, not an assignment. Holding a level means
    -- writing want-current, otherwise every sweep adds the target again and the
    -- cell runs far past RadLevelMax.
    groundLevel   = 500,    -- = RadLevelMax, the value the Desolator's own
                            -- RadEruptionWeapon uses
    ground        = true,

    -- Never irradiated: these are the units a player cannot move out of the way
    -- and would only feel like a bug.
    exempt        = { AGENT=true, ENGR=true, MCV=true, MCVB=true, THIEF=true },
    damage        = 1,
    affected      = "player",   -- "player" | "all"
}

local FORCE_KEY   = 0x75   -- VK_F6: arm a hazard now, for testing
local DESO_KEY    = 0x78   -- VK_F9: watch the Desolator (temporary diagnostic)

local ST_IDLE, ST_WARNING, ST_ACTIVE, ST_RECOVERY = 0, 1, 2, 3
-- Indexed explicitly. A literal { "IDLE", "WARNING", ... } starts at 1, so
-- STATE_NAME[0] is nil and every logged label is shifted by one.
local STATE_NAME = { [0]="IDLE", [1]="WARNING", [2]="ACTIVE", [3]="RECOVERY" }

-- --------------------------------------------------------------------------
-- State
-- --------------------------------------------------------------------------
local S = {
    -- Must be the numeric ST_* value, not the string "IDLE". Comparing the
    -- string against ST_IDLE is false for every branch, and the whole cycle
    -- then silently never runs -- which is exactly what it did.
    state      = ST_IDLE,
    stateUntil = 0,
    nextEvent  = 0,
    started    = false,
    targets    = {},
    pinned     = {},
    lastGround  = {},
    forced     = false,
    ramp        = 0,
}

local stats = { events = 0, hits = 0, damage = 0, exempted = 0, seen = 0 }

local house      = nil
local warheadOK  = false
local announced  = nil      -- last countdown second spoken
local struck     = false    -- Desolator strike already done this event
local desoLast   = nil
local desoUntil  = 0

-- --------------------------------------------------------------------------
-- Helpers
-- --------------------------------------------------------------------------
local function say(msg)
    pcall(Engine.PrintMessage, msg)
    print("[RAD] " .. msg)
end

local function houseSide(h)
    return h == (House.GetPlayer())
end

local function isOpenInfantry(u)
    if not util.is_alive(u) then return false end
    local t = u:GetTypeName()
    if not t or t:sub(1, 1) ~= "E" then return false end   -- E-prefix = infantry
    if CFG.exempt[t] then return false end
    -- Garrisoned foot is removed from TechnoClass::Array by the engine, so a
    -- unit we can see here is in the open. Measured, not assumed.
    return true
end

local function enter(state, untilFrame)
    S.state, S.stateUntil = state, untilFrame
    if state == ST_WARNING then announced = nil end
    if state == ST_ACTIVE then struck = false end
    print(string.format("[RAD] STATE %s until f=%d", STATE_NAME[state], untilFrame))
end

-- Pick the cells the cloud will sit on, and keep them pinned for the window.
-- A fixed coordinate is useless in a game where units move, so this is
-- re-evaluated at the start of every phase and then held still.
local function chooseTargets(frame)
    local units = World.GetUnits()
    if not units then return {} end

    local mine, theirs = {}, {}
    for _, u in ipairs(units) do
        if isOpenInfantry(u) then
            local p = u:GetPosition()
            if p then
                local rec = { x = p.x, y = p.y }
                if houseSide(u:GetOwner()) then mine[#mine + 1] = rec
                else theirs[#theirs + 1] = rec end
            end
        end
    end

    -- The player's own crowded base is the point of the warning; ranking by
    -- isolation alone used to pick scattered enemies and skip it.
    local pick = (#mine > 0) and mine or theirs
    local seen, out = {}, {}
    for _, t in ipairs(pick) do
        local k = t.x .. "," .. t.y
        if not seen[k] then
            seen[k] = true
            out[#out + 1] = t
        end
        if #out >= CFG.blastRadius then break end
    end
    return out
end

-- --------------------------------------------------------------------------
-- Ground
--
-- Write the delta that brings each cell to the target level, and read back what
-- the engine actually holds so repeated sweeps do not compound.
local function paintGround(level)
    if not CFG.ground or not World.SetCellRadLevel then return 0 end
    if level < 1 then level = 1 end
    local painted = 0
    for _, t in ipairs(S.targets) do
        local cur = 0
        if World.GetCellRadLevel then
            local ok, v = pcall(World.GetCellRadLevel, t.x, t.y)
            if ok and type(v) == "number" then cur = v end
        end
        local delta = level - cur
        if math.abs(delta) >= 0.5 then
            local ok, res = pcall(World.SetCellRadLevel, t.x, t.y, delta)
            if ok and res then
                S.lastGround[#S.lastGround + 1] = { x = t.x, y = t.y }
            end
        end
        -- Count a cell as painted even when delta was ~0. Reporting only the
        -- writes made ground=0 for ticks where the cells were already at the
        -- target, which read as "nothing is painted" when it actually meant
        -- "nothing left to write".
        painted = painted + 1
    end
    return painted
end

local function scrubGround()
    if not World.SetCellRadLevel then return 0 end
    local seen, removed = {}, 0
    for _, t in ipairs(S.lastGround) do
        local k = t.x .. "," .. t.y
        if not seen[k] then
            seen[k] = true
            local cur = 0
            if World.GetCellRadLevel then
                local ok, v = pcall(World.GetCellRadLevel, t.x, t.y)
                if ok and type(v) == "number" then cur = v end
            end
            if cur > 0 then
                pcall(World.SetCellRadLevel, t.x, t.y, -cur)
                removed = removed + cur
            end
        end
    end
    S.lastGround = {}
    return removed
end

-- --------------------------------------------------------------------------
-- Damage
--
-- unit:TakeDamage takes a warhead NAME, and Techno_TakeDamage resolves it
-- through a silent fallback, so the name is always passed explicitly.
local function sweep(frame, level)
    local units = World.GetUnits()
    if not units then return 0 end

    local hits, dmg = 0, 0
    local scanned, passed, inRadius = 0, 0, 0   -- census: where the chain breaks
    for _, u in ipairs(units) do
        scanned = scanned + 1
        if isOpenInfantry(u) then
            passed = passed + 1
            local h = u:GetOwner()
            if (CFG.affected == "all") or (h == house) then
                local p = u:GetPosition()
                if p then
                    for _, t in ipairs(S.targets) do
                        local dx, dy = p.x - t.x, p.y - t.y
                        if (dx * dx + dy * dy) <= (CFG.blastRadius * CFG.blastRadius) then
                            local ok, hp = pcall(u.TakeDamage, u, CFG.damage * level, CFG.warhead)
                            if ok then
                                hits = hits + 1
                                dmg = dmg + 1
                            end
                            inRadius = inRadius + 1
                            break
                        end
                    end
                end
            end
        end
    end
    stats.hits, stats.damage = stats.hits + hits, stats.damage + dmg
    -- One census line per event. hits=0 on its own says nothing about WHY:
    -- it could be no infantry, the type filter, ownership, or the radius. These
    -- four numbers localise it immediately.
    if not S.censusLogged or frame % 600 == 0 then
        S.censusLogged = true
        local tc = {}
        for i = 1, math.min(4, #S.targets) do
            tc[#tc + 1] = string.format("(%d,%d)", S.targets[i].x, S.targets[i].y)
        end
        print(string.format(
            "[RAD] CENSUS f=%d scanned=%d infantry=%d inRadius=%d hits=%d targets=%d radius=%d at=%s",
            frame, scanned, passed, inRadius, hits, #S.targets, CFG.blastRadius,
            table.concat(tc, " ")))
    end
    if hits > 0 and hits <= 3 then
        say(string.format("Radiation is burning %d exposed infantry. Get them "
            .. "into a structure.", hits))
    end
    return hits
end

-- --------------------------------------------------------------------------
-- Desolator watch -- TEMPORARY diagnostic, F9. Not a feature.
--
-- The one thing never observed in this project: a RadSiteClass created BY THE
-- ENGINE. Every earlier probe inspected zones this mod built itself. Play as a
-- side that owns a DESO, press F9, then D to deploy its radiation; the log then
-- contains the first real dump of an engine-wired effect object.
-- --------------------------------------------------------------------------
-- Samples the cells AROUND the Desolator, not its own cell.
--
-- Two reasons, both learned the hard way:
--   * The eruption is a field: [RadEruptionWarhead] has CellSpread=10 and
--     CellInset=3, so the radiation covers an area and the Desolator's own cell
--     is not representative. Reading only that cell reported 0.00 while a
--     deployed Desolator was plainly sitting there.
--   * Change-only logging hid the deploy entirely. A deploy changes neither the
--     cell nor that one reading, so it produced no line at all. A throttled
--     heartbeat is kept so an event cannot pass silently again.
local DESO_SCAN = 12     -- cells scanned each way from the Desolator
                        -- widened from 5: the eruption's real footprint may be
                        -- larger than assumed, and a too-small scan reports a
                        -- clean 0.00 for a field that is plainly there
local DESO_BEAT = 30     -- frames between forced re-samples


-- Validate RadSiteHasZone instead of trusting it.
--
-- RadSiteHasZone reads the owner pointer at CellClass+0xF8, which is set on a
-- site's BASE cell only, not across its spread. So radSites=0 is ambiguous: it
-- can mean "no sites exist" or "the query cannot see them". RadSiteProbe is
-- independent of that - it reads the engine's RadSite array directly and
-- reports its count - so it tells the two apart.
local desoProbeBeat = 0

local function desoProbe(deso, p)
    if not World.RadSiteProbe then return end
    desoProbeBeat = desoProbeBeat + 1
    if desoProbeBeat % 10 ~= 1 then return end
    local ok, dump = pcall(World.RadSiteProbe, p.x, p.y)
    if ok then print("[DESO] PROBE " .. tostring(dump)) end
end

local function desoWatch(frame)
    if not house then return end
    local units = World.GetUnits()
    if not units then return end
    local deso
    for _, u in ipairs(units) do
        if util.is_alive(u) and u:GetTypeName() == "DESO" then deso = u break end
    end
    if not deso then
        if desoLast ~= "none" then
            desoLast = "none"
            print("[DESO] no DESO on the map (Iraq only; a Soviet DESO is refused "
                .. "by another House, so this watcher stays idle)")
        end
        return
    end
    local p = deso:GetPosition()
    if not p then return end

    -- Sweep the neighbourhood for the strongest reading.
    local best, bestAt, hot = 0.0, nil, 0
    local zones, zoneAt = 0, nil
    for dy = -DESO_SCAN, DESO_SCAN do
        for dx = -DESO_SCAN, DESO_SCAN do
            local x, y = p.x + dx, p.y + dy
            if x >= 0 and y >= 0 then
                if World.GetCellRadLevel then
                    local ok, v = pcall(World.GetCellRadLevel, x, y)
                    if ok and type(v) == "number" and v > best then
                        best, bestAt = v, { x = x, y = y }
                    end
                end
                if World.IsCellRadiated then
                    local ok, r = pcall(World.IsCellRadiated, x, y)
                    if ok and r then hot = hot + 1 end
                end
                -- The field that actually carries the picture. CellClass::RadLevel
                -- is now PROVEN to be damage-only: the control cell reads 500 and
                -- isRadiated=true and still shows no green. The Desolator's
                -- irradiation also reads 0.00 there, so its green lives in a
                -- RadSiteClass and its light. This is the field to watch.
                if World.RadSiteHasZone then
                    local ok, hz = pcall(World.RadSiteHasZone, x, y)
                    if ok and hz then
                        zones = zones + 1
                        if zoneAt == nil then zoneAt = { x = x, y = y } end
                    end
                end
            end
        end
    end

    local sig = string.format("%d,%d|%.2f|%d|%d", p.x, p.y, best, hot, zones)
    local beat = (frame % DESO_BEAT) == 0
    if sig == desoLast and not beat then return end
    desoLast = sig

    print(string.format(
        "[DESO] f=%d cell=(%d,%d) scan=%d maxRadLevel=%.2f at=(%d,%d) hotCells=%d radSites=%d firstSite=(%d,%d) hp=%s",
        frame, p.x, p.y, DESO_SCAN * 2 + 1, best,
        bestAt and bestAt.x or -1, bestAt and bestAt.y or -1,
        hot, zones, zoneAt and zoneAt.x or -1, zoneAt and zoneAt.y or -1,
        tostring(deso:GetHealth())))

    desoProbe(deso, p)

    -- The engine holds a real RadSite (radVecCount=1) that is not on any cell
    -- we would think to ask about, so enumerate the array instead. This is the
    -- measured reference for a site the ENGINE built: base cell, spread, and
    -- the light source that actually tints the ground.
    if World.RadSiteList then
        local ok2, lst = pcall(World.RadSiteList)
        if ok2 and type(lst) == "string" and lst ~= "" and lst ~= " [SEH]" then
            print("[DESO] LIST" .. lst)
        end
    end

    -- CONTROL. Read the same two bindings on a cell this mod is KNOWN to hold
    -- at RadLevel 500. If the control reads 500 and the Desolator's area reads
    -- 0, the probe is sound and the Desolator's green is simply not stored in
    -- RadLevel. If the control also reads 0, the probe itself is broken and
    -- every radmax number reported so far is void.
    if #S.targets > 0 and World.GetCellRadLevel then
        local t = S.targets[1]
        local ok, v = pcall(World.GetCellRadLevel, t.x, t.y)
        local ctrl = (ok and type(v) == "number") and v or -1
        local rad = false
        if World.IsCellRadiated then
            local ok2, r = pcall(World.IsCellRadiated, t.x, t.y)
            if ok2 then rad = r and true or false end
        end
        print(string.format("[DESO] CONTROL f=%d cell=(%d,%d) radLevel=%.2f isRadiated=%s",
            frame, t.x, t.y, ctrl, tostring(rad)))
    end

end

-- --------------------------------------------------------------------------
-- Phases
-- --------------------------------------------------------------------------
local function startEvent(frame)
    stats.events = stats.events + 1
    S.targets = chooseTargets(frame)
    if #S.targets == 0 then
        print("[RAD] no open infantry in range; skipping this event")
        S.nextEvent = frame + CFG.gapFrames
        return
    end
    S.ramp = 0
    say(string.format("Radiation warning: %d second%s until it hits. %d "
        .. "infantry cluster(s) will be irradiated - garrison them or move them.",
        CFG.announceSecs, (CFG.announceSecs == 1) and "" or "s", #S.targets))
    enter(ST_WARNING, frame + CFG.warnFrames)
end

local function tick(frame)
    if not S.started then
        S.started = true
        S.nextEvent = frame + CFG.firstDelay
        print(string.format("[RAD] armed; first hazard f=%d", S.nextEvent))
        return
    end

    if S.state == ST_IDLE then
        if frame >= S.nextEvent then startEvent(frame) end
        return
    end

    if S.state == ST_WARNING then
        local left = S.stateUntil - frame
        local secs = math.ceil(left / 60)
        if left <= 0 then
            enter(ST_ACTIVE, frame + CFG.activeFrames)
            S.ramp = CFG.groundLevel
            say(string.format("RADIATION DETECTED - %d infantry cluster(s) hot. "
                .. "Infantry in the open will take attrition damage. "
                .. "Get them into a structure.", #S.targets))
        -- per-second countdown REMOVED 2026-09-29 (screen spam: Engine
        -- stacks every PrintMessage line instead of replacing, so 10..1
        -- piled up to ten banners plus start/impact/clear). Kept: the
        -- initial 10s warning (startEvent), RADIATION DETECTED at impact,
        -- dissipating at clear. Three banners per event instead of ~thirteen.
        -- (Revert: restore the elseif branch below.)
        end
        return
    end

    if S.state == ST_ACTIVE then
        if frame > S.stateUntil then
            local removed = scrubGround()
            print(string.format("[RAD] CLEAR f=%d groundRemoved=%.2f", frame, removed))
            say("Radiation cloud is dissipating.")
            S.targets, S.pinned = {}, {}
            enter(ST_RECOVERY, frame + CFG.recoveryFrames)
            return
        end
        if frame % CFG.tickEvery ~= 0 then return end
        -- Re-pick the targets EVERY sweep. Holding the list chosen at the start
        -- of the event is exactly the failure this file warns about: infantry
        -- walks, the pinned cells go empty, and inRadius drops to 0 while the
        -- mod reports hits=0 and looks broken. Census showed
        -- infantry=14 inRadius=0 for that reason.
        local fresh = chooseTargets(frame)
        if #fresh > 0 then
            S.targets = fresh
        end
        local hits = sweep(frame, 1)
        local painted = paintGround(S.ramp)
        if frame % 300 == 0 then
            local tc = {}
            for i = 1, math.min(4, #S.targets) do
                tc[#tc + 1] = string.format("(%d,%d)", S.targets[i].x, S.targets[i].y)
            end
            print(string.format("[RAD] ACTIVE f=%d hits=%d ground=%d/%d targets=%s",
                frame, hits, painted, #S.targets, table.concat(tc, " ")))
        end
        return
    end

    if S.state == ST_RECOVERY then
        if frame > S.stateUntil then
            enter(ST_IDLE, frame)
            S.nextEvent = frame + CFG.gapFrames
        end
    end
end

-- --------------------------------------------------------------------------
-- Public surface
-- --------------------------------------------------------------------------
function Mod.Update(frame)
    if not house then
        house = House.GetPlayer()
        if not house then return end
        if Engine.WarheadExists then
            warheadOK = Engine.WarheadExists(CFG.warhead) and true or false
        else
            warheadOK = false
        end
        print(string.format("[RAD] warhead=%s found=%s affected=%s ground=%s level=%d",
            CFG.warhead, tostring(warheadOK), CFG.affected,
            tostring(CFG.ground), CFG.groundLevel))

        -- Does the game read the rules file that Phobos keys live in?
        --
        -- Two runs produced "no green" and neither proved the file was even
        -- read, so a negative result stayed ambiguous. rulesmd.ini carries a
        -- deliberately visible key (ShowPlacementPreview), so the log states
        -- the precondition instead of assuming it.
        --
        -- Read-only: this only stats a file. It never writes to the game folder
        -- at runtime, which the engine would not expect and which would leave
        -- litter behind on every match.
        do
            local path = "rulesmd.ini"
            local probe = io and io.open and io.open(path, "rb")
            if not probe then
                print("[RAD] RULES probe=" .. path .. " FOUND=no"
                    .. "  (no rules file next to the DLL: any Phobos key in it"
                    .. " cannot be in effect, so no effect is not evidence)")
            else
                local body = probe:read("*a")
                probe:close()
                body = body or ""
                local detonate = body:find("Detonate", 1, true) ~= nil
                local preview  = body:find("ShowPlacementPreview", 1, true) ~= nil
                print(string.format(
                    "[RAD] RULES probe=%s FOUND=yes bytes=%d hasDetonate=%s hasPreviewKey=%s"
                    .. "  (previewKey AND a building ghost on screen => file IS read)",
                    path, #body, tostring(detonate), tostring(preview)))
            end
        end

        say("Radiation conditions are being monitored.")
    end

    local okF6, f6 = pcall(Input.WasKeyPressed, FORCE_KEY)
    if okF6 and f6 and S.state == ST_IDLE then
        S.forced = true
        S.nextEvent = frame + 30
        say("Manual radiation trigger (F6).")
    end

    local okF9, f9 = pcall(Input.WasKeyPressed, DESO_KEY)
    if okF9 and f9 then
        desoUntil = frame + 60 * 90    -- 90s: the last run ended 12s after F9
        desoLast = nil
        say("Desolator watch armed 30s (F9). Deploy it yourself and watch the log.")
    end
    if desoUntil > frame then desoWatch(frame) end

    tick(frame)
end

function Mod.Summary()
    print(string.format(
        "[RAD] SUMMARY events=%d hits=%d damage=%d exempted=%d",
        stats.events, stats.hits, stats.damage, stats.exempted))
    return stats
end

return Mod
