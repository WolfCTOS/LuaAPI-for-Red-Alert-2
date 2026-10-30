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
    gapFrames     = 900,    -- quiet period between events. Was 3000, which made a
                            -- full cycle ~80s, so a short match showed exactly one
                            -- event and looked like a broken mod.

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

    -- GREEN TILE TEST (2026-09-29). This is the one thing never actually tried:
    -- a WIDE zone. SetSpread computes SpreadInLeptons = Spread*256 + 0x80 and
    -- the glow radius comes from it, so the radius is our argument. Every prior
    -- attempt used spread=6, the Desolator's own default, which means the one
    -- obvious question - does the engine draw a wide green zone at all - was
    -- never asked. The Desolator's own eruption reached hotCells=349 of 625
    -- scanned cells, so the renderer does honour wide coverage.
    --
    -- OFF unless LUAAPI_RADSITE_NATIVE=1, so this cannot affect an ordinary
    -- run. Creating these sites faulted the renderer twice, so it stays opt-in.
    -- The engine paints its own RadSite green. Proven on 2026-09-30: a site
    -- created with spread=60 covered the whole map in vanilla green with
    -- lit=0 - no tint was ever written by us. So greenTint and
    -- World.RadSiteSetLight are not needed for colour and are left unused.
    -- What remains unsolved is the renderer fault, and the evidence says fewer
    -- sites is safer, so the cap starts at 1.
    greenSpread   = 60,      -- cells; one site of this radius already covers the map
    greenStep     = 2,       -- cell stride
    greenCap      = 1,       -- one site is enough to green the map; raise slowly
    -- Map-wide green is NOT solved. paintGreen() hand-builds RadSiteClass
    -- objects; they render, then Phobos faults at +0x6E0AA on the next frame
    -- because Intensity/Tint/Radiate were never initialised. Off by default:
    -- the orchestration path (strike) builds its sites through the engine and
    -- does not crash, so that is the one in use.
    green         = false,

    -- Map-scale green sweep. Replaces the disabled hand-built grid: the engine
    -- builds ONE site per detonation, so the map is covered by many detonations
    -- rather than by one wide site. radStep is the aim stride; blastSpread is the
    -- per-detonation damage radius (capped at 11, the warhead CellSpread limit).
    -- 163x163 at radStep=40 gives 5x5 = 25 detonations.
    radStep       = 40,
    -- Map-scale green sweep: build the site's PICTURE without damaging anything.
    --
    -- The Lua damage loop never touches vehicles - isOpenInfantry() requires an
    -- "E" type prefix - so what used to wreck tanks was the detonation itself: the
    -- radiation weapon carries a normal blast warhead. But that detonation is the
    -- only thing that makes the engine build a green RadSite, since the
    -- hand-built path is disabled by the Phobos fault. Passing suppressDamage
    -- zeroes WarheadTypeClass::Verses (11 armour types) for the duration of that
    -- one blast: the site is still created, nothing takes damage.
    --
    -- The timed radiation event is UNAFFECTED - infantry attrition still works.
    radNoDamage    = true,
    radGap        = 20,       -- frames between detonations
    greenTint     = 1000,
    -- Engine's own green: order a Desolator to fire. Needs a side that owns
    -- one, and needs units in range for it to shoot at.
    strike        = true,
    -- frames between Desolator re-orders (150 = 2.5s): each shot is one real
    -- engine-built site, so this is how fast the green grows
    strikeGap     = 3600,   -- frames between Desolator re-orders. 3600 = one blast
                               -- per minute, so F6 is a single event and not a
                               -- stream that keeps firing over the recording.

    -- Never irradiated: these are the units a player cannot move out of the way
    -- and would only feel like a bug.
    exempt        = { AGENT=true, ENGR=true, MCV=true, MCVB=true, THIEF=true },
    damage        = 1,
    affected      = "player",   -- "player" | "all"
}

-- Set the zone radius at runtime. Recreates the field so a new spread takes
-- effect immediately. Pure Lua - no build, no new binding.
function Mod.SetRadSpread(n)
    local v = tonumber(n)
    if not v or v < 1 then return false end
    CFG.greenSpread = math.floor(v)
    S.greenZones = {}
    print("[RAD] green spread = " .. CFG.greenSpread)
    return true
end

function Mod.GetRadSpread() return CFG.greenSpread end

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
local desoListBeat = 0        -- throttles the ENGINE-LIST dump to once a second

