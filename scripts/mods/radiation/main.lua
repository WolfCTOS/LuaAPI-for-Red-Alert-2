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
  CFG.detonateWeapon = "Desolator"

  -- CellSpread to use for the blast, in cells. The Desolator's own is 10. A
  -- YR 1.001 map is 128x128, so 150 comfortably covers it from anywhere. This
  -- overrides the warhead for one detonation only; the binding restores it.
  -- 0 = leave the weapon's own spread alone.
  CFG.blastSpread = 150

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
        if (World.DetonateAtFromUnit or World.DetonateAt) and CFG.detonateWeapon then
            -- Prefer taking the weapon off a live unit's DEPLOY weapon. Every
            -- type lookup in this YRpp is unreliable - DetonateAt("Desolator")
            -- reported "unknown weaponId", and House:SpawnUnit never resolved a
            -- single typeId in this project's history, not even "E1". A
            -- Desolator's deploy weapon IS the radiation weapon, so reading it
            -- off the unit sidesteps the lookup completely.
            local provider
            if World.DetonateAtFromUnit then
                local all = World.GetUnits and World.GetUnits() or nil
                if all then
                    for _, u in ipairs(all) do
                        if util.is_alive(u) and u:GetTypeName() == STRIKE_TYPE then
                            provider = u
                            break
                        end
                    end
                end
            end
            local okD, done, why
            if provider then
                -- One wide blast instead of many small ones. The Desolator's
                -- own CellSpread is 10, i.e. a ~7 cell radius, so covering a
                -- 128x128 map that way would take hundreds of detonations. The
                -- binding widens CellSpread for the duration of this single
                -- detonation and restores it right after, so the site Phobos
                -- builds is map-sized. Zero means "use the weapon's own spread".
                okD, done, why = pcall(World.DetonateAtFromUnit, provider, sx, sy,
                    CFG.blastSpread)
            else
                local owner
                local all = World.GetUnits and World.GetUnits() or nil
                if all then
                    for _, u in ipairs(all) do
                        if util.is_alive(u) then owner = u break end
                    end
                end
                okD, done, why = pcall(World.DetonateAt, CFG.detonateWeapon, sx, sy, owner)
            end
            S.detonateTries = (S.detonateTries or 0) + 1
            if okD and done then
                S.detonateOk = (S.detonateOk or 0) + 1
                S.struckThisEvent = true
                print(string.format(
                    "[RAD] DETONATED f=%d via=%s at (%d,%d) ok=%d/%d - the"
                    .. " engine is building the site.",
                    frame, provider and "unit" or CFG.detonateWeapon, sx, sy,
                    S.detonateOk, S.detonateTries))
                return true
            end
            if not (S.detFailBeat) or (frame - S.detFailBeat) >= 600 then
                S.detFailBeat = frame
                print(string.format(
                    "[RAD] DETONATE failed f=%d via=%s ok=%s why=%s - the [Bullet]"
                    .. " lines in LuaAPI.log say which condition did not hold.",
                    frame, provider and STRIKE_TYPE or CFG.detonateWeapon,
                    tostring(okD), tostring(why)))
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
                if okL and type(lst) == "string" and lst ~= "" and lst ~= " [SEH]" then
                    local pos, cnt = {}, 0
                    for p in string.gmatch(lst, "pos=%((%-?%d+)%,(%-?%d+)%)") do
                        cnt = cnt + 1
                        if cnt <= 6 then pos[#pos + 1] = "(" .. p .. ")" end
                    end
                    print(string.format("[RAD] SITES f=%d engineBuilt=%d at=%s",
                        frame, cnt, (#pos > 0) and table.concat(pos, " ") or "-"))
                elseif okL and lst == "" then
                    -- An empty result is NOT silence. World_RadSiteList writes
                    -- no header, so "" means the engine's registry is empty:
                    -- radVecCount == 0. Say that, because "no output" reads as
                    -- "no information" and hid a hard fact for a whole run.
                    S.emptySites = (S.emptySites or 0) + 1
                    if S.emptySites <= 3 or (frame % 900 == 0) then
                        print(string.format(
                            "[RAD] SITES f=%d engineBuilt=0 - the engine's RadSite"
                            .. " registry is EMPTY. Shots are landing but no"
                            .. " radiation site is being built.", frame))
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