-- --------------------------------------------------------------------------
-- Helpers
-- --------------------------------------------------------------------------
  -- Silent mode. The mod is used for recording, and Engine.PrintMessage draws
  -- straight onto the screen - every warning, countdown and "HITS NOW" banner
  -- was landing in the video. With this on, the mod still does everything, it
  -- just says nothing where it can be seen. The log file keeps the detail.
  CFG.quiet       = true

  local function say(msg)
      if CFG.quiet then
          print("[RAD] " .. msg)
          return
      end
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
-- The unit that actually makes the green, used as the engine's own tool.
--
-- A Desolator is a UNIT carrying RadEruptionWeapon. When its weapon lands the
-- engine builds the RadSite and draws the green itself - the vanilla effect
-- this mod wants and cannot fake. Hand-building a RadSiteClass from LuaAPI
-- faulted the renderer twice; going through a real unit constructs nothing.
--
-- Constraint, learned the hard way: House.SpawnUnit only resolves types the
-- player's own house may build. Asking a British house for "DESO" returned 0.
-- So this works only on a side that owns a Desolator, and says so in the log
-- rather than failing quietly.
  local STRIKE_TYPE = "DESO"

  -- Radiation weapon handed to World.DetonateAt. The engine builds a proper
  -- green RadSiteClass on any radiation-bullet detonation, so this is the direct
  -- route and no Desolator is needed. If the ID is wrong the binding says so in
  -- the log (unknown weaponId / RadLevel=0 / no Projectile) instead of failing
  -- quietly - that was the failure mode this whole project kept hitting.
  CFG.detonateWeapon = "RadEruptionWeapon"

  -- CellSpread to use for the blast, in cells. The Desolator's own is 10. A
  -- YR 1.001 map is 128x128, so 150 comfortably covers it from anywhere. This
  -- overrides the warhead for one detonation only; the binding restores it.
  -- 0 = leave the weapon's own spread alone.
  -- CellSpread is a fixed 12 entry lookup table, valid 0-11. 150 indexed past the
  -- end of it and swept the whole map instead of one area. 11 is the maximum
  -- legal blast radius, and the mod fires many detonations to cover ground.
  CFG.blastSpread = 11

  -- Thunderstorm. Superweapon id, its own key, and OFF by default.
  --
  -- The civilian-building filter is NOT implemented yet. Until it is, a
  -- Thunderstorm will flatten civilian structures the way the real superweapon
  -- does, which is exactly the behaviour the player asked to avoid. So it is
  -- deliberately not wired to F6: F7 only, and the log says so out loud.
  -- Thunderstorm. Superweapon id, its own key.
  --
  -- The id is NOT "Thunderstorm" - that is the name the UI shows. Enumerated
  -- from the game itself (World.ListSuperWeapons), the real list is:
  --   NukeSpecial, IronCurtainSpecial, LightningStormSpecial, ChronoSphereSpecial,
  --   ChronoWarpSpecial, ParaDropSpecial, AmericanParaDropSpecial,
  --   PsychicDominatorSpecial, SpyPlaneSpecial, GeneticConverterSpecial,
  --   ForceShieldSpecial, PsychicRevealSpecial
  -- The Allied lightning strike is LightningStormSpecial.
  --
  -- WARNING: the civilian-building filter is NOT implemented. A Thunderstorm
  -- will flatten civilian structures exactly like the real superweapon does.
  -- Test on a throwaway save. The filter is the next piece of work.
  CFG.thunderKey     = 0x76          -- VK_F7 (0x76; 0x77 is F8, F9 is 0x78)
  -- DISABLED. The SuperClass::Launch route CRASHES the game.
  --
  -- Measured 2026-09-30: GameCreate<SuperClass> + Launch() faults inside gamemd
  -- at 0x006CAFA4 before "launched at" is ever logged. The lightning rules were
  -- read and written correctly first (spread=10 duration=180 hitDelay=10
  -- damage=250 separation=3), so the crash is in creating or launching the
  -- delivery object, not in the rules. SuperClass is a Phobos-extended class and
  -- almost certainly needs allocation and initialisation that GameCreate does not
  -- perform. Unverified path, so it stays off until that is actually known.
  -- ONE storm, not two. A house owns a single LightningStorm object and
  -- Launch() re-targets it, so a second launch replaces the first even
  -- 0.7s later - only the enemy base was ever visible. Coverage therefore
  -- comes from the SCATTER, not from two storms.
  --
  -- scatter (Rules.LightningCellSpread) is a plain integer controlling where
  -- strikes are placed, and is NOT the warhead CellSpread, so it has no
  -- 0-11 table limit. Strikes still only damage their own small radius,
  -- so a wide scatter covers a lot of ground without killing anything
  -- instantly - which is exactly what a 150 CellSpread could not do.
  -- Documented in the official rulesmd.ini:
  --   LightningCellSpread=10    "how far away random bolts can go (n by n square)"
  --   LightningHitDelay=10      "how often the direct target gets hit in frames"
  --   LightningScatterDelay=5   "frame delay between random bolts - DO NOT DECREASE"
  --   LightningSeparation=3     "city-block distance in cells between clouds/bolts"
  --   LightningDeferment=250    "frames between announcement and commencement"
  --
  -- CellSpread is an n-by-n SQUARE around the aim cell, not a radius - which is
  -- exactly why the stock value of 10 missed bases 25 cells away, and why
  -- raising it to 40 still missed: 40x40 centred on (83,82) does not contain
  -- (78,102) or (88,61).
  --
  -- Map-wide coverage therefore comes from a SWEEP of aim cells sized to the
  -- real map. Two numbers must be EQUAL for the coverage to be gapless:
  --
  --   CFG.thunderScatter - Rules.LightningCellSpread, the n-by-n square of bolts
  --                        the storm places around its aim cell;
  --   CFG.thunderStep    - how far the sweep moves between aim cells.
  --
  -- stride == scatter means each square is exactly covered by the next one.
  -- At the stock 10 on this 163x163 map that is 17x17 = 289 aim cells, measured
  -- at 9.25s each: about 44 minutes for one pass. At 30 it is 6x6 = 36 cells,
  -- about 5 minutes, with no gaps.
  --
  -- LightningCellSpread is a plain int with no 0-11 lookup table - that limit
  -- applies to the warhead's CellSpread, a different field. The applied value is
  -- read back from the engine and a mismatch is logged, because a silent clamp
  -- would leave the tiling assumption false.
  --
  -- BOLT DENSITY is the other half, and it is what produced visible gaps with
  -- stride == scatter == 30. The squares tiled correctly, but the storm only
  -- placed duration/hitDelay = 180/10 = 18 bolts into a 30x30 = 900 cell square:
  -- one bolt per 50 cells, about 26% coverage. Bolts are placed at RANDOM points
  -- inside the square, so a sparse square is patchy no matter how well the aim
  -- cells tile.
  --
  -- The limit on storm size is NOT CellSpread. It is LightningScatterDelay:
  --   "frame delay between random bolts -- DO NOT DECREASE -- PERFORMANCE HIT"
  -- so the random bolts are capped at duration/ScatterDelay, and with
  -- separation=3 the storm can only span about sqrt(bolts)*3 cells:
  --
  --   duration= 180  bolts<=  36  span ~18 cells   (stock: tiny)
  --   duration= 600  bolts<= 120  span ~33 cells
  --   duration= 900  bolts<= 180  span ~40 cells
  --   duration=1800  bolts<= 360  span ~57 cells   <- chosen
  --
  -- At a ~57 cell span a 163x163 map needs 3x3 = 9 aim cells instead of 6x6 = 36.
  -- hitDelay is lowered to 1 so the direct target is hit every frame, using the
  -- whole duration rather than a fraction of it. ScatterDelay stays at the stock
  -- 5, which the INI explicitly warns must not be decreased.
  CFG.thunderScatter     = 60       -- Rules.LightningCellSpread (stock 10)
  CFG.thunderStep        = 60       -- sweep stride; must equal the scatter
  CFG.thunderHitDelay    = 1        -- frames between target strikes (stock 10)
  CFG.thunderDuration    = 1800     -- storm frames: 30s, the span driver
  CFG.thunderDamage      = 250      -- per strike (stock 250)
  CFG.thunderSeparation  = 3        -- min cells between bolts (stock 3)

  -- The gap MUST exceed the storm's own lifetime. A run with gap=20 against a
  -- 150 frame storm produced 21 Launch calls and exactly ONE visible storm:
  -- SuperClass::Launch is silently dropped while a storm is already in
  -- progress, and SuperClass has no in-progress predicate to query. The window
  -- is therefore duration + deferment, not a value picked for looks.
  CFG.thunderGap         = 1900    -- frames between aim cells. MUST exceed
                                       -- CFG.thunderDuration (1800), or the next
                                       -- Launch re-targets the storm still running
                                       -- and the new aim cell is silently dropped.
                                       -- These two are a pair: changing one
                                       -- without the other loses whole aim cells.

  CFG.thunderstorm   = true          -- F7 fires the ENGINE-OWNED storm
  CFG.thunderId      = "LightningStormSpecial"
  CFG.thunderType    = 2            -- SuperWeaponType::LightningStorm (enum, not a name)

  -- Removed: an earlier block of duplicates (thunderSpread=150, strikeSpread=150,
  -- thunderHitDelay=2, thunderSeparation=0, thunderDuration=600) that shadowed the
  -- values above. Those were the source of the whole-map Game Over - strikeSpread
  -- was the warhead CellSpread, which is a 12 entry table limited to 0-11.

  -- Set by radStrike() to the current Desolator's IsAttacking accessor, so the
  -- ACTIVE phase can tell whether the unit is already busy and must be left
  -- alone. Declared up here because radStrike and tick are separate functions.
  local unit_busy_check


-- Find a house that is not the player's, preferring the civilian one. A civilian
-- is the right stand-in for a target: the Desolator will fire on it, and unlike
-- an AI unit it will not come back through the map, garrison anything or start
-- a war. Indexes are 0-based and the civilian house is not at a fixed index, so
-- it is found by name and cached.
local function findTargetHouse()
    if S.targetHouse ~= nil then
        if S.targetHouse == false then return nil end
        return S.targetHouse
    end
    S.targetHouse = false
    if not House or not House.GetCount or not House.GetByIndex then return nil end
    local okN, count = pcall(House.GetCount)
    if not okN or type(count) ~= "number" then return nil end
    local fallback
    for i = 0, count - 1 do
        local okH, h = pcall(House.GetByIndex, i)
        if okH and h and h ~= house then
            local okG, grew = pcall(h.GetName, h)
            local nm = (okG and type(grew) == "string") and string.lower(grew) or ""
            if string.find(nm, "civ", 1, true) then
                S.targetHouse = h
                return h
            end
            if not fallback then fallback = h end
        end
    end
    S.targetHouse = fallback or false
    return S.targetHouse
end

-- Give the Desolator something to shoot at, without introducing an enemy.
--
-- The Desolator's eruption only happens when it detonates on a target inside
-- CellSpread 10 / CellInset 3, so with no enemy on the map it holds fire
-- forever and no radiation site is ever built. Spawning a civilian gives it a
-- legal target. Type IDs are tried in order and whichever one the game actually
-- accepts is reported in the log, rather than assuming one.
local CIV_TYPES = { "CIV", "ALLCIV", "SOVCIV", "E1", "E2", "E3", "E4" }

local function ensureTarget(x, y)
    if S.targetUnit then
        if util.is_alive(S.targetUnit) then return S.targetUnit end
        S.targetUnit = nil
    end
    if S.noTargetTried then return nil end
    local th = findTargetHouse()
    if not th or not th.SpawnUnit then
        S.noTargetTried = true
        print("[RAD] STRIKE: no second house in the array, so there is nobody to"
            .. " target and no site can be built. Place one enemy unit nearby"
            .. " instead.")
        return nil
    end
    for _, id in ipairs(CIV_TYPES) do
        local ok, made = pcall(th.SpawnUnit, th, id, 1, x, y, 0, true, "")
        if ok and type(made) == "number" and made >= 1 then
            S.targetType = id
            S.noTargetTried = true
            print(string.format("[RAD] STRIKE: spawned target '%s' at (%d,%d)"
                .. " so the Desolator has something to detonate on. This may be a real enemy unit; delete it after the test.", id, x, y))
            return true
        end
    end
    S.noTargetTried = true
    print("[RAD] STRIKE: could not spawn a civilian target (tried "
        .. table.concat(CIV_TYPES, ", ") .. "). Place one enemy unit near the"
        .. " hazard instead.")
    return nil
end

local function radStrike(frame)
        if not house then return false end
        local t = S.targets[1]
        if not t then return false end
        local sx, sy = t.x, t.y + 1

        -- PRIMARY: ask the engine directly. One bullet, one detonation, at the
        -- hazard cell - the engine and Phobos build the site themselves. This is
        -- the whole point of World.DetonateAt and it needs no Desolator, no
        -- target unit and no line of sight.
        if (World.DetonateAtFromUnit or World.DetonateAt) then
            -- Two ways to fire, and WHICH ONE IS TRIED FIRST MATTERS.
            --
            -- The "every type lookup is broken" conclusion was almost certainly
            -- wrong: the real weapon id is 'RadEruptionWeapon', and the id that
            -- failed, "Desolator", was my guess. 'CIV'/'ALLCIV'/'SOVCIV' are
            -- Red Alert 2 ids that do not exist in Yuri's Revenge at all. So
            -- DetonateAt(id) is tried first now, and taking the weapon off a live
            -- Desolator is only the fallback.
            --
            -- This is the cheap test that decides whether a superweapon binding
            -- is worth writing: SuperWeaponTypeClass::Find goes through the same
            -- mechanism, so if the id lookup works, so will that.
            local spread = CFG.blastSpread
            local via
            local okD, done, why
            if CFG.detonateWeapon and CFG.detonateWeapon ~= "" and World.DetonateAt then
                via = "id:" .. CFG.detonateWeapon
                local owner
                local all = World.GetUnits and World.GetUnits() or nil
                if all then
                    for _, u in ipairs(all) do
                        if util.is_alive(u) then owner = u break end
                    end
                end
                okD, done, why = pcall(World.DetonateAt, CFG.detonateWeapon, sx, sy,
                    owner, spread)
            end
            if (not (okD and done)) and World.DetonateAtFromUnit then
                local all = World.GetUnits and World.GetUnits() or nil
                local provider
                if all then
                    for _, u in ipairs(all) do
                        if util.is_alive(u) and u:GetTypeName() == STRIKE_TYPE then
                            provider = u
                            break
                        end
                    end
                end
                if provider then
                    via = "unit:" .. STRIKE_TYPE
                    okD, done, why = pcall(World.DetonateAtFromUnit, provider, sx, sy,
                        spread)
                end
            end
            S.detonateTries = (S.detonateTries or 0) + 1
            if okD and done then
                S.detonateOk = (S.detonateOk or 0) + 1
                S.struckThisEvent = true
                print(string.format(
                    "[RAD] DETONATED f=%d via=%s at (%d,%d) ok=%d/%d - the"
                    .. " engine is building the site.",
                    frame, tostring(via), sx, sy, S.detonateOk, S.detonateTries))
                return true
            end
            if not (S.detFailBeat) or (frame - S.detFailBeat) >= 600 then
                S.detFailBeat = frame
                print(string.format(
                    "[RAD] DETONATE failed f=%d via=%s ok=%s why=%s - the [Bullet]"
                    .. " lines in LuaAPI.log say which condition did not hold.",
                    frame, tostring(via), tostring(okD), tostring(why)))
            end
            return false
        end


      -- Do NOT spawn a Desolator. House.SpawnUnit refuses this side, and even if
      -- it succeeded, hand-building the radiation is the wrong approach:
      -- Phobos only builds a fully initialised RadSiteClass when a bullet with a
      -- radiation warhead detonates (their 0x469150 BulletClass_Detonate hook),
      -- and a site assembled by hand leaves Intensity/Tint/Radiate unset, which
      -- is what faults Phobos at +0x6E0AA.
      --
      -- So: use a Desolator the player already owns, and let the engine do the
      -- work. This is orchestration, not construction.
      local units = World.GetUnits()
      if not units then return false end

      -- Any living Desolator will do. It used to require an ALLIED one, which
      -- meant an enemy Desolator was ignored - so on a map where the AI owns the
      -- Desolator, the mod reported "no owned DESO" and did nothing even though
      -- a perfectly usable one was standing right there. All that matters is
      -- that it can be ordered to fire.
        local unit
        for _, u in ipairs(units) do
            if util.is_alive(u) and u:GetTypeName() == STRIKE_TYPE then
                if util.is_ally(house, u) then unit = u break end
                if not unit then unit = u end
            end
        end
        -- Expose the unit's own state so the caller can leave it alone while it
        -- is already attacking instead of re-issuing orders into a running
        -- attack run.
        unit_busy_check = nil
        if unit and unit.IsAttacking then
            unit_busy_check = function() return unit:IsAttacking() end
        end

      if not unit then
          -- "Not found" is not good enough. A Desolator that exists but is
          -- parked in its Attack Deployment Pit is invisible to GetUnits() and
          -- reports IsAlive()==false, which is indistinguishable from "you do
          -- not own one". Look in the full techno array and say which it is.
          local hint = "not present in the techno array"
          local all = World.GetAllUnits and World.GetAllUnits() or nil
          if all then
              for _, u in ipairs(all) do
                  if u:GetTypeName() == STRIKE_TYPE then
                      local okP, p = pcall(u.GetPosition, u)
                      local okA, alive = pcall(u.IsAlive, u)
                      local pos = (okP and p) and ("(" .. p.x .. "," .. p.y .. ")") or "no position"
                      hint = string.format("found one at %s but IsAlive=%s - it is"
                          .. " almost certainly still parked in the Attack"
                          .. " Deployment Pit. DEPLOY it (it must be out on the"
                          .. " map) and the mod can use it.",
                          pos, tostring(alive))
                      break
                  end
              end
          end
          if not S.warnedStrike then
              S.warnedStrike = true
              print("[RAD] STRIKE: no usable " .. STRIKE_TYPE .. " - " .. hint)
          end
          return false
      end

      -- Pick something to detonate on.
      --
      -- The reach must be measured FROM THE DESOLATOR, not from the hazard. That
      -- was a real bug: the order was placed on a neutral 85 cells away because
      -- the distance was tested against the infantry cluster, while the eruption
      -- only happens within a few cells of the Desolator. The order was accepted
      -- (res=true) and then nothing detonated, because the shooter could not
      -- possibly reach it.
      --
      -- For green to land on the hazard, the Desolator itself has to be near the
      -- hazard. So: check that first, and say so plainly if it is not.
      local ERUPT_REACH = 7          -- Desolator -> target
      local dpos = unit:GetPosition()
      if not dpos then return false end
      local dToHazard = math.abs(dpos.x - sx) + math.abs(dpos.y - sy)
      local tooFar = dToHazard > ERUPT_REACH
      if tooFar then
          -- Advice, not a refusal. This used to `return false`, which made the
          -- "drive to a further target" path below unreachable: whenever the
          -- Desolator was far from the infantry - which is exactly when a tester
          -- wants to see the effect - the mod gave up instead of firing. It now
          -- still shoots, and the green simply lands at the target.
          if not (S.farBeat) or (frame - S.farBeat) >= 300 then
              S.farBeat = frame
              print(string.format(
                  "[RAD] STRIKE: the %s is at (%d,%d) but the hazard is at (%d,%d)"
                  .. " - %d cells apart, so its green cannot cover the infantry"
                  .. " from there. Move it closer for that; firing anyway now.",
                  STRIKE_TYPE, dpos.x, dpos.y, sx, sy, dToHazard))
          end
      end

      local victim, bestD, victimKind
      for _, u in ipairs(units) do
          if util.is_alive(u) and u ~= unit then
              local p = u:GetPosition()
              if p then
                  local d = math.abs(p.x - dpos.x) + math.abs(p.y - dpos.y)
                  if d <= ERUPT_REACH then
                      local kind
                      if util.is_enemy(house, u) then
                          kind = "enemy"
                      else
                          local okO, owner = pcall(u.GetOwner, u)
                          if okO and owner and util.is_neutral_house(owner) then
                              kind = "neutral"
                          end
                      end
                      if kind and (bestD == nil or d < bestD) then
                          victim, bestD, victimKind = u, d, kind
                      end
                  end
              end
          end
      end
      if not victim then
          -- Nothing in eruption range. The Desolator is a mobile vehicle while
          -- un-deployed, so it CAN be sent to a target further away - but only a
          -- short walk is worth ordering.
          --
          -- This used to accept any distance, and that was actively harmful: it
          -- picked a neutral 58 cells away, ordered the attack every 2.4s, and
          -- the Desolator spent the whole event driving or firing from a range
          -- where CellSpread 10 / CellInset 3 means NO radiation is deployed. The
          -- engine registry stayed at zero sites for the entire run while the log
          -- cheerfully reported "ordered=true". Refuse the hopeless case and say
          -- plainly what is needed.
          local FAR_LIMIT = 18
          local far, farD, farKind
          for _, u in ipairs(units) do
              if util.is_alive(u) and u ~= unit then
                  local p = u:GetPosition()
                  if p then
                      local d = math.abs(p.x - dpos.x) + math.abs(p.y - dpos.y)
                      if d > ERUPT_REACH and d <= FAR_LIMIT and (farD == nil or d < farD) then
                          local kind
                          if util.is_enemy(house, u) then
                              kind = "enemy"
                          else
                              local okO, owner = pcall(u.GetOwner, u)
                              if okO and owner and util.is_neutral_house(owner) then
                                  kind = "neutral"
                              end
                          end
                          if kind then far, farD, farKind = u, d, kind end
                      end
                  end
              end
          end
          if far then
              victim, victimKind, S.strikeFar = far, farKind, farD
              if not (S.farSaid) or (frame - S.farSaid) >= 600 then
                  S.farSaid = frame
                  print(string.format(
                      "[RAD] STRIKE: nothing within %d cells, so the %s will drive"
                      .. " %d cells to a %s and fire there. The green will appear"
                      .. " where the target is, not on the infantry.",
                      ERUPT_REACH, STRIKE_TYPE, farD, farKind))
              end
          end
      end
      if not victim then
          if not (S.noTargetBeat) or (frame - S.noTargetBeat) >= 300 then
              S.noTargetBeat = frame
              print(string.format(
                  "[RAD] STRIKE: the %s is at (%d,%d) next to the hazard, but"
                  .. " there is nothing to fire on anywhere on the map. The"
                  .. " eruption needs a target - put a unit beside it.",
                  STRIKE_TYPE, dpos.x, dpos.y))
          end
          return false
      end

        local okA, resA = pcall(unit.Attack, unit, victim)
        S.struckThisEvent = true
        print(string.format("[RAD] STRIKE ordered=%s res=%s from=(%d,%d) on=%s(%s)",
            tostring(okA), tostring(resA), sx, sy, STRIKE_TYPE,
            tostring(victimKind or "?")))
        return true


end

-- GREEN TILE TEST: lay down real RadSiteClass zones with a WIDE spread, so the
-- engine draws the green itself. See the CFG block for why this is the one
-- untried thing.
--
-- Guarded twice: it refuses to run unless the binding reports the native path
-- is armed (LUAAPI_RADSITE_NATIVE=1), and it never exceeds greenCap. Creating
-- these faulted the renderer twice, so it must stay opt-in.
--
-- Zones are re-created as the field drifts, one at a time per sweep, so a crash
-- can be attributed to a single site rather than a flood of them.
local greenMade, greenTries, greenSaidNo = 0, 0, false
local greenLit = 0   -- stays 0: the engine paints its own green, we never tint

local function paintGreen(frame)
    if not CFG.green then return 0 end
    if not (World.RadSiteCreate and World.RadSiteSetLevel) then return 0 end
    if not (World.RadSiteNativeEnabled and World.RadSiteNativeEnabled()) then
        -- Say it out loud. Two runs were lost to exactly this silence: the gate
        -- was closed, so nothing happened, and a quiet log is indistinguishable
        -- from "zones were created and the engine did not draw them".
        if not greenSaidNo then
            greenSaidNo = true
            print("[RAD] GREEN DISARMED: LUAAPI_RADSITE_NATIVE is not set, so no"
                .. " zone is created. ground=cells only, and its green would"
                .. " not appear anyway (RadLevel is damage, not picture).")
        end
        return 0
    end

    if S.greenZones == nil then S.greenZones = {} end
    local made = 0
    local budget = 1                      -- deliberately slow: one site per sweep
    -- Walk outward from the hazard cells themselves, so the sites that get
    -- created are the ones the player is actually looking at.
    local seeds = S.targets
    if not seeds or #seeds == 0 then seeds = { { x = 60, y = 60 } } end
    for _, sd in ipairs(seeds) do
      for cy = sd.y - 3, sd.y + 3, CFG.greenStep do
        for cx = sd.x - 3, sd.x + 3, CFG.greenStep do
            if made >= budget then break end
            if greenMade < CFG.greenCap then
                local k = cx .. "," .. cy
                if not S.greenZones[k] then
                    local ok, res = pcall(World.RadSiteCreate, cx, cy,
                        CFG.greenSpread, CFG.groundLevel)
                    greenTries = greenTries + 1
                    if ok and res then
                        S.greenZones[k] = true
                        greenMade = greenMade + 1
                        made = made + 1
                        -- No tint call here on purpose. The engine painted this
                        -- zone vanilla green with lit=0, so writing a tint was
                        -- solving a problem that did not exist - and it was the
                        -- last thing to run before the renderer faulted.
                    end
      end
    end
            end
        end
    end
    if frame % 300 == 0 then
        print(string.format("[RAD] GREEN f=%d made=%d lit=%d tries=%d cap=%d spread=%d",
            frame, greenMade, greenLit, greenTries, CFG.greenCap, CFG.greenSpread))
    end
    return made
end

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
    -- Anti-spam 2026-09-29: this runs every sweep (1/s) for the whole ACTIVE
    -- phase, and the engine stacks banners + beeps each one. Announce once
    -- per event (flag reset at impact); damage keeps applying silently.
    if hits > 0 and hits <= 3 and not S.burnSaid then
        S.burnSaid = true
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

    -- The engine's own site, when a Desolator is deployed, is the ONLY correct
    -- reference for a site that actually has a light. Without it there is
    -- nothing to compare our created site against, and a field-by-field diff is
    -- worthless -- which is why the probe was silently useless for its first
    -- three runs: the Desolator was walking, never deploying, so the list was
    -- empty and there was no second object.
    --
    -- Throttled, because a deployed site is stable and re-dumping it every
    -- heartbeat is log spam.
    local lt = frame
    if lt - (desoListBeat or 0) >= 60 and World.RadSiteList then
        desoListBeat = lt
        local ok2, lst = pcall(World.RadSiteList)
        if ok2 and type(lst) == "string" and lst ~= "" and lst ~= " [SEH]" then
            print("[DESO] ENGINE-LIST" .. lst)
        end
    end

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

      -- F6 = instant. No 10 second warning, no 3-2-1, no banner. The player
      -- asked for this to record video without the script pausing the match
      -- every cycle, so F6 must produce the effect on the next tick and nothing
      -- else. The normal timed cycle is untouched.
      if S.instant then
          S.instant = nil
          S.ramp = CFG.groundLevel
          say(string.format("Radiation HITS NOW - %d cluster(s), no warning (F6).",
              #S.targets))
          enter(ST_ACTIVE, frame + CFG.activeFrames)
          return
      end

      say(string.format("Radiation warning: %d second%s until it hits. %d "
          .. "infantry cluster(s) will be irradiated - garrison them or move them.",
          CFG.announceSecs, (CFG.announceSecs == 1) and "" or "s", #S.targets))
      enter(ST_WARNING, frame + CFG.warnFrames)
  end


  -- Storm sweep driver. It MUST be called from update() every frame, not from
  -- the F7 handler: the handler runs once per key press, so a driver parked
  -- there never advances past aim cell 1.
  -- Applies the storm parameters once per sweep and proves, by reading them back
  -- from the engine, what is actually live. Returns nothing; the caller pcall()s
  -- it so that a failure here can never stop the launch.
  --
  -- Read-back is not optional. A fresh process was observed holding
  -- 150/15/200/0 against the documented 180/10/250/3, so none of these values can
  -- be assumed. The engine may also clamp the scatter, and a clamped scatter
  -- silently invalidates the "stride == scatter" tiling the sweep relies on.
  local function applyStormRules()
      if S.stormApplied then return end
      if not (World.SetLightningRules and World.GetLightningRules) then return end
      S.stormApplied = true
      local _, p1, p2v, p3, p4, p5 = pcall(World.GetLightningRules)
      pcall(World.SetLightningRules, CFG.thunderScatter,
          CFG.thunderDuration, CFG.thunderHitDelay, CFG.thunderDamage,
          CFG.thunderSeparation, -1)
      local okR, a1, b1, c1, d1, e1 = pcall(World.GetLightningRules)
      local got = tonumber(a1)
      if got and got ~= CFG.thunderScatter then
          print(string.format(
              "[RAD] WARNING asked scatter=%d but engine reports %d -"
              .. " stride must match the real value or the sweep leaves gaps",
              CFG.thunderScatter, got))
      end
      -- Assigned before use. Doing it after the banner left this nil once, the
      -- arithmetic threw, and the throw cost the whole frame - including the
      -- launch and every other mod's Update.
      S.stormFrames = tonumber(b1) or 180
      -- Bolt density is what makes a tiled square look continuous: bolts are
      -- placed at RANDOM points inside the n-by-n square, so a square can tile
      -- perfectly and still read as patches.
      local bolts = math.floor(S.stormFrames / math.max(1, tonumber(c1) or 10))
      print(string.format(
          "[RAD] lightning rules %s/%s/%s/%s/%s -> wrote %d/%d/%d/%d/%d"
          .. " -> now %s/%s/%s/%s/%s  (%d bolts per %dx%d square, "
          .. "warhead spread 2 ~ 13 cells -> %d%% coverage)",
          tostring(p1), tostring(p2v), tostring(p3), tostring(p4), tostring(p5),
          CFG.thunderScatter, CFG.thunderDuration, CFG.thunderHitDelay,
          CFG.thunderDamage, CFG.thunderSeparation,
          tostring(a1), tostring(b1), tostring(c1), tostring(d1), tostring(e1),
          bolts, CFG.thunderScatter, CFG.thunderScatter,
          math.floor(bolts * 13 / (CFG.thunderScatter * CFG.thunderScatter) * 100)))
  end

  -- Sweep centre, and the two base centroids it is derived from.
  --
  -- FAIRNESS. The sweep used to be ordered by distance from the PLAYER's own base,
  -- because starting there was the only way to make the storm visible (starting at
  -- the map origin put the first ten storms in a far corner). Once visibility was
  -- solved that reasoning expired, but the bias stayed: the last run started at
  -- (97,139) = our own base and expanded around it, so our base took the first
  -- several hits and the enemy base was reached much later. "Most storms were on
  -- my base, unfair" is exactly that, and it was a consequence of the fix, not of
  -- the engine.
  --
  -- Ordering by distance from the MIDPOINT between the two bases fixes it: the
  -- coverage grows as an expanding square centred between the two, so both bases
  -- are reached at the same rate and neither side is hit first. The player can
  -- still see the start, because the midpoint is inside the normal view range.
  --
  -- Returns centre x, centre y, own centroid, enemy centroid, own count,
  -- enemy count.
  local function sweepCenter(mx, my, mw, mh)
      local ax, ay, an = 0, 0, 0
      local ex, ey, en = 0, 0, 0
      if World.GetBuildings and house then
          local okB, bl = pcall(World.GetBuildings)
          if okB and type(bl) == "table" then
              for _, b in ipairs(bl) do
                  if util.is_alive(b) then
                      local pos = b:GetPosition()
                      if pos then
                          if util.is_ally(house, b) then
                              ax, ay, an = ax + pos.x, ay + pos.y, an + 1
                          elseif util.is_enemy(house, b) then
                              ex, ey, en = ex + pos.x, ey + pos.y, en + 1
                          end
                      end
                  end
              end
          end
      end
      local ox, oy = (an > 0) and math.floor(ax / an + 0.5) or nil
      local fx, fy = (en > 0) and math.floor(ex / en + 0.5) or nil
      local cx, cy
      if ox and fx then
          -- Midpoint: both bases are then equidistant, so they take equal numbers
          -- of hits for equal time.
          cx = math.floor((ox + fx) / 2 + 0.5)
          cy = math.floor((oy + fy) / 2 + 0.5)
      elseif ox then
          cx, cy = ox, oy
      elseif fx then
          cx, cy = fx, fy
      else
          cx = mx + math.floor(mw / 2)
          cy = my + math.floor(mh / 2)
      end
      print(string.format(
          "[RAD] sweep centre (%d,%d) = midpoint of ours (%s) n=%d and enemy (%s)"
          .. " n=%d - both bases are hit at the same rate",
          cx, cy,
          ox and tostring(ox .. "," .. oy) or "none", an,
          fx and tostring(fx .. "," .. fy) or "none", en))
      return cx, cy, ox, oy, fx, fy
  end

  -- MAP-SCALE RADIATION SWEEP
  --
  -- Why this exists. Green tiles come only from real RadSiteClass objects;
  -- CellClass::RadLevel is damage, not picture. The mod's own grid path
  -- (paintGreen) is switched off because hand-built sites fault in Phobos at
  -- +0x6E0AA. That leaves exactly one green source: the engine building a site
  -- from a detonation. Measured over a whole run, that source produces
  -- sites=1 in 16 probes out of 16 - one zone at a time, never two. With one
  -- zone per Desolator strike and no spreading path enabled, a 163x163 map is
  -- structurally unreachable.
  --
  -- So the storm's proven sweep shape is reused, with the detonation in place
  -- of the superweapon: serpentine over the REAL map extent, ordered by
  -- distance from the player's base, one detonation per aim cell. Each
  -- detonation is the engine's own, which is the only route known not to crash.
  -- Nothing is hand-built.
  local function buildRadSweep(mx, my, mw, mh)
      local bx, by = sweepCenter(mx, my, mw, mh)
      local cells = {}
      local x = mx
      while x < mx + mw do
          local y = my
          while y < my + mh do
              local dx, dy = x - bx, y - by
              if dx < 0 then dx = -dx end
              if dy < 0 then dy = -dy end
              cells[#cells + 1] = { x = x, y = y, d = (dx > dy) and dx or dy }
              y = y + CFG.radStep
          end
          x = x + CFG.radStep
      end
      table.sort(cells, function(p1, p2)
          if p1.d ~= p2.d then return p1.d < p2.d end
          if p1.x ~= p2.x then return p1.x < p2.x end
          return p1.y < p2.y
      end)
      return cells, bx, by
  end

  -- One detonation per aim cell. pcall-wrapped because a throw here would abort
  -- the rest of Update for the frame and every other mod's update with it.
  local function radDrive(frame)
      if not S.radActive then return end
      if S.radPending and S.radArmAt and frame >= S.radArmAt then
          local c = S.radPending
          S.radPending = nil
          S.radArmAt = nil
          local okD, done, why = pcall(World.DetonateAt, CFG.detonateWeapon,
              c.x, c.y, nil, CFG.blastSpread, CFG.radNoDamage)
          S.radOk = (S.radOk or 0) + (done and 1 or 0)
          if S.radSeq <= 3 or not done then
              print(string.format(
                  "[RAD] GREEN aim %d at (%d,%d) detonate=%s spread=%d%s",
                  S.radSeq, c.x, c.y, tostring(done), CFG.blastSpread,
                  (not done) and (" why=" .. tostring(why)) or ""))
          end
          S.radCell = true
          S.radNextAt = frame + CFG.radGap
      end
      if S.radCell and S.radNextAt and frame >= S.radNextAt then
          S.radCell = false
          local nxt = table.remove(S.radSweep, 1)
          if nxt then
              S.radPending = nxt
              S.radSeq = S.radSeq + 1
              S.radArmAt = frame + 2
          else
              S.radActive = false
              print(string.format(
                  "[RAD] GREEN sweep complete: %d cells, detonations ok=%d",
                  S.radSeq or 0, S.radOk or 0))
          end
      end
  end

  local function thunderDrive(frame)

      if not S.thunderActive then return end

      -- Retire the current aim cell and queue the next one.
      local cur = S.thunderCell
      if cur and frame >= cur.nextAt then
          S.thunderCell = nil
          local nxt = table.remove(S.thunderSweep, 1)
          if nxt then
              S.thunderPending = { x = nxt.x, y = nxt.y, seq = (S.thunderSeq or 0) + 1 }
              S.thunderSeq = S.thunderPending.seq
              S.thunderArmAt = frame + CFG.thunderGap
          else
              S.thunderActive = false
              print("[RAD] thunder sweep complete after "
                  .. tostring(S.thunderSeq or 0) .. " aim cells")
          end
      end

          -- Arm the next aim cell: apply the documented storm parameters, fire, and
          -- schedule the retire.
          --
          -- The rule application and its banner are wrapped in pcall on purpose.
          -- A throw anywhere in here aborts the REST of Mod.Update for that frame -
          -- a nil field in a log line took out the launch AND every other mod's
          -- Update (smart_ai reported the same error) - and silently dropped the
          -- aim cell, because the pending cell had already been cleared. Logging
          -- must never be able to stop the storm.
          if S.thunderPending and S.thunderArmAt and frame >= S.thunderArmAt then
              local p2 = S.thunderPending
              S.thunderPending = nil
              S.thunderArmAt = nil
              pcall(applyStormRules)
              if not S.stormFrames then S.stormFrames = 180 end
              local okS, done, why = pcall(World.LaunchHouseSuperWeapon,
                  CFG.thunderType, p2.x, p2.y)
              print(string.format(
                  "[RAD] THUNDERSTORM aim %d at (%d,%d) stock ok=%s%s",

                  p2.seq, p2.x, p2.y,
                  tostring(done),

              (done ~= true) and (" why=" .. tostring(why)) or ""))
          S.thunderCell = {
              nextAt = frame + S.stormFrames + CFG.thunderGap }
      end

  end

local function tick(frame)
    if not S.started then
        S.started = true
        S.nextEvent = frame + CFG.firstDelay
        print(string.format("[RAD] armed; first hazard f=%d", S.nextEvent))
    -- State the green-tile path's gate at startup. Its silence is the single
    -- most expensive thing in this investigation: two runs concluded "no green"
    -- when in fact the path had never been armed and nothing had been created.
    local armed = (World.RadSiteNativeEnabled and World.RadSiteNativeEnabled()) and true or false
    print(string.format(
        "[RAD] GREEN path %s (spread=%d cap=%d) - %s",
        armed and "ARMED" or "DISARMED", CFG.greenSpread, CFG.greenCap,
        armed and "zones will be created during ACTIVE"
              or "LUAAPI_RADSITE_NATIVE unset: no zone will be created"))
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
            S.struckThisEvent = false   -- one Desolator per event, not per session
            -- Each event gets one fresh attempt at supplying a target, so a
            -- failed spawn does not disable the strike for the whole session.
            S.noTargetTried = false
            S.targetType = nil
        enter(ST_ACTIVE, frame + CFG.activeFrames)
            S.burnSaid = false
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
        local green = paintGreen(frame)
        -- One Desolator per event, so the engine makes the green itself. Off
        -- unless asked for, because it only works on a side that owns one.
          -- Keep feeding the Desolator. It used to be strictly one shot per
          -- event (`struckThisEvent`), which was inherited from when the mod
          -- built the site itself. Delegating to the engine means every shot is
          -- a real site, so more shots means more green - and the Desolator
          -- re-orders itself for us now instead of firing once and idling.
          if CFG.strike and #S.targets > 0 then
              -- Do not re-order a Desolator that is already doing the job. The
              -- engine has its own state machine for drive-then-fire, and
              -- re-issuing Attack every 2.4s kept resetting it: the unit looked
              -- busy and even fired, but it never completed an attack run, and
              -- a shot fired from outside the eruption radius lays no radiation
              -- (CellSpread 10 / CellInset 3 - it only radiates what it can
              -- deploy on). Asking only when it is idle lets it finish.
              local busy = false
              if unit_busy_check then
                  local okI, atk = pcall(unit_busy_check)
                  busy = okI and atk == true
              end
              if not busy and (not S.strikeNext or (frame - S.strikeNext) >= 0) then
                  local got = radStrike(frame)
                  S.strikeNext = frame + (got and CFG.strikeGap or 60)
              end
          end

        if frame % 300 == 0 then
            local tc = {}
            for i = 1, math.min(4, #S.targets) do
                tc[#tc + 1] = string.format("(%d,%d)", S.targets[i].x, S.targets[i].y)
            end
            print(string.format("[RAD] ACTIVE f=%d hits=%d ground=%d/%d targets=%s",
                frame, hits, painted, #S.targets, table.concat(tc, " ")))

            -- Hard evidence that the ENGINE built the sites, not us. RadSiteList
            -- walks the engine's own registry, so a rising count here can only
            -- come from real detonations. This is the observation this whole
            -- project lacked: a RadSite that survives more than one frame.
            if not S.listProbed then
                S.listProbed = true
                local has = (World.RadSiteList ~= nil)
                local okL, lst = has and pcall(World.RadSiteList) or false, nil
                if has then okL, lst = pcall(World.RadSiteList) end
                print(string.format("[RAD] SITES probe: binding=%s ok=%s type=%s len=%s head=%s",
                    tostring(has), tostring(okL), type(lst),
                    (type(lst) == "string") and tostring(#lst) or "-",
                    (type(lst) == "string") and string.sub(lst, 1, 120) or tostring(lst)))
            end
            if World.RadSiteList and (frame - (S.listBeat or 0)) >= 300 then
                S.listBeat = frame
                local okL, lst = pcall(World.RadSiteList)
                -- The binding writes " [i] site=%p fx=%p" - it has never emitted a
                -- "pos=(x,y)" field. This parser used to look for exactly that, so
                -- it could never match, and every run reported
                -- "engineBuilt=0 / registry is EMPTY" while the raw probe line
                -- showed "[0] site=1B541120 fx=00000000". The green diagnosis was
                -- built on a measurement that could not succeed.
                --
                -- Count the entries the binding actually produces. fx is the
                -- brightness field it reads; 0 there is reported verbatim rather
                -- than guessed at, because that is the green-tile question.
                local cnt, lit, raw = 0, 0, {}
                for hexsite, hexfx in string.gmatch(lst, "site=(%x+) fx=(%x+)") do
                    cnt = cnt + 1
                    if hexfx ~= "00000000" then lit = lit + 1 end
                    if cnt <= 4 then
                        raw[#raw + 1] = hexsite .. "/" .. hexfx
                    end
                end
                if cnt > 0 then
                    print(string.format(
                        "[RAD] SITES f=%d engineBuilt=%d lit=%d  [%s]",
                        frame, cnt, lit, table.concat(raw, " ")))
                else
                    -- An empty result is NOT silence: the binding writes no header,
                    -- so "" means radVecCount == 0. Say that, because "no output"
                    -- reads as "no information" and hid a hard fact for a run.
                    S.emptySites = (S.emptySites or 0) + 1
                    if S.emptySites <= 3 or (frame % 900 == 0) then
                        print(string.format(
                            "[RAD] SITES f=%d engineBuilt=0 - RadSiteList returned"
                            .. " no entries (radVecCount == 0). Raw: [%s]",
                            frame, lst))
                    end
                end
            end
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
    if okF6 and f6 then
        -- F6 fires the effect immediately: no 10 second warning, no 3-2-1, no
        -- banner, no 30 frame delay. Requested so the mod does not interrupt a
        -- recording every cycle. The normal timed cycle is untouched.
        S.forced = true
        S.instant = true
        S.struckThisEvent = false
        S.noTargetTried = false
        S.targetType = nil
        S.warnedStrike = false
        S.ramp = CFG.groundLevel
        S.state = ST_IDLE
        S.stateUntil = frame
        S.nextEvent = frame + 1
        say("Manual radiation trigger (F6) - instant.")


        -- F6 also arms the map-scale green sweep. Independent of the timed
        -- radiation event: this is the only path that can cover the map, since
        -- the hand-built grid is off and the engine makes one site per blast.
        if World.GetMapSize and World.DetonateAt then
            local okM, ax, ay, aw, ah = pcall(World.GetMapSize)
            if okM and type(aw) == "number" and aw > 0 then
                local cells, bx, by = buildRadSweep(ax, ay, aw, ah)
                S.radSweep = cells
                S.radSeq = 0
                S.radOk = 0
                S.radCell = false
                S.radPending = table.remove(cells, 1)
                S.radSeq = 1
                S.radActive = true
                S.radArmAt = frame + 4
                print(string.format(
                    "[RAD] GREEN sweep armed: %d detonations over %dx%d map,"
                    .. " starting at our base (%d,%d), stride %d",
                    #cells + 1, aw, ah, bx, by, CFG.radStep))
            else
                print("[RAD] GREEN sweep not armed: GetMapSize returned no map")
            end
        end
    end

    local okF9, f9 = pcall(Input.WasKeyPressed, DESO_KEY)
    if okF9 and f9 then
        desoUntil = frame + 60 * 90    -- 90s: the last run ended 12s after F9
        desoLast = nil
        say("Desolator watch armed 30s (F9). Deploy it yourself and watch the log.")
    end
      if desoUntil > frame then desoWatch(frame) end

      -- Thunderstorm, on its own key and off by default. The civilian filter is
      -- not implemented yet, so this is not on F6 on purpose: F6 is the
      -- recording key and must not flatten anyone's city.
      local okT, f7 = pcall(Input.WasKeyPressed, CFG.thunderKey)
      if okT and f7 then
          -- One line to the log on every press, so "nothing happened" is never
          -- ambiguous: either the strike ran, or the flag is off, or it failed.
          print("[RAD] F7 pressed - thunderstorm="
              .. tostring(CFG.thunderstorm) .. " id=" .. tostring(CFG.thunderId))
          -- Report only. Nothing is launched from here: the SuperClass::Launch
          -- route is known to crash when the object is hand-built, so the only
          -- safe version is Launch() on an object the engine itself owns, and
          -- that first needs proof the house has one.
          if World.ListHouseSupers and not S.supersListed then
              S.supersListed = true
              local okL, txt = pcall(World.ListHouseSupers)
              print("[RAD] house supers = " .. (okL and tostring(txt) or tostring(txt)))
          end
          if not CFG.thunderstorm then
              if not S.thunderSaid then
                  S.thunderSaid = true
                  print("[RAD] F7 ignored: CFG.thunderstorm is false. The"
                      .. " civilian-building filter is not implemented, so a"
                      .. " Thunderstorm would destroy civilian structures too.")
              end
          else
              -- Plain, stock Thunderstorm. Every Rules override is gone: the
              -- storm now runs on the values the game shipped with
              -- (scatter=10 duration=180 hitDelay=10 separation=3 strikeSpread=2
              -- damage=250), which is what a third party firing the real
              -- superweapon looks like. The previous version widened the spread
              -- to 150 and fired a strike every 2 frames, which annihilated both
              -- bases and the match instantly - that was a settings problem, not
              -- a mechanism problem, and the mechanism is proven.
              --
              -- Two launches, because a stock storm only covers a 10 cell radius:
              -- one on the player's base and one on the enemy's, so both sides
              -- get hit the way an actual third-party attack would.
              -- Both bases, counted rather than guessed. The previous version
              -- put the "enemy" storm 3 cells from the "self" storm, so both
              -- landed on the same spot: the enemy centroid was built from
              -- whatever util.is_enemy matched, and that was not the enemy base.
              -- This prints the census so the next run shows the truth, and
              -- computes each base from its OWN buildings.
              -- Map-wide coverage, built from the REAL map size. The sweep is a
              -- serpentine over aim cells, and a house owns only ONE storm
              -- object, so the aim cell is moved by re-firing the same storm
              -- rather than by trying to run several at once (the second Launch
              -- replaces the first - proven).
              -- Real playable rectangle from the engine. The previous version
              -- used MaxWidth/MaxHeight, which is the 512x512 allocated BUFFER,
              -- not the map: the sweep walked (0,0)..(0,70) and onward through
              -- empty space, so there was sound but nothing on screen. GetMapSize
              -- returns x, y, width, height.
              local mx, my, mw, mh = 0, 0, 0, 0
              if World.GetMapSize then
                  local okM, a1, b1, c1, d1 = pcall(World.GetMapSize)
                  if okM and type(c1) == "number" and type(d1) == "number" then
                      mx, my, mw, mh = a1, b1, c1, d1
                  end
              end
              if mw <= 0 or mh <= 0 then
                  mx, my, mw, mh = 0, 0, 128, 128
                  print("[RAD] GetMapSize unavailable; falling back to 0,0 128x128")
              end
              -- Refuse an implausible map rather than sweeping it for hours. The
              -- 512x512 buffer size slipped through here once and produced 2704
              -- aim cells of empty space. A real YR map is at most 256x256.
              if mw > 256 or mh > 256 then
                  print(string.format(
                      "[RAD] refusing implausible map %dx%d - that is the cell"
                      .. " buffer, not the map. No sweep started.", mw, mh))
                  S.thunderActive = false
                  S.thunderSweep = {}
                  return
              end
              -- Stock storm only: the square size is the game's own
              -- LightningCellSpread (10), reported by GetLightningRules. Do not
              -- reference a CFG key here - a nil argument to string.format throws
              -- and kills the whole F7 handler, which is exactly what happened.
              print(string.format(
                  "[RAD] thunder sweep over map rect (%d,%d) %dx%d, stride %d"
                  .. " -> %d cells",
                  mx, my, mw, mh, CFG.thunderStep,
                  math.ceil(mw / CFG.thunderStep) * math.ceil(mh / CFG.thunderStep)))


              S.thunderSweep = S.thunderSweep or {}
              if #S.thunderSweep == 0 and not S.thunderCell
                  and not S.thunderPending then
                  -- The sweep starts at the player's OWN base and is ordered by
                  -- distance from it, so the storm always begins where the camera
                  -- is and then walks outward.
                  --
                  -- A plain serpentine began at the map origin, which put the
                  -- first ten storms in a far corner: the log looked exactly like a
                  -- storm that does not render, and nothing was visible. The first
                  -- aim cell was therefore moved onto the player's own base - which
                  -- fixed visibility and introduced the bias the player then
                  -- reported. The centre is now the midpoint between the bases, so
                  -- visibility and fairness hold at the same time.
                  local bx, by = sweepCenter(mx, my, mw, mh)

              -- Chebyshev distance on the cell grid: cheap, and the coverage
                  -- grows as an expanding square, so no cell is ever skipped and
                  -- consecutive aim cells are always neighbours.
                  local cells = {}
                  local x = mx
                  while x < mx + mw do
                      local y = my
                      while y < my + mh do
                          local dx, dy = x - bx, y - by
                          if dx < 0 then dx = -dx end
                          if dy < 0 then dy = -dy end
                          cells[#cells + 1] = {
                              x = x, y = y,
                              d = (dx > dy) and dx or dy,
                          }
                          y = y + CFG.thunderStep
                      end
                      x = x + CFG.thunderStep
                  end
                  table.sort(cells, function(p1, p2)
                      if p1.d ~= p2.d then return p1.d < p2.d end
                      if p1.x ~= p2.x then return p1.x < p2.x end
                      return p1.y < p2.y
                  end)
                  for i = 1, #cells do
                      S.thunderSweep[#S.thunderSweep + 1] =
                          { x = cells[i].x, y = cells[i].y }
                  end
              end

              -- Arm the sweep. The driver itself is thunderDrive(), called from

              -- Update() every frame: a driver living in the key handler would
              -- only ever fire aim cell 1, because the handler runs once.
              S.thunderActive = true
              S.thunderSeq = 0
              S.thunderCell = nil
              S.thunderPending = nil
              local first = table.remove(S.thunderSweep, 1)
              if first then
                  S.thunderPending = { x = first.x, y = first.y, seq = 1 }
                  S.thunderSeq = 1
                  S.thunderArmAt = frame
              else
                  S.thunderActive = false
                  print("[RAD] thunder sweep had no aim cells to run")
              end
              print(string.format(
                  "[RAD] F7 sweep armed: %d aim cells left, first at (%d,%d)",
                  #S.thunderSweep + 1, S.thunderPending and S.thunderPending.x or -1,
                  S.thunderPending and S.thunderPending.y or -1))
    end   -- if not CFG.thunderstorm / else
    end   -- if okT and f7

    -- Drive the storm sweep every frame. This has to be here, in Update(), not
    -- in the F7 handler: the handler runs once per press, so a driver parked
    -- there could never advance past aim cell 1.
    radDrive(frame)
    thunderDrive(frame)

    tick(frame)
end


function Mod.Summary()
    print(string.format(
        "[RAD] SUMMARY events=%d hits=%d damage=%d exempted=%d",
        stats.events, stats.hits, stats.damage, stats.exempted))
    return stats
end

-- Read-only hazard status for other mods (e.g. SmartAI RADEVAC).
-- Pure observation: no state change, no orders. Returns a FRESH table
-- every call (callers cannot mutate our S.targets through it).
-- phase: "IDLE" | "WARNING" | "ACTIVE" | "RECOVERY"
-- targets: {{x, y}...} blast cells (pinned in WARNING, re-picked per
--   sweep in ACTIVE); empty outside WARNING/ACTIVE.
-- radius: blast radius in cells (CFG.blastRadius).
-- untilFrame: frame the current phase ends.
function Mod.GetStatus()
    local tg = {}
    for _, t in ipairs(S.targets) do
        if type(t.x) == "number" and type(t.y) == "number" then
            tg[#tg + 1] = { x = t.x, y = t.y }
        end
    end
    return {
        phase = STATE_NAME[S.state] or "IDLE",
        untilFrame = S.stateUntil,
        targets = tg,
        radius = CFG.blastRadius,
    }
end

return Mod
