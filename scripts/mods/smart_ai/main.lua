local util = require("framework.util")
local ForceGroup = require("framework.force_group")
local Tactical = require("framework.tactical")

local SmartAI = {}

-- CORE PRINCIPLE (M2, 2026-09-25 — read before extending this mod):
-- SmartAI does not create the battlefield situation it reacts to.
-- It exploits the battlefield situation created by the existing game AI.
--
-- Consequences for every future check:
--   * No production/spawns (AI.QueueUnit is BLOCKED live; zero SpawnUnit
--     in this file — verified by grep). We command existing units only.
--   * No order lease (vanilla re-selects eventually). We issue transition-
--     only orders + cooldowns and measure persistence via readback, never
--     assume it. Fighting units are NEVER yanked.
--   * Vanilla owns the mass (attack waves, demo-truck teams, base building).
--     SmartAI owns the point decisions on top: which economy to snipe,
--     when to retreat, whom to escort, when to stand down the base.
--   * A layer that needs vanilla to leave units idle will starve live
--     (proven 2026-09-25: idle pool=0 all match) — always add the
--     marching-not-fighting second choice.
--   * Surrender is a conjunction (no Barracks AND no WF AND no MCV/CY),
--     never a single-building trigger; anchor tables err toward DELAYING.


-- Difficulty: "auto" | "easy" | "medium" | "hard" (default "auto").
-- "auto" (2026-09-25, per-house): each AI house gets the preset matching
-- its lobby AI difficulty via house:GetAIDifficulty() (LuaAPI Beta M1,
-- normalized "easy"|"normal"|"hard"; engine stores it reversed). "normal"
-- maps to the medium preset. Old DLL (binding absent -> nil) or any
-- unexpected value falls back to SmartAI.DIFFICULTY_FALLBACK for that
-- house. A fixed value ("easy"/"medium"/"hard") still applies to every
-- AI house and remains the test override.
-- Design note (unchanged): presets scale OBSERVATION and TEMPO only —
-- scan cadence, radii, defender counts, cooldowns. No preset grants
-- production, credits, spawn or any economy cheat: the intelligence split
-- is behavioral (easy = rally only; medium = full behavior; hard = same
-- brain, faster reactions), never economic.
SmartAI.DIFFICULTY = "auto"
SmartAI.DIFFICULTY_FALLBACK = "medium"

-- Engine defeat transition on surrender: OFF (2026-09-26).
--
-- When true, a house that satisfies the surrender DoD is additionally passed
-- to Engine.__SmartAILose -> HouseClass::Lose(false), which forces the
-- engine's internal defeat transition. Measured consequence in a 1-human +
-- 3-AI match: the engine leaves the game main loop ~90 frames later, at
-- BorrowedTime expiry, and the whole match ends and the process exits
-- (code 0) with every other house still alive and undefeated. Reproduced
-- 3/3. Control-flow counters prove Lose() returns and the detour is merely
-- never entered again.
--
-- False keeps surrender as a DECISION only: the latch, order silence, base
-- liquidation and the HUD notice. Defeat itself stays the engine's job, which
-- is what this mod's architecture says it should be. Flip to true only to
-- reproduce the old behaviour for diagnosis.
SmartAI.SURRENDER_ENGINE_CALL = false

-- Preset key used when a house's difficulty is unknown (old DLL, read
-- failure, unexpected string). Defined before difficultyKey (upvalue).
local PRESET_FALLBACK_KEY = SmartAI.DIFFICULTY_FALLBACK

-- Map a house's engine difficulty string to a preset key. Unknown/nil ->
-- the fallback key. Pure function; harness-covered.
local function difficultyKey(level)
    if level == "easy" then return "easy" end
    if level == "normal" then return "medium" end
    if level == "hard" then return "hard" end
    return PRESET_FALLBACK_KEY
end

local PRESETS = {
    easy = {
        scan = 60,
        rallyEvery = 300,
        guard = false, escort = false, garrison = false,
        recall = false, defense = false, raid = false,
    },
    medium = {
        scan = 30,
        rallyEvery = 150,
        guard = true, escort = true, garrison = true,
        recall = true, defense = true, raid = true,
        intruderR = 18, assignR = 35, defendersN = 2, defenseEvery = 150,
        confirmN = 2, clearN = 4, marchRecall = true, redirectEvery = 300,
        raidN = 4, raidMin = 3, raidQuiet = 600, raidEvery = 600,
        raidRange = 60, raidRetreatMult = 2,
    },
    hard = {
        scan = 15,
        rallyEvery = 90,
        guard = true, escort = true, garrison = true,
        recall = true, defense = true, raid = true,
        intruderR = 24, assignR = 45, defendersN = 3, defenseEvery = 90,
        confirmN = 1, clearN = 6, marchRecall = true, redirectEvery = 180,
        raidN = 5, raidMin = 3, raidQuiet = 450, raidEvery = 450,
        raidRange = 80, raidRetreatMult = 2,
    },
}

-- Per-house preset cache ("auto" difficulty): house userdata -> preset
-- table. MUST stay above paramsFor: Lua binds upvalues lexically, and a
-- declaration below it would silently become a global (the same live bug
-- class the officerReset NOTE below documents). Cleared by officerReset.
local houseParamsCache = {}
-- Houses seen at the last scan (userdata keys). Lets the per-frame scan
-- gate compute the FASTEST house cadence without re-enumerating houses
-- every frame. Rebuilt each scan; cleared by officerReset on restart.
local aiHousesCache = {}

-- Global preset: fixed difficulty modes, harness override, and the
-- fallback used when per-house data is unavailable.
local function params()
    local key = SmartAI.DIFFICULTY
    if key == "auto" then key = SmartAI.DIFFICULTY_FALLBACK end
    return PRESETS[key] or PRESETS.medium
end

-- Per-house preset (Beta M1, "auto"): resolves the house's engine
-- difficulty to a preset table. Read via a pcall'd house:GetAIDifficulty()
-- when available (binding present on the current DLL); nil result, absent
-- binding, or read error all fall back to the global params(). Results are
-- cached per house for the whole match (the lobby value never changes
-- mid-match; userdata identity makes the table safe), so a hostile state
-- cannot appear per scan. Cleared by officerReset on match restart.
local function paramsFor(house)
    if SmartAI.DIFFICULTY ~= "auto" then return params() end
    if not house then return params() end
    local cached = houseParamsCache[house]
    if cached then return cached end
    local key = SmartAI.DIFFICULTY_FALLBACK
    if type(house.GetAIDifficulty) == "function" then
        local ok, level = pcall(house.GetAIDifficulty, house)
        if ok and (level == "easy" or level == "normal" or level == "hard") then
            key = difficultyKey(level)
        end
    end
    local preset = PRESETS[key] or PRESETS.medium
    houseParamsCache[house] = preset
    return preset
end

-- Per-frame scan-gate cadence: in "auto", the tick runs at the FASTEST
-- AI house's scan (a hard AI must actually see the battlefield at 15f,
-- not at the fallback's 30f); houses are the cache from the previous
-- scan (empty on the very first tick -> fallback). Fixed modes keep the
-- global preset's scan. Mixed-lobby note: slower houses therefore get
-- their layers evaluated at the faster tick too — order RATES stay
-- per-house (every layer is cooldown-gated by its own preset); what
-- moves at the tick cadence is detection granularity (confirmN counts
-- scans), which is acceptable and documented.
local function tickScanEvery()
    local base = params()
    if SmartAI.DIFFICULTY ~= "auto" then return base.scan end
    local best = base.scan
    for h in pairs(aiHousesCache) do
        local hp = paramsFor(h)
        if hp and hp.scan and hp.scan < best then best = hp.scan end
    end
    return best
end

local lastScanFrame = 0

-- ---------------------------------------------------------------------------
-- Capture-aware valuables guard (MVP): high-value own units avoid lone
-- exposure to observable vanilla mind-control threats. Vanilla capture
-- itself is untouched and unimplemented here: this only reads owner /
-- type / position via LuaAPI and issues standard MoveTo orders.
--
-- Vanilla ground truth being reasoned about (not implemented): Yuri
-- mind-control units flip a victim's owner (Yuri clone, Yuri Prime,
-- Mastermind; Hijacker ID unconfirmed and Chaos Drone causes frenzy,
-- not capture, so both are excluded). A flip is observable afterwards
-- as a GetOwner() change on a tracked id.
--
-- VALUABLE_TYPES lists high-value TypeIDs to protect. Extend with real
-- TypeIDs as needed (e.g. a custom Nuclear Truck goes here under the ID
-- of the mod that defines it; no such unit exists in this repo — verified
-- by repo-wide search). THREAT_TYPES lists observable capturer TypeIDs:
-- MIND was observed live; YURIPR/YURI are rules/wiki IDs for Yuri Prime
-- and the Yuri clone (both mind-control infantry).
-- ---------------------------------------------------------------------------

local VALUABLE_TYPES = {
    APOC = true,               -- Apocalypse: expensive, slow, high-value
    HTNK = true,               -- Rhino: main-line armor, capture-observed live
                               -- (HTNK#1058871 Africans -> YuriCountry,
                               -- Dannath session) - lone Rhinos are exactly
                               -- what mind-control punishes
}

local CAPTURE_THREATS = {
    MIND = true,               -- Mastermind: controls vehicles (live-observed)
    YURIPR = true,             -- Yuri Prime: mind control (rules/wiki ID)
    YURI = true,               -- Yuri clone: mind control (rules/wiki ID)
}

local THREAT_RADIUS = 9        -- mind-control engagement envelope class
local HOLD_RADIUS = 4          -- with-group distance: no lone exposure
local REORDER_EVERY = 600      -- refresh pullback at most every ~10 s

local guardState = {}          -- unitId -> { threatened = bool, orderedFrame = n }
local seenOwner = {}           -- unitId -> ownerName (flip observation only)
local guardLastFrame = 0

local function guardReset()
    guardState = {}
    seenOwner = {}
end

-- ---------------------------------------------------------------------------
-- Commander + Officers: AI-house coordination layer (classic split restored:
-- Commander manages the BASE, Officer manages UNITS).
-- One shared scan feeds everything below — no extra World scans. Officers
-- assign ROLES (escort / garrison-screen), never tactics: vanilla AI keeps
-- attacking, officers only attach support.
-- Deterministic (ID-ordered picks, no RNG/clock) and churn-free
-- (transition-only orders + destination memory + refresh caps).
--
-- Escort officer: own ARTILLERY_TYPES (V3, INI-confirmed [V3]) get up to
-- ESCORT_N idle combat bodyguards that follow at destination memory.
-- Engaged bodyguards are released, never yanked.
-- Garrison officer (EXPERIMENTAL): idle own infantry near own DEFENSE_TYPES
-- are ordered onto the building cell (vanilla enter-if-garrisonable, screen
-- otherwise). BUNKER id is UNCONFIRMED live (no BUNK* section in the repo's
-- INI subset); the directive log prints the acted type name so a live
-- census confirms it. NAPILL/GAPILL are INI-confirmed but screen-only
-- (not garrisonable).
-- ---------------------------------------------------------------------------

local ARTILLERY_TYPES = { V3 = true }
local DEFENSE_TYPES = { BUNKER = true, NAPILL = true, GAPILL = true }
local HARVESTER_TYPES = { SMIN = true, HARV = true, CMIN = true }
local MCV_TYPES = { AMCV = true, SMCV = true, YMCV = true }

-- Production anchors for strategic-defeat detection (Phase 1).
-- Source tags: [LIVE] = seen in live [SMARTAI][BASE] lines; others are
-- pre-existing/ModEnc knowledge marked [UNVERIFIED]. Fail-safe direction:
-- an extra entry can only DELAY surrender (never cause a false one), while
-- a missing entry could cause one — so uncertain IDs stay IN until live
-- BASE lines confirm or deny them. TODO: confirm unverified IDs live.
local ANCHOR_BARRACKS = {
    NAHAND = true, -- [LIVE] Soviet Barracks
    NABRCK = true, -- [UNVERIFIED]
    GAPILE = true, -- [UNVERIFIED] Allied Barracks
    YABRCK = true, -- [UNVERIFIED] Yuri Barracks
}
local ANCHOR_WF = {
    NAWEAP = true, -- [LIVE] Soviet War Factory
    GAWEAP = true, -- [LIVE] Allied War Factory
    YAWEAP = true, -- [UNVERIFIED] Yuri War Factory
    YWEAP = true,  -- [UNVERIFIED] alt Yuri War Factory id
}
local ANCHOR_MCV = {
    AMCV = true, SMCV = true, YMCV = true, -- pre-existing MCV trio
    NACNST = true, -- [LIVE] Soviet Construction Yard (deployed MCV)
    GACNST = true, -- [LIVE] Allied Construction Yard
    YACNST = true, -- [UNVERIFIED] Yuri Construction Yard
}

-- Surrender futility (user ruling 2026-09-25): with anchors gone the
-- house cannot recover; it surrenders only when its remaining combat
-- value is below this ratio of the strongest enemy's (futility).
-- Tunable; harness uses clear-cut ratios, live feel decides the number.
local SURRENDER_FUTILITY_RATIO = 0.25

local surrendered = {} -- house -> latch frame (Phase 1: order silence)

local ESCORT_N = 2
local ESCORT_RADIUS = 6
local ESCORT_EVERY = 300
local ESCORT_DRAFT_RADIUS = 25 -- bodyguards draft near their V3 only: a guard
                               -- trekking 100 cells across the map is not an
                               -- escort, and far V3s must not eat home
                               -- defenders / recall candidates (live T15 bug)
local GARRISON_N = 3
local GARRISON_RADIUS = 12
local GARRISON_EVERY = 600

-- Base-threat recall: presence-based. RECALL_TYPES are committed long-range
-- attackers worth bringing home.
-- KIROV id UNCONFIRMED live (no KIROV section in the repo INI subset,
-- never seen in census) — the recall log prints the acted type name.
local RECALL_TYPES = { V3 = true, KIROV = true }
local RECALL_THREAT_RADIUS = 20
local RECALL_THREAT_N = 3
local RECALL_FAR = 30
local RECALL_EVERY = 300

-- RALLY threat radius (2026-09-26): an own building with a hostile combat
-- unit inside this radius is a breach. Tighter than RECALL_THREAT_RADIUS (20)
-- and much tighter than the point-defense intruderR (18) because this is
-- PER BUILDING, not per base: a raider 18 cells from building X is not
-- threatening X. 10 cells covers the firing envelope of direct-fire units
-- plus their approach, so a reserve ordered here arrives in time.
-- The old trigger was "own building below 85% max HP", which was a sampling
-- race: a building can be destroyed between two scans, and two consecutive
-- live matches logged RALLY_BREACH = 0 while a BASE line showed NAMISL at
-- 585/1000 (58.5%). Proximity is observable every scan and cannot be missed
-- between frames.
local RALLY_THREAT_R = 10
local RALLY_THREAT_R2 = RALLY_THREAT_R * RALLY_THREAT_R

-- M2-C3 raider force: economy snipe values (data, not branches).
-- Refinery/power/tech IDs INI-confirmed (NAREFN/GAREFN, NAPOWR/GAPOWR,
-- NATECH/GATECH); Yuri economy IDs unknown — omitted, never invented.
-- NAFLAK (Flak cannon) live-observed; pillbox/sentry are screen-only
-- defense types shared with the garrison table.
local RAID_REFINERY = { NAREFN = true, GAREFN = true }
local RAID_POWER = { NAPOWR = true, GAPOWR = true }
local RAID_TECH = { NATECH = true, GATECH = true }
local RAID_AA_BUILDING = { NAFLAK = true }

-- Prefixed IDs (e.g. RAZER-NAREFN on modded maps): match exact or
-- "-SUFFIX" so mod namespaces never blind a layer (proven 2026-09-25:
-- belief + raid valuation missed a full-HP RAZER-NAREFN). Dash required —
-- "XNAREFN" without one does not match. Sets stay tiny; linear scan is
-- cheaper than the native calls this code already makes per tick.
local function typeIn(set, t)
    if not t or t == "" then return false end
    if set[t] then return true end
    for k in pairs(set) do
        if t:sub(-(#k + 1)) == "-" .. k then return true end
    end
    return false
end

local function raidTargetValue(e)
    if e.k == "building" then
        if typeIn(RAID_REFINERY, e.t) then return 3.0 end
        if typeIn(RAID_POWER, e.t) then return 2.5 end
        if typeIn(RAID_TECH, e.t) then return 2.0 end
        if typeIn(RAID_AA_BUILDING, e.t) then return 1.5 end
        return 1.0
    end
    if HARVESTER_TYPES[e.t] then return 2.0 end -- mobile economy fallback
    return 0.0 -- raiders snipe economy, not meat (C3 scope)
end

-- ---------------------------------------------------------------------------
-- M2-C1 Belief / Memory (grudge slice): repeated economic attacks change
-- future raid targeting. EVENT (HP drop or kill of own economy with an
-- enemy near) -> MEMORY (grudge[aiHouse][enemyHouse] count) -> BELIEF
-- (raid value multiplier for that owner's candidates) -> DIFFERENT
-- DECISION (BELIEF_EFFECT when the winner flips). Pure Lua, in-match
-- only (cleared by officerReset — never persist.lua: cross-match memory
-- must not drive deterministic in-match decisions). No RNG, ID-tiebreaks.
-- ---------------------------------------------------------------------------

local GRUDGE_W = 0.5            -- value multiplier per remembered attack
local GRUDGE_CAP = 4            -- max counted attacks per house pair
local GRUDGE_ATTRIB_R = 12      -- attribution radius (cells, mirrors retreat scan)
local ECON_EVENT_EVERY = 300    -- per-victim event cooldown (frames)

local function isEconAsset(e)
    if e.k == "building" then
        return typeIn(RAID_REFINERY, e.t)
            or typeIn(RAID_POWER, e.t) or typeIn(RAID_TECH, e.t)
    end
    return HARVESTER_TYPES[e.t] == true
end

-- Point defense (fixes "parked army at the base is ignored"): enemy ground
-- mobiles inside INTRUDER_RADIUS of any own building are intruders.
-- Tier 1: idle own vehicles near the intruder get Attack orders (transition-
-- only + refresh cap). Tier 2 (march recall): a CONFIRMED intrusion (seen N
-- consecutive scans) pulls marching (non-idle, non-attacking) ground units
-- back with MoveTo; units actually fighting (IsAttacking) are NEVER yanked,
-- and unknown attack state (no binding) means skip — never assume.
local DEFENSE_KINDS = { unit = true, infantry = true }

-- Suicide-unit types: threat != price (a DTRUCK costs little but one-shots
-- a building). Ranked before everything in intruder sort (data, not logic).
-- DTRUCK INI-confirmed ([DTRUCK] Demolitions Truck, repo INI set).
local SUICIDE_TYPES = { DTRUCK = true }

-- M2-C4 air defense (data): AA-capable mobile types, drafted first (and
-- exclusively from infantry) against air intruders. All INI-confirmed:
-- HTK Flak Track, FLAKT Flak Trooper, FV Allied IFV, YTNK Gattling Tank.
-- AIR_RAIDERS covers bombers that read as kind=="unit" live (ZEP Kirov —
-- engine ground truth per M1 anomaly note; KIROV kept as harmless extra).
-- kind=="aircraft" (jets etc.) is always air, no list needed.
local AA_TYPES = { HTK = true, FLAKT = true, FV = true, YTNK = true }
local AIR_RAIDERS = { ZEP = true, KIROV = true }

local function isAirThreat(e)
    if e.k == "aircraft" then return true end
    return AIR_RAIDERS[e.t] == true
end

-- M2-C4 severity weights (data): suicide 4.0, vehicle 1.0 + cost/2000,
-- infantry 0.5, other 0.5. Levels: LOW < 2.0 (single scout),
-- NORMAL in between, HIGH >= 6.0 (push).
local SEV_SUICIDE = 4.0
local SEV_INFANTRY = 0.5
local SEV_LOW_BELOW = 2.0
local SEV_HIGH_FROM = 6.0

-- Lease-lite (weakness fix): advice the engine overwrote gets bounded
-- re-asserts, then a release. A native lease primitive would need C++;
-- until its separate Beta case is proven, this is the Lua-side ceiling.
-- The lease window (60f) is SHORTER than Tier-1 refresh (150f) on purpose:
-- the lease is the first responder to a yank; Tier-1 stays the steady
-- allocator. Retry only a live commanded target, max LEASE_RETRIES, then
-- let go (visible as LEASE_REASSERT / LEASE_RELEASED, live-tunable).
local LEASE_RETRIES = 2
local LEASE_RETRY_EVERY = 60

-- Retaliate (M14.1-line self-preservation): an AI combat unit taking fire
-- while holding another target turns onto its assumed attacker (nearest
-- hostile). Corrects only the TARGET via the native path (unit stays in
-- combat — never pulled off, never yanked to base); cooldown-gated, no
-- re-issue inside the window. Skips officer-assigned (roles owned
-- elsewhere) and non-combat types. Apocs CAN hit air — no special case.
local RETALIATE_R = 12
local RETALIATE_EVERY = 300

local function intruderSev(e)
    if SUICIDE_TYPES[e.t] then return SEV_SUICIDE end
    if e.k == "infantry" then return SEV_INFANTRY end
    if e.k == "unit" then return 1.0 + (e.cost or 0) / 2000.0 end
    return 0.5
end

local escortState = {}   -- v3id -> { guards = {ids}, ax, ay, orderedFrame }
local garrisonState = {} -- bldId -> { orderedFrame }
local cmdState = {}      -- house -> { x, y, frame } last breach (Commander blackboard)
local recallState = {}   -- unitId -> { orderedFrame } recalled heavies
local rallyState = {}    -- house -> { x, y, frame } last rally (same-breach cooldown)
local defenseState = {}  -- defenderId -> { targetId, orderedFrame }
local intruderSeen = {}  -- house -> { [intruderId] = { n, lastFrame } }
local defSevLevel = {}   -- M2-C4: house -> last logged severity level
-- M2-C3 raider force: per-AI-house hunter group from idle surplus.
-- raidState[house] = { members = {ids}, targetId, orderedFrame, formedFrame }
local raidState = {}       -- house -> raid group (id-tracked, re-resolved per tick)
local lastThreatFrame = {} -- house -> last frame with intruders or breach (quiet gate)
local raidCooldownUntil = {} -- house -> frame before which no re-form after STANDDOWN
-- M2-C5 execution layer: house -> ForceGroup (CombatStateTracker Observe +
-- Tactical Evaluate/Decide). SmartAI keeps Act (claims/dlog) + target
-- selection (grudge) + coordination. Groups are created on raid FORM and
-- dropped with the group (standdown/retreat keeps it while members live).
local raidGroups = {}

-- House getter factory (avoids loop-variable capture in closures).
local function houseGetter(h)
    return function() return h end
end

-- === M2-C4 ADAPTATION (Act -> Observe result -> Adapt) ====================
-- Severity re-evaluates the CURRENT threat size every tick. That is
-- re-evaluation, not adaptation: it has no memory of how the previous
-- response went. This table is that memory.
--
-- dEff[aiHouse] = { w = wins, l = losses, streak = consecutive losses,
--                   adaptUntil = frame before which no adapt-pull }
--
-- An "episode" is one point-defense assignment (defenderId -> intruderId)
-- that ENDS. It is resolved in the end-of-Update cleanup, where both
-- liveness facts are already known:
--   defender gone, intruder alive -> LOSS
--   defender alive, intruder gone -> WIN
--   both gone                      -> draw, counted as neither
--   both alive                     -> still ongoing, not counted
--
-- ADAPT_STREAK_TRIGGER consecutive losses arms the adaptation. Rationale: a
-- single loss can be a cornered unit; two in a row is a signal that the
-- current defence allocation is not winning its exchanges.
--
-- Why escalate by pulling units instead of raising `sevN`: measured live,
-- `poolIdle = 0` while `poolOwn = 1..6` -- there are no idle defenders left to
-- draft, so a larger sevN would compute a bigger number and still find zero
-- candidates. Marching units (`attacking == false`) are the pool that actually
-- exists, and march-recall already uses exactly that predicate.
local dEff = {}
local ADAPT_STREAK_TRIGGER = 2
local ADAPT_PULL_MIN_DIST = 25 -- cells from the base anchor; near units are left alone
local dEffStats = { w = 0, l = 0, pulls = 0, denied = 0, draw = 0 } -- per-match totals (diagnostic)

-- M2-C1 belief tables (in-match only; cleared by officerReset below).
-- grudge[aiHouse][enemyHouse] = { n = count, lastFrame = frame }
-- econSeen[aiHouse][unitId] = { hp, x, y, eventFrame } prev-tick economy
-- snapshot, PER HOUSE. The outer key is mandatory: the belief loop runs once
-- per AI house with a nowSeen set built from that house alone, so a flat
-- id->snapshot store made every OTHER house's economy look "gone" on every
-- scan -- false ECON_KILLED events, mass pruning, and (because the prune
-- re-seeds eventFrame) a dead per-victim cooldown. See the 2026-09-26 fix.
local grudge = {}
local econSeen = {}
-- Retaliate prev-tick store: combatSeen[unitId] = { hp, retalFrame }.
local combatSeen = {}
-- Bomber-focus cooldowns: focusState[unitId] = { orderedFrame }.
local focusState = {}
-- NOTE: these MUST stay above officerReset: Lua binds upvalues lexically,
-- so a reset function defined before the local would silently write a
-- global instead (live bug found 2026-09-24: census cadence + divLogged
-- survived match restarts).
local divLogged = {} -- defenderId -> last divergent actual target id logged
local lastCensusFrame = 0

local function officerReset()
    escortState = {}
    garrisonState = {}
    cmdState = {}
    recallState = {}
    rallyState = {}
    defenseState = {}
    intruderSeen = {}
    defSevLevel = {}
    raidState = {}
    lastThreatFrame = {}
    raidCooldownUntil = {}
    raidGroups = {}
    grudge = {}
    econSeen = {}
    dEff = {}
    dEffStats = { w = 0, l = 0, pulls = 0, denied = 0, draw = 0 }
    combatSeen = {}
    focusState = {}
    divLogged = {}
    lastCensusFrame = 0
    surrendered = {}
    houseParamsCache = {}
    aiHousesCache = {}
end

-- Coordination: units the Officer is actively handling (escort bodyguards,
-- threatened pullbacks, active defenders). Commander rally stands off them.
local function isOfficerAssigned(id)
    local gs = guardState[id]
    if gs and gs.threatened then return true end
    if defenseState[id] then return true end
    for _, st in pairs(escortState) do
        for _, gid in ipairs(st.guards) do
            if gid == id then return true end
        end
    end
    for _, rst in pairs(raidState) do
        if rst.members then
            for _, mid in ipairs(rst.members) do
                if mid == id then return true end
            end
        end
    end
    return false
end

-- ---------------------------------------------------------------------------
-- Snapshot: ONE World scan per tick, pcall-hardened. Every layer below reads
-- cached fields instead of re-issuing native calls (GetPosition/GetDistanceTo
-- per pair was the old O(n*m) native-call hotspot). Building userdata never
-- persists past the tick; unit userdata is used same-tick only.
-- ---------------------------------------------------------------------------

local function snapOf(u)
    local aliveOk, alive = pcall(u.IsAlive, u)
    if not aliveOk or alive ~= true then return nil end
    local e = { u = u, idle = false, attacking = nil }
    local ok, v = pcall(u.GetId, u)
    if not ok or type(v) ~= "number" then return nil end
    e.id = v
    ok, v = pcall(u.GetOwner, u)
    e.oh = (ok and v) or nil
    if e.oh then
        local onOk, on = pcall(e.oh.GetName, e.oh)
        e.on = (onOk and type(on) == "string") and on or nil
    end
    ok, v = pcall(u.GetTypeName, u)
    e.t = (ok and type(v) == "string") and v or ""
    ok, v = pcall(u.GetKind, u)
    e.k = (ok and type(v) == "string") and v or ""
    ok, v = pcall(u.GetPosition, u)
    if ok and type(v) == "table" and type(v.x) == "number" and type(v.y) == "number" then
        e.x, e.y, e.hasPos = v.x, v.y, true
    else
        e.x, e.y, e.hasPos = 0, 0, false
    end
    ok, v = pcall(u.IsIdle, u)
    e.idle = (ok and v == true)
    ok, v = pcall(u.IsAttacking, u)
    if ok then e.attacking = (v == true) end
    ok, v = pcall(u.GetHealth, u)
    e.hp = (ok and type(v) == "number") and v or nil
    ok, v = pcall(u.GetMaxHealth, u)
    e.maxhp = (ok and type(v) == "number") and v or nil
    ok, v = pcall(u.GetCost, u)
    e.cost = (ok and type(v) == "number" and v > 0) and math.floor(v) or 0
    return e
end

local function dist2(ax, ay, bx, by)
    local dx, dy = ax - bx, ay - by
    return dx * dx + dy * dy
end

local function findSnap(list, id)
    for _, e in ipairs(list) do
        if e.id == id then return e end
    end
    return nil
end

local function orderMove(e, x, y)
    local ok, res = pcall(e.u.MoveTo, e.u, x, y)
    return ok and res == true
end

local function orderAttack(e, target)
    local ok, res = pcall(e.u.Attack, e.u, target)
    return ok and res ~= false
end

-- ---------------------------------------------------------------------------
-- Diagnostics (attribution only — never decisions, never re-orders).
-- Every SmartAI-issued order logs frame + site + unit identity so a later
-- log reader can answer "SmartAI ordered X to unit Y at frame Z. What did
-- the game report afterward?" print() goes to LuaAPI.log only (no HUD).
-- Namespace [SMARTAI]* is ours; target_reselect uses [TARGET]/[M14.1].
-- ---------------------------------------------------------------------------

local SMARTAI_CENSUS_EVERY = 1800 -- AI-house roster dump cadence (frames)

local function dlog(tag, frame, body)
    print(string.format("[SMARTAI][%s] frame=%d %s", tostring(tag),
        math.floor(frame or 0), tostring(body)))
end

local function liveId(u)
    local ok, uid = pcall(u.GetId, u)
    if ok and type(uid) == "number" then return uid end
    return nil
end

-- Immediate read-back of the engine's actual target (existing GetTarget
-- binding; read-only). Returns nil when the engine reports no target.
local function readbackTargetId(u)
    local okT, tgt = pcall(u.GetTarget, u)
    if not okT or tgt == nil then return nil end
    return liveId(tgt)
end

local function unitDesc(e)
    return string.format("unit=%s type=%s kind=%s owner=%s",
        tostring(e.id), tostring(e.t), tostring(e.k), tostring(e.on))
end

local function diagOrder(frame, site, e, order, extra)
    dlog("ORDER", frame, string.format("site=%s %s order=%s%s",
        tostring(site), unitDesc(e), tostring(order),
        extra and (" " .. tostring(extra)) or ""))
end

-- Read-only audit: did SmartAI itself order `did` after `sinceFrame`?
-- Scans SmartAI's own order memories (diagnostics only; garrison has no
-- per-unit record and is therefore NOT covered — documented gap).
-- Returns "none" or a comma list like "RALLY@1230".
local function lastSmartAIOrder(did, sinceFrame)
    local hits = {}
    local gs = guardState[did]
    if gs and gs.threatened and (gs.orderedFrame or 0) > (sinceFrame or 0) then
        hits[#hits + 1] = "GUARD_PULLBACK@" .. tostring(gs.orderedFrame)
    end
    for pid, st in pairs(escortState) do
        if st.guards then
            for _, gid in ipairs(st.guards) do
                if gid == did and (st.orderedFrame or 0) > (sinceFrame or 0) then
                    hits[#hits + 1] = "ESCORT@" .. tostring(st.orderedFrame)
                        .. "(principal " .. tostring(pid) .. ", approx)"
                    break
                end
            end
        end
    end
    local rs = recallState[did]
    if rs and (rs.orderedFrame or 0) > (sinceFrame or 0) then
        hits[#hits + 1] = "RECALL@" .. tostring(rs.orderedFrame)
    end
    for _, rdst in pairs(raidState) do
        if rdst.members then
            for _, mid in ipairs(rdst.members) do
                if mid == did and (rdst.orderedFrame or 0) > (sinceFrame or 0) then
                    hits[#hits + 1] = "RAID_ATTACK@" .. tostring(rdst.orderedFrame)
                    break
                end
            end
        end
    end
    for _, rst in pairs(rallyState) do
        if rst.ids and rst.ids[did] and (rst.frame or 0) > (sinceFrame or 0) then
            hits[#hits + 1] = "RALLY@" .. tostring(rst.frame)
        end
    end
    if #hits == 0 then return "none" end
    return table.concat(hits, ",")
end

-- === C4 DETECTOR DIAGNOSTIC =============================================
-- DIAGNOSTIC ONLY. Changes no decision, issues no order, and leaves the real
-- detector's `if` chain byte-for-byte intact. It exists because live matches
-- logged ZERO DEFENSE_SEV / POINT_DEFENSE / MARCH_RECALL over 45 360 frames
-- while RALLY_BREACH proved own buildings were being damaged -- so the
-- detector's `intruders` table was empty against a real intrusion, and the
-- pipeline stage responsible was unknown.
--
-- c4diagScan MIRRORS the POINT DEFENSE predicate chain in the same
-- short-circuit order and attributes each unit to the FIRST failing
-- predicate, which is exactly what the `and` chain does. The mirror is
-- guarded, not trusted: the harness asserts distPass == #intruders, so any
-- future divergence between mirror and detector fails a test instead of
-- silently producing wrong diagnostics.
--
-- Rejection reasons: OWN (no owner / own house), ALLIED, NEUTRAL, CIVIL,
-- KIND (not in DEFENSE_KINDS), NOPOS (no position), DISTANCE (passed every
-- filter but no own building within intruderR).
local C4DIAG_SAMPLE_CAP = 6 -- bounded per census interval; never per frame
local C4DIAG_MAX_COORD_SAMPLES = 3

-- Preset table -> its PRESETS key (identity match; tables are distinct).
-- Used only by the C4PRESET diagnostic so a log can name the preset instead
-- of dumping the table. Unknown => "?".
local function c4PresetName(t)
    if not t then return "nil" end
    for k, v in pairs(PRESETS) do
        if v == t then return tostring(k) end
    end
    return "?"
end

-- Raw engine difficulty string for diagnostics. Mirrors the read paramsFor
-- performs, but reports the value instead of acting on it: a failed read
-- returns nil there (and falls back to DIFFICULTY_FALLBACK), which is
-- indistinguishable from a real level unless it is logged. "unbound" and
-- "error" are reported distinctly so both are visible in a log.
local function c4RawDifficulty(house)
    if type(house.GetAIDifficulty) ~= "function" then return "unbound" end
    local ok, level = pcall(house.GetAIDifficulty, house)
    if not ok then return "error" end
    if level == nil then return "nil" end
    return tostring(level)
end

local function c4diagScan(units, home, aiHouse, P, allied)
    local r2 = P.intruderR * P.intruderR
    local d = {
        cands = #units, home = #home, R = P.intruderR, R2 = r2,
        own = 0, allied = 0, neutral = 0, civil = 0,
        kindIn = 0, kind = 0, nopos = 0,
        pass = 0, distRej = 0, distPass = 0,
        intr = 0, kindsRej = {}, samples = {}, coordSamples = 0,
        -- Act-side candidate selection (DIAGNOSTIC ONLY). The funnel above
        -- answers "did the base see the intruder?"; this one answers "given
        -- that it did, on which gate did Act lose the candidate?". Filled by
        -- the read-only pass in the Tier-1 block and printed on the same
        -- census line, so it adds no per-frame spam.
        --
        -- claimIds/orderIds de-duplicate a unit that Act retries against
        -- several intruders within one pass: a unit is counted once per
        -- outcome, never once per (unit, intruder) pair.
        act = {
            ran = false, ownTotal = 0, ownIdle = 0, ownOfficerHeld = 0,
            ownKindRejected = 0, ownNonGround = 0, ownNoPos = 0,
            ownEligible = 0, inAssignR = 0,
            claimDenied = 0, orderFail = 0,
            actKindRej = {}, actNonGroundRej = {},
            claimIds = {}, orderIds = {},
        },
    }
    for _, u in ipairs(units) do
        local why
        if not (u.oh and u.oh ~= aiHouse) then
            why = "OWN"
        elseif allied(aiHouse, u.oh) then
            why = "ALLIED"
        elseif util.is_neutral_house(u.oh) then
            why = "NEUTRAL"
        elseif util.CIVIL_TYPES[u.t] then
            why = "CIVIL"
        elseif not DEFENSE_KINDS[u.k] then
            why = "KIND"
        elseif not u.hasPos then
            why = "NOPOS"
        end
        if why then
            if why == "OWN" then
                d.own = d.own + 1
            elseif why == "ALLIED" then
                d.allied = d.allied + 1
            elseif why == "NEUTRAL" then
                d.neutral = d.neutral + 1
            elseif why == "CIVIL" then
                d.civil = d.civil + 1
            elseif why == "KIND" then
                d.kind = d.kind + 1
                local kk = tostring(u.k)
                d.kindsRej[kk] = (d.kindsRej[kk] or 0) + 1
            else
                d.nopos = d.nopos + 1
            end
            if why ~= "OWN" and #d.samples < C4DIAG_SAMPLE_CAP then
                d.samples[#d.samples + 1] = string.format(
                    "id=%s k=%s o=%s t=%s why=%s",
                    tostring(u.id), tostring(u.k), tostring(u.on), tostring(u.t), why)
            end
        else
            d.pass = d.pass + 1
            local bestB, bestD2
            for _, b in ipairs(home) do
                local dd = dist2(u.x, u.y, b.x, b.y)
                if not bestD2 or dd < bestD2 then bestB, bestD2 = b, dd end
            end
            if bestD2 and bestD2 <= r2 then
                d.distPass = d.distPass + 1
            else
                d.distRej = d.distRej + 1
                if d.coordSamples < C4DIAG_MAX_COORD_SAMPLES then
                    d.coordSamples = d.coordSamples + 1
                    d.samples[#d.samples + 1] = string.format(
                        "id=%s k=%s o=%s t=%s why=DISTANCE cand=(%s,%s) bld=%s bpos=(%s,%s) d2=%s R2=%s",
                        tostring(u.id), tostring(u.k), tostring(u.on), tostring(u.t),
                        tostring(u.x), tostring(u.y),
                        bestB and tostring(bestB.id) or "-",
                        bestB and tostring(bestB.x) or "-",
                        bestB and tostring(bestB.y) or "-",
                        tostring(bestD2 or -1), tostring(r2))
                end
            end
        end
    end
    d.kindIn = d.cands - d.own - d.allied - d.neutral - d.civil
    if d.home == 0 then
        d.note = "NO_HOME_BUILDINGS"
    elseif d.pass == 0 then
        d.note = "FILTER_REJECT_ALL"
    elseif d.distPass == 0 then
        d.note = "DISTANCE_REJECT_ALL"
    elseif d.distRej == 0 then
        d.note = "INTRUDERS_OK"
    else
        d.note = "MIXED"
    end
    return d
end

local function c4diagLine(frame, aiName, d)
    local kr = {}
    for k, n in pairs(d.kindsRej) do kr[#kr + 1] = tostring(k) .. "=" .. tostring(n) end
    table.sort(kr)
    -- Act-side rejection histograms. HARV/MCV/ART land in actNonGroundRej and
    -- NOT in actKindRej, because that is where the production predicate puts
    -- them: a harvester passes `kindOk` (it is k=="unit") and is excluded by
    -- the separate type gates. Merging the two buckets would misreport which
    -- gate actually rejected the unit.
    local akr = {}
    for k, n in pairs(d.act.actKindRej) do
        akr[#akr + 1] = tostring(k) .. "=" .. tostring(n)
    end
    table.sort(akr)
    local agr = {}
    for k, n in pairs(d.act.actNonGroundRej) do
        agr[#agr + 1] = tostring(k) .. "=" .. tostring(n)
    end
    table.sort(agr)
    -- Single-token verdict so a log reader does not have to re-derive the
    -- funnel. Ordered from the earliest possible blocker to the latest, and
    -- it deliberately distinguishes "Act never got a candidate" from
    -- "Act got one and was refused", which is the whole point of the pass.
    local a = d.act
    local actNote
    if not a.ran then
        actNote = "ACT_NO_INTRUDERS"
    elseif a.ownTotal == 0 then
        actNote = "ACT_NO_OWN_UNITS"
    elseif a.ownIdle == 0 then
        actNote = "ACT_NO_IDLE"
    elseif a.ownOfficerHeld > 0 and a.ownEligible == 0 then
        actNote = "ACT_ALL_OFFICER_HELD"
    elseif a.ownEligible == 0 then
        actNote = "ACT_NO_ELIGIBLE"
    elseif a.inAssignR == 0 then
        actNote = "ACT_NONE_IN_RADIUS"
    elseif a.claimDenied > 0 then
        actNote = "ACT_CLAIM_DENIED"
    elseif a.orderFail > 0 then
        actNote = "ACT_ORDER_FAIL"
    else
        actNote = "ACT_ATTEMPTED"
    end
    return string.format(
        "house=%s home=%d units=%d R=%s R2=%s cands=%d "
            .. "own=%d allied=%d neutral=%d civil=%d kindIn=%d kind=%d nopos=%d "
            .. "pass=%d distRej=%d distPass=%d intr=%d kindsRej={%s} note=%s "
            .. "mismatch=%s "
            .. "ownTotal=%d ownIdle=%d ownOfficerHeld=%d ownKindRejected=%d "
            .. "ownNonGround=%d ownNoPos=%d ownEligible=%d inAssignR=%d "
            .. "claimDenied=%d orderFail=%d actKindRej={%s} actNonGroundRej={%s} "
            .. "actRan=%s actNote=%s",
        tostring(aiName), d.home, d.cands, tostring(d.R), tostring(d.R2), d.cands,
        d.own, d.allied, d.neutral, d.civil, d.kindIn, d.kind, d.nopos,
        d.pass, d.distRej, d.distPass, d.intr, table.concat(kr, ","),
        tostring(d.note), tostring(d.distPass ~= d.intr),
        a.ownTotal, a.ownIdle, a.ownOfficerHeld, a.ownKindRejected,
        a.ownNonGround, a.ownNoPos, a.ownEligible, a.inAssignR,
        a.claimDenied, a.orderFail, table.concat(akr, ","),
        table.concat(agr, ","), tostring(a.ran), actNote)
end

function SmartAI.Update(frame)
    local P = params()
    -- Match restart: frame counter went backwards (same pattern as
    -- Command Authority). Resets both the legacy scan gate and the
    -- capture-guard state so a new match starts clean.
    if frame < lastScanFrame then lastScanFrame = 0 end
    if frame < guardLastFrame then guardReset(); officerReset() end
    guardLastFrame = frame

    if frame - lastScanFrame < tickScanEvery() then return end
    lastScanFrame = frame

    local humanPlayer = House.GetPlayer()
    if not humanPlayer then return end

    -- One snapshot for the whole tick.
    local units, buildings = {}, {}
    do
        local okU, ul = pcall(World.GetUnits)
        if okU and type(ul) == "table" then
            for _, u in ipairs(ul) do
                local e = snapOf(u)
                if e then units[#units + 1] = e end
            end
        end
        local okB, bl = pcall(World.GetBuildings)
        if okB and type(bl) == "table" then
            for _, b in ipairs(bl) do
                local e = snapOf(b)
                if e then buildings[#buildings + 1] = e end
            end
        end
    end
    table.sort(units, function(a, b) return a.id < b.id end)
    table.sort(buildings, function(a, b) return a.id < b.id end)

    -- Per-scan allied cache (was: IsAlliedWith per pair, repeatedly).
    local alliedCache = {}
    local function allied(ha, hb)
        if ha == hb then return true end
        local ka = tostring(ha) .. "|" .. tostring(hb)
        local v = alliedCache[ka]
        if v == nil then
            local ok, res = pcall(ha.IsAlliedWith, ha, hb)
            v = (ok and res == true)
            alliedCache[ka] = v
        end
        return v
    end

    -- Group AI houses (identity-matched; name compare would collide on
    -- mirror-country houses sharing one GetName()).
    local aiHouses = {}
    do
        local okN, n = pcall(House.GetCount)
        if okN and type(n) == "number" then
            for idx = 0, n - 1 do
                local okH, h = pcall(House.GetByIndex, idx)
                if okH and h then
                    local okHu, isHu = pcall(h.IsHuman, h)
                    if okHu and isHu ~= true and not allied(h, humanPlayer)
                        and not util.is_neutral_house(h) then
                        aiHouses[#aiHouses + 1] = h
                    end
                end
            end
        end
    end

    if #aiHouses == 0 then return end

    -- Refresh the scan-gate cache for tickScanEvery (next frames).
    do
        local fresh = {}
        for _, h in ipairs(aiHouses) do fresh[h] = true end
        aiHousesCache = fresh
    end

    -- M2-C2 ARBITER (tick-claim registry): ONE authority deciding which
    -- site owns a unit THIS tick. Priority = file order below (codified
    -- status quo, not a rebalance — severity retuning is C4):
    --   GUARD > DEFENSE(+MARCH) > ESCORT > GARRISON > RALLY > RECALL > RAID.
    -- Every drafting site must tryClaim() before ordering and skip on
    -- conflict; the skipped site counts arbSkips for the per-tick ARBITER
    -- summary. Tick-local by construction: nothing survives the tick, so
    -- cross-match leakage is impossible and no reset entry is needed.
    -- Persistent role memory (escort/defense/guard/raid across ticks)
    -- stays in isOfficerAssigned — claims only stop SAME-tick double
    -- orders (proven gaps: RALLY↔RECALL on the idle-far pool when breach
    -- and presence coexist, GARRISON double-send across buildings).
    -- MARCH↔RAID need no arbitration: march pool and raid form both honor
    -- isOfficerAssigned and their gates (confirmed intrusion vs quiet
    -- home) are mutually exclusive — verified by audit 2026-09-25.
    local claimTick = {}
    local arbSkips = {}
    local function tryClaim(id, site)
        if claimTick[id] then
            arbSkips[site] = (arbSkips[site] or 0) + 1
            return false
        end
        claimTick[id] = site
        return true
    end

    -- STRATEGIC DEFEAT DETECTION (Phase 1: latch + order silence only).
    -- Contract: no Barracks AND no War Factory AND no MCV/CY → surrendered.
    -- Power/refinery/credits/tech/defenses/units/navy/airfield excluded.
    -- Reuses this tick's snapshot (no extra scans). Never calls engine
    -- defeat APIs (Phase 2 research pending). Latch is permanent for the
    -- match; cleared only by the restart path (officerReset).
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if not surrendered[aiHouse] then
            local nB, nW, nM = 0, 0, 0
            for _, e in ipairs(buildings) do
                if e.oh == aiHouse then
                    if typeIn(ANCHOR_BARRACKS, e.t) then nB = nB + 1 end
                    if typeIn(ANCHOR_WF, e.t) then nW = nW + 1 end
                    if typeIn(ANCHOR_MCV, e.t) then nM = nM + 1 end
                end
            end
            for _, e in ipairs(units) do
                if e.oh == aiHouse and e.k == "unit" and typeIn(ANCHOR_MCV, e.t) then
                    nM = nM + 1
                end
            end
            if nB == 0 and nW == 0 and nM == 0 then
                -- Futility gate (user ruling 2026-09-25): anchors-gone means
                -- the house CANNOT RECOVER; it surrenders only when fighting
                -- on is FUTILE too (remaining combat value < ratio of the
                -- strongest enemy's). Otherwise the house keeps full SmartAI
                -- support and the reason is logged throttled (DEFERRED).
                local ownFighters, ownValue = 0, 0
                local foeValue = {}
                for _, e in ipairs(units) do
                    if e.oh and not util.is_neutral_house(e.oh)
                        and not util.CIVIL_TYPES[e.t]
                        and (e.k == "unit" or e.k == "infantry"
                            or e.k == "aircraft")
                        and not HARVESTER_TYPES[e.t]
                        and not MCV_TYPES[e.t] and e.hasPos then
                        if e.oh == aiHouse then
                            ownFighters = ownFighters + 1
                            ownValue = ownValue + e.cost
                        else
                            local okA, al = pcall(e.oh.IsAlliedWith, e.oh, aiHouse)
                            if okA and al ~= true then
                                foeValue[e.oh] = (foeValue[e.oh] or 0) + e.cost
                            end
                        end
                    end
                end
                local topFoe = 0
                for _, v in pairs(foeValue) do
                    if v > topFoe then topFoe = v end
                end
                if ownFighters > 0 and topFoe > 0
                    and ownValue >= SURRENDER_FUTILITY_RATIO * topFoe then
                    if frame % SMARTAI_CENSUS_EVERY < P.scan then
                        dlog("SURRENDER_DEFERRED", frame, string.format(
                            "house=%s anchors=0/0/0 fighters=%d value=%d topfoe=%d",
                            tostring(aiHouse:GetName()), ownFighters,
                            ownValue, topFoe))
                    end
                    goto next_surrender
                end
                surrendered[aiHouse] = frame
                cmdState[aiHouse] = nil
                dlog("SURRENDER_DETECTED", frame, string.format(
                    "house=%s barracks=0 war_factory=0 mcv=0",
                    tostring(aiHouse:GetName())))
                -- Visible surrender: liquidate every own building through
                -- the standard engine sell path (one pass over this tick's
                -- snapshot; each call pcall-guarded). Runs while the house
                -- is fully functional, before the defeat call below.
                local sold, total = 0, 0
                for _, b in ipairs(buildings) do
                    if b.oh == aiHouse then
                        total = total + 1
                        local okS, res = pcall(b.u.Sell, b.u)
                        if okS and res == true then sold = sold + 1 end
                    end
                end
                dlog("SURRENDER_SELL", frame, string.format(
                    "house=%s sold=%d/%d", tostring(aiHouse:GetName()),
                    sold, total))
                -- Engine defeat transition: DISABLED BY DEFAULT (2026-09-26).
                --
                -- Runtime evidence (3/3 multi-house runs, probe-verified):
                -- calling HouseClass::Lose(false) on an AI house makes the
                -- engine LEAVE the hooked game main loop ~90 frames later,
                -- at BorrowedTime expiry, ending the whole match and exiting
                -- the process (code 0) even though every other house is
                -- untouched and undefeated. PRE/POST control-flow counters
                -- prove the call itself RETURNS every time and the detour is
                -- simply never entered again -- so this is the engine
                -- switching out of the game loop, not a crash and not our
                -- timer dying.
                --
                -- HouseClass::Lose() is an engine-INTERNAL defeat transition
                -- that expects the engine's own evaluator to drive it. Forcing
                -- it externally on a non-player house duplicates that
                -- transition with the wrong calling context and ends the
                -- scenario. Corroboration: in the 1v1 Phase-2 run the match
                -- also ended at expiry and IsWinner was NEVER set on the
                -- human -- not how a normal victory flow behaves.
                --
                -- The engine owns defeat. SmartAI's surrender is a DECISION
                -- (latch + order silence + base liquidation + HUD notice),
                -- which is exactly this mod's architecture: a runtime decision
                -- layer, no production/engine-state ownership.
                --
                -- Set to true only to reproduce the old behaviour for
                -- diagnosis; expect the match to end early.
                if SmartAI.SURRENDER_ENGINE_CALL then
                    local okLose, loseRes =
                        pcall(Engine.__SmartAILose, aiHouse)
                    dlog("LOSE_FALSE_CALLED", frame, string.format(
                        "house=%s result=%s", tostring(aiHouse:GetName()),
                        (okLose and loseRes == true) and "called" or "refused"))
                else
                    dlog("SURRENDER_NO_ENGINE_CALL", frame, string.format(
                        "house=%s defeated=0 reason=engine_defeat_owned_by_" ..
                        "engine; decision_only", tostring(aiHouse:GetName())))
                end
                local alert = string.format(
                    "[AI Commander - %s] Strategic defeat: production gone, standing down.",
                    tostring(aiHouse:GetName()))
                Engine.PrintMessage(alert)
                print("[LuaAPI] " .. alert)
            end
            ::next_surrender::
        end
    end

    -- COMMANDER recon (base): breach detection feeds the shared blackboard.
    -- pendingBreach is same-scan only (building userdata never persists).
    -- Officer runs before Commander acts, so the rally below sees fresh
    -- Officer assignments and stands off them (mutual coordination).
    --
    -- TRIGGER 2026-09-26: proximity, NOT HP. See RALLY_THREAT_R for why the
    -- old "below 85% max HP" rule was a sampling race that silently produced
    -- zero rallies in two consecutive live matches.
    --
    -- Aircraft ARE part of the threat set on purpose: the old HP rule caught
    -- bombing damage, and DEFENSE_KINDS excludes aircraft, so reusing the
    -- point-defense filter here would have silently dropped air cover.
    -- Harvesters and MCVs are excluded (economy and construction, not
    -- attackers); civilians and non-defenders never count.
    --
    -- Most-threatened building wins (smallest hostile distance, id
    -- tiebreak) so the rally goes to the flank actually being hit instead of
    -- whichever building happened to come first in snapshot order.
    --
    -- RALLYDIAG (diagnostic only) reports every stage of the rally causal
    -- chain so a missing RALLY_BREACH names the stage that stopped it.
    local pendingBreach, rallyDiag = {}, {}
    for _, aiHouse in ipairs(aiHouses) do
        local dg = {
            own = 0, threatened = 0, bestD2 = 0, bId = 0, bType = "",
            poolOwn = 0, poolIdle = 0, poolFar = 0, poolFree = 0,
            poolFresh = 0, claims = 0, orders = 0,
            sameSpot = false, capElapsed = false, since = 0,
        }
        rallyDiag[aiHouse] = dg
        local bestB
        for _, bld in ipairs(buildings) do
            if bld.oh == aiHouse and bld.hasPos then
                dg.own = dg.own + 1
                local nearD2
                for _, u in ipairs(units) do
                    if u.oh and u.oh ~= aiHouse and not allied(aiHouse, u.oh)
                        and not util.is_neutral_house(u.oh)
                        and not util.CIVIL_TYPES[u.t]
                        and not HARVESTER_TYPES[u.t]
                        and not MCV_TYPES[u.t]
                        and (u.k == "unit" or u.k == "infantry"
                            or u.k == "aircraft")
                        and u.hasPos then
                        local d2 = dist2(bld.x, bld.y, u.x, u.y)
                        if d2 <= RALLY_THREAT_R2 and (not nearD2 or d2 < nearD2) then
                            nearD2 = d2
                        end
                    end
                end
                if nearD2 then
                    dg.threatened = dg.threatened + 1
                    if not bestB or nearD2 < dg.bestD2
                        or (nearD2 == dg.bestD2 and bld.id < bestB.id) then
                        bestB, dg.bestD2 = bld, nearD2
                    end
                end
            end
        end
        if bestB then
            pendingBreach[aiHouse] = bestB
            dg.bId, dg.bType = bestB.id, tostring(bestB.t)
        end
    end

    -- OFFICER (units): maneuvers, pullbacks, observation. Never touches base.
    -- Capture-aware valuables guard (see header block). Per AI house:
    -- valuable unit + nearby observable capturer + lone exposure
    -- -> pull back toward the own group (MoveTo only, once per
    -- transition + periodic refresh). Engaged units are never yanked.
    -- Owner flips are logged as observations (possible vanilla
    -- capture/mind-control); they drive no decisions.
    local seenNow = {}
    for _, e in ipairs(units) do
        seenNow[e.id] = true
        if e.on then
            local prev = seenOwner[e.id]
            if prev and prev ~= e.on then
                local note = string.format(
                    "[AI Commander] Ownership change observed: %s#%s %s -> %s (possible vanilla capture/mind-control)",
                    tostring(e.t), tostring(e.id),
                    tostring(prev), tostring(e.on))
                Engine.PrintMessage(note)
                print("[LuaAPI] " .. note)
            end
            seenOwner[e.id] = e.on
        end
    end
    for _, e in ipairs(buildings) do
        seenNow[e.id] = true
    end
    for id in pairs(seenOwner) do
        if not seenNow[id] then seenOwner[id] = nil end
    end
    for id in pairs(guardState) do
        if not seenNow[id] then guardState[id] = nil end
    end

    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_guard end -- Phase 1: silence
        -- Own mobiles, ID-ordered (deterministic centroid). The centroid is
        -- combat-only: harvesters/MCVs/artillery staging elsewhere must not
        -- drag the "own group" anchor (old code averaged every mobile).
        local tagged = {}
        for _, e in ipairs(units) do
            if e.oh == aiHouse and e.hasPos then
                tagged[#tagged + 1] = e
            end
        end
        local cx, cy, n = 0, 0, 0
        for _, e in ipairs(tagged) do
            if not HARVESTER_TYPES[e.t] and not MCV_TYPES[e.t]
                and not ARTILLERY_TYPES[e.t] then
                cx, cy, n = cx + e.x, cy + e.y, n + 1
            end
        end
        if P.guard and n > 0 then
            cx, cy = cx / n, cy / n
            for _, e in ipairs(tagged) do
                if VALUABLE_TYPES[e.t] and e.idle then
                    -- Nearest observable hostile capturer.
                    local nearest = nil
                    for _, v in ipairs(units) do
                        if v.id ~= e.id and CAPTURE_THREATS[v.t] then
                            if v.oh and v.oh ~= aiHouse and not allied(aiHouse, v.oh)
                                and v.hasPos then
                                local d2 = dist2(e.x, e.y, v.x, v.y)
                                if not nearest or d2 < nearest then
                                    nearest = d2
                                end
                            end
                        end
                    end
                    local exposed = e.hasPos and
                        (dist2(e.x, e.y, cx, cy) > HOLD_RADIUS * HOLD_RADIUS)
                    local nearestCells = nearest and math.sqrt(nearest) or nil
                    local st = guardState[e.id]
                    if nearestCells and nearestCells <= THREAT_RADIUS and exposed then
                        if not st or not st.threatened
                            or (frame - (st.orderedFrame or 0) >= REORDER_EVERY) then
                            if tryClaim(e.id, "GUARD")
                                and orderMove(e, math.floor(cx + 0.5), math.floor(cy + 0.5)) then
                                diagOrder(frame, "GUARD_PULLBACK", e, "MoveTo",
                                    string.format("dest=%d,%d threat=%.1f",
                                        math.floor(cx + 0.5), math.floor(cy + 0.5),
                                        nearestCells or -1))
                                guardState[e.id] = { threatened = true, orderedFrame = frame }
                                local alert = string.format(
                                    "[AI Commander - %s] %s#%s capture risk (threat %.1f cells): pulling back to group!",
                                    tostring(aiHouse:GetName()), tostring(e.t),
                                    tostring(e.id), nearestCells)
                                Engine.PrintMessage(alert)
                                print("[LuaAPI] " .. alert)
                            end
                        end
                    elseif st and st.threatened then
                        guardState[e.id] = nil
                    end
                end
            end
        end
        ::next_guard::
    end

    -- C4 PRESET/LAYER DIAGNOSTIC (DIAGNOSTIC ONLY). Emitted ABOVE the
    -- `if P.defense` gate and above the per-house `not P.intruderR` gate on
    -- purpose. The C4DIAG funnel below sits INSIDE those gates, so it can
    -- never report the case where the layer is switched off -- which is
    -- exactly the case a live 23 400-frame match hit (C4DIAG=0, no
    -- DEFENSE_SEV, no POINT_DEFENSE, player standing on the AI base).
    -- This line reports, per house: the global preset, the per-house
    -- preset, the raw engine difficulty, whether intruderR exists, and the
    -- resulting layer state, so a disabled layer is visible instead of
    -- silent. Bounded to one line per house per SMARTAI_CENSUS_EVERY.
    if (frame % SMARTAI_CENSUS_EVERY) < tickScanEvery() then
        local outerName = c4PresetName(P)
        for _, aiHouse in ipairs(aiHouses) do
            local ph = paramsFor(aiHouse)
            local innerName = c4PresetName(ph)
            local isSurr = surrendered[aiHouse] ~= nil
            local state
            if not P.defense then state = "OFF_NO_DEFENSE"
            elseif not ph.intruderR then state = "OFF_NO_INTRUDER_R"
            elseif isSurr then state = "OFF_SURRENDERED"
            else state = "ON" end
            dlog("C4PRESET", frame, string.format(
                "aiHouses=%d house=%s outer=%s preset=%s difficulty=%s R=%s "
                    .. "def=%s r=%s surr=%s state=%s diverge=%s funnel=%s",
                #aiHouses, tostring(aiHouse:GetName()), outerName, innerName,
                c4RawDifficulty(aiHouse),
                ph.intruderR and tostring(ph.intruderR) or "nil",
                tostring(P.defense == true),
                tostring(ph.intruderR ~= nil),
                tostring(isSurr),
                state, tostring(innerName ~= outerName),
                tostring(state == "ON")))
        end
    end

    -- POINT DEFENSE (per AI house, runs FIRST so home defense outranks
    -- escort drafting): intruders are hostile ground mobiles inside
    -- intruderR of any own building. Tier 1 assigns idle defenders real
    -- Attack orders (transition-only + refresh cap). Tier 2 (march recall)
    -- pulls marching ground units home once the intrusion is CONFIRMED over
    -- consecutive scans; fighting units (IsAttacking) are never yanked,
    -- unknown attack state means skip. Defender bookkeeping feeds
    -- isOfficerAssigned, so escort and rally below stand off defenders.
    if P.defense then
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if not P.intruderR then goto next_defense end -- easy preset: layer off
        if surrendered[aiHouse] then goto next_defense end -- Phase 1: silence
        local aiName = aiHouse:GetName()
        local home = {}
        for _, b in ipairs(buildings) do
            if b.oh == aiHouse and b.hasPos then home[#home + 1] = b end
        end
        -- C4 detector diagnostic (throttled, DIAGNOSTIC ONLY). Deliberately
        -- placed BEFORE the `#home > 0` gate: a house with no own buildings
        -- skips detection entirely and also yields zero intruders, and that
        -- cause must be distinguishable from a filter or distance failure.
        local c4diag = (frame % SMARTAI_CENSUS_EVERY) < P.scan
        local c4d
        if c4diag then c4d = c4diagScan(units, home, aiHouse, P, allied) end
        if #home > 0 then
            local bx, by = 0, 0
            for _, b in ipairs(home) do bx, by = bx + b.x, by + b.y end
            bx, by = bx / #home, by / #home

            local intruders = {}
            for _, u in ipairs(units) do
                if u.oh and u.oh ~= aiHouse and not allied(aiHouse, u.oh)
                    and not util.is_neutral_house(u.oh)
                    and not util.CIVIL_TYPES[u.t]
                    and DEFENSE_KINDS[u.k] and u.hasPos then
                    for _, b in ipairs(home) do
                        if dist2(u.x, u.y, b.x, b.y) <= P.intruderR * P.intruderR then
                            intruders[#intruders + 1] = u
                            break
                        end
                    end
                end
            end
            -- Threat order (live-proven need: a lone defender must take
            -- the Mammoth, not the GI): suicide units first (DTRUCK
            -- one-shots buildings — threat != price), then vehicles before
            -- infantry, then pricey before cheap, then id ascending
            -- (deterministic).
            local function intruderRank(e)
                if SUICIDE_TYPES[e.t] then return -1 end
                if e.k == "unit" then return 0 end
                if e.k == "infantry" then return 1 end
                return 2
            end
            table.sort(intruders, function(a, b)
                local ra, rb = intruderRank(a), intruderRank(b)
                if ra ~= rb then return ra < rb end
                if a.cost ~= b.cost then return a.cost > b.cost end
                return a.id < b.id
            end)

            -- Hysteresis counters for the march recall.
            local seen = intruderSeen[aiHouse] or {}
            local cur = {}
            for _, it in ipairs(intruders) do
                local prev = seen[it.id]
                cur[it.id] = {
                    n = (prev and prev.n or 0) + 1,
                    lastFrame = frame,
                }
            end
            intruderSeen[aiHouse] = cur
            if #intruders > 0 then
                lastThreatFrame[aiHouse] = frame -- M2-C3 quiet gate input
            end
            -- C4 diagnostic (CASE D probe): the aggregate every downstream
            -- stage consumes, captured as formed and BEFORE severity reads
            -- #intruders. distPass in the mirror must equal this value; a
            -- mismatch means mirror and detector have diverged.
            if c4diag then c4d.intr = #intruders end
            -- M2-C4 SEVERITY: score the intrusion, allocate by level.
            -- LOW (scout): 1 defender per intruder + keep the last idle
            -- tank in reserve. NORMAL: defendersN as before. HIGH (push):
            -- defendersN+1 per intruder + 1.5x assign radius. Recomputed
            -- every tick from live intruders (re-evaluate by construction);
            -- level line logged on change only.
            local sevScore = 0.0
            for _, it in ipairs(intruders) do
                sevScore = sevScore + intruderSev(it)
            end
            local sevLevel = "NORMAL"
            if sevScore < SEV_LOW_BELOW then sevLevel = "LOW"
            elseif sevScore >= SEV_HIGH_FROM then sevLevel = "HIGH" end
            if #intruders == 0 then sevLevel = "NONE" end
            if defSevLevel[aiHouse] ~= sevLevel then
                defSevLevel[aiHouse] = sevLevel
                if sevLevel ~= "NONE" then
                    dlog("DEFENSE_SEV", frame, string.format(
                        "house=%s level=%s score=%.1f intruders=%d",
                        tostring(aiName), sevLevel, sevScore, #intruders))
                end
            end
            local sevN = P.defendersN
            local sevAssignR = P.assignR
            if sevLevel == "LOW" then sevN = 1
            elseif sevLevel == "HIGH" then
                sevN = P.defendersN + 1
                sevAssignR = P.assignR * 1.5
            end

            -- === DIAGNOSTIC ONLY: Act-side candidate selection ============
            -- Answers "intruders > 0 but POINT_DEFENSE = 0" by naming the
            -- exact gate that dropped each own unit. Pure reads: no claim,
            -- no order, no write to defenseState / dEff / intruderSeen /
            -- C2 state / candidate order. Runs only on a census frame.
            --
            -- Instrumenting the EXISTING scan rather than adding a second
            -- population scan: the verdict below re-evaluates the very same
            -- predicate, in the same order, over the same `units` list.
            --
            -- COUNTING SEMANTICS (deliberate, and the reason this is not a
            -- naive mirror of Tier 1): Tier 1 re-evaluates every own unit once
            -- PER INTRUDER, so a naive tally would report eligible=3 for one
            -- defender standing near three intruders. Here each unit is
            -- tallied exactly ONCE, at the furthest gate it reached for AT
            -- LEAST ONE current intruder, which keeps the funnel strictly
            -- monotonic (ownTotal -> ownIdle -> officer -> kind -> nonGround
            -- -> eligible -> inAssignR). Consequence: ownEligible is a LOWER
            -- BOUND on Act's effective pool, because a unit blocked for
            -- intruder A but free for intruder B is credited to B's verdict
            -- only. This mirrors the architecture rather than correcting it.
            --
            -- `takenDef` is intentionally NOT a gate here: it is transient
            -- per-pass bookkeeping for units already drafted in this sweep,
            -- not a property of the unit.
            if c4diag and c4d and #intruders > 0 then
                local a = c4d.act
                a.ran = true
                for _, u in ipairs(units) do
                    if u.oh == aiHouse then
                        a.ownTotal = a.ownTotal + 1
                        if u.idle then
                            a.ownIdle = a.ownIdle + 1
                            -- Verdict rank, furthest reached across all
                            -- current intruders: 1 officer, 2 kind, 3 nonGround,
                            -- 4 no position, 5 eligible-out-of-radius,
                            -- 6 eligible-in-radius.
                            local rank, why = 0, nil
                            -- Hoisted: isOfficerAssigned does not depend on
                            -- the intruder, and holdingIt is only consulted
                            -- when the unit is actually held.
                            local held = isOfficerAssigned(u.id)
                            for _, it in ipairs(intruders) do
                                local v, vwhy
                                if held then
                                    local ds0 = defenseState[u.id]
                                    local holdingIt = ds0 and ds0.targetId == it.id
                                        and frame - (ds0.orderedFrame or 0) < P.defenseEvery
                                    if not holdingIt then
                                        v, vwhy = 1, "OFFICER"
                                    end
                                end
                                if not v then
                                    local airJob = isAirThreat(it)
                                    -- Production kind predicate, unchanged.
                                    local kindOk = (u.k == "unit" and not AIR_RAIDERS[u.t])
                                        or (airJob and u.k == "infantry" and AA_TYPES[u.t])
                                    if not kindOk then
                                        if u.k == "infantry" then
                                            v, vwhy = 2, "INF"
                                        elseif u.k == "aircraft" then
                                            v, vwhy = 2, "AIR"
                                        elseif AIR_RAIDERS[u.t] then
                                            v, vwhy = 2, "AIRRAID"
                                        else
                                            v, vwhy = 2, "OTHER"
                                        end
                                    elseif HARVESTER_TYPES[u.t] then
                                        v, vwhy = 3, "HARV"
                                    elseif MCV_TYPES[u.t] then
                                        v, vwhy = 3, "MCV"
                                    elseif ARTILLERY_TYPES[u.t] then
                                        v, vwhy = 3, "ART"
                                    elseif not u.hasPos then
                                        v, vwhy = 4, "NOPOS"
                                    elseif dist2(u.x, u.y, it.x, it.y)
                                        <= sevAssignR * sevAssignR then
                                        v, vwhy = 6, "RADIUS"
                                    else
                                        v, vwhy = 5, "OUT_OF_RADIUS"
                                    end
                                end
                                if v and v > rank then rank, why = v, vwhy end
                            end
                            if rank == 1 then
                                a.ownOfficerHeld = a.ownOfficerHeld + 1
                            elseif rank == 2 then
                                a.ownKindRejected = a.ownKindRejected + 1
                                a.actKindRej[why] = (a.actKindRej[why] or 0) + 1
                            elseif rank == 3 then
                                a.ownNonGround = a.ownNonGround + 1
                                a.actNonGroundRej[why] = (a.actNonGroundRej[why] or 0) + 1
                            elseif rank == 4 then
                                a.ownNoPos = a.ownNoPos + 1
                            else
                                a.ownEligible = a.ownEligible + 1
                                if rank == 6 then a.inAssignR = a.inAssignR + 1 end
                            end
                        end
                    end
                end
            end

            -- === M2-C4 ADAPT: Act -> Observe result -> Adapt =============
            -- Act (above): Tier 1 drafted sevN idle defenders onto intruders.
            -- Observe (end of Update): each finished assignment was resolved
            -- into a win or a loss for this house (dEff).
            -- Adapt (here): the defence is MEASURABLY losing its exchanges,
            -- so the response changes class -- instead of one more idle
            -- defender (there are none: measured poolIdle=0), commit the
            -- marching units that vanilla left heading somewhere else.
            --
            -- Claims as "MARCH": the documented arbiter order already folds
            -- MARCH into the DEFENSE tier (GUARD > DEFENSE(+MARCH) > ...),
            -- so this needs no priority change and correctly loses to GUARD.
            -- NOTE: `and` binds looser than `>=` in Lua, so `eff and
            -- eff.streak >= N` would parse as `(eff and eff.streak) >= N`
            -- and throw "attempt to compare nil with number" on the very
            -- first scan, before any episode has resolved. Read the streak
            -- into a local first.
            local eff = dEff[aiHouse]
            local streak = (eff and eff.streak) or 0
            if eff and #intruders > 0 and streak >= ADAPT_STREAK_TRIGGER
                and frame >= (eff.adaptUntil or 0) then
                eff.adaptUntil = frame + P.defenseEvery
                local poolFar, poolBusy, poolTaken, poolOfficer = 0, 0, 0, 0
                for _, u in ipairs(units) do
                    if u.oh == aiHouse and u.k == "unit" and u.hasPos
                        and not HARVESTER_TYPES[u.t] and not MCV_TYPES[u.t]
                        and not ARTILLERY_TYPES[u.t] then
                        if dist2(u.x, u.y, bx, by) > ADAPT_PULL_MIN_DIST * ADAPT_PULL_MIN_DIST then
                            poolFar = poolFar + 1
                            if not u.idle and u.attacking == false then
                                poolBusy = poolBusy + 1
                                if isOfficerAssigned(u.id) then
                                    poolOfficer = poolOfficer + 1
                                elseif tryClaim(u.id, "MARCH")
                                    and orderMove(u, math.floor(bx + 0.5), math.floor(by + 0.5)) then
                                    poolTaken = poolTaken + 1
                                    eff.pulled = (eff.pulled or 0) + 1
                                    diagOrder(frame, "ADAPT_RECALL", u, "MoveTo",
                                        string.format("reason=DEFENSE_LOSING streak=%d "
                                            .. "w=%d l=%d dest=%d,%d",
                                            eff.streak, eff.w, eff.l,
                                            math.floor(bx + 0.5), math.floor(by + 0.5)))
                                end
                            end
                        end
                    end
                end
    if poolTaken > 0 then
        dEffStats.pulls = dEffStats.pulls + poolTaken
                else
                    dEffStats.denied = dEffStats.denied + 1
                end
                -- Success is an EVENT, not a state: never throttled, so a
                -- grep for this tag cannot conclude the adaptation is dead
                -- (the RALLYDIAG lesson).
                dlog("ADAPT", frame, string.format(
                    "house=%s streak=%d w=%d l=%d intruders=%d sev=%s "
                        .. "poolFar=%d poolBusy=%d poolOfficer=%d pulled=%d",
                    tostring(aiName), eff.streak, eff.w, eff.l, #intruders,
                    tostring(sevLevel), poolFar, poolBusy, poolOfficer, poolTaken))
            end
            eff = nil
            local confirmed = false
            for _, c in pairs(cur) do
                if c.n >= P.confirmN then confirmed = true; break end
            end

            -- Tier 1: idle defenders -> Attack (nearest first, sevN per
            -- intruder). Holders keep their slots first (deterministic
            -- exact cap); newcomers fill the rest. Units holding THIS
            -- target pass the assigned filter (they are on this job, not
            -- busy elsewhere) — other role memories still exclude.
            local takenDef = {}
            for _, it in ipairs(intruders) do
                local airJob = isAirThreat(it)
                local cands = {}
                for _, u in ipairs(units) do
                    local ds0 = defenseState[u.id]
                    local holdingIt = ds0 and ds0.targetId == it.id
                        and frame - (ds0.orderedFrame or 0) < P.defenseEvery
                    -- Infantry drafts ONLY as AA vs air (FLAKT); all other
                    -- infantry stays out of the vehicle pool as before.
                    -- Bombers (ZEP/KIROV) never intercept: they are strike
                    -- assets, and ordering a Kirov onto a tank wastes it
                    -- (proven live: POINT_DEFENSE ZEP->HTNK while the base
                    -- waited). Jets (aircraft kind) were never eligible.
                    local kindOk = (u.k == "unit" and not AIR_RAIDERS[u.t])
                        or (airJob and u.k == "infantry" and AA_TYPES[u.t])
                    if u.oh == aiHouse and u.idle
                        and (not isOfficerAssigned(u.id) or holdingIt)
                        and not takenDef[u.id] and kindOk
                        and not HARVESTER_TYPES[u.t] and not MCV_TYPES[u.t]
                        and not ARTILLERY_TYPES[u.t] and u.hasPos then
                        local d2 = dist2(u.x, u.y, it.x, it.y)
                        if d2 <= sevAssignR * sevAssignR then
                            cands[#cands + 1] = { u = u, d2 = d2 }
                        end
                    end
                end
                table.sort(cands, function(a, b)
                    if airJob then
                        -- AA guns first vs air (a Rhino cannot hit a Kirov);
                        -- no-AA fallback keeps the old nearest order.
                        local aa, ab = AA_TYPES[a.u.t], AA_TYPES[b.u.t]
                        if (aa and true or false) ~= (ab and true or false) then
                            return (aa and true or false)
                        end
                    end
                    if a.d2 ~= b.d2 then return a.d2 < b.d2 end
                    return a.u.id < b.u.id
                end)
                local assigned = 0
                for _, c in ipairs(cands) do
                    local ds = defenseState[c.u.id]
                    if ds and ds.targetId == it.id
                        and frame - (ds.orderedFrame or 0) < P.defenseEvery then
                        takenDef[c.u.id] = true -- holding: no re-issue
                        assigned = assigned + 1 -- holder covers the slot
                    end
                end
                for _, c in ipairs(cands) do
                    if not takenDef[c.u.id] and assigned < sevN then
                        -- DIAGNOSTIC: reuse the REAL results of the calls that
                        -- were going to happen anyway. tryClaim/orderAttack
                        -- are never invoked a second time just to measure, and
                        -- the short-circuit is preserved: orderAttack runs
                        -- only when the claim succeeded, exactly as
                        -- `tryClaim(...) and orderAttack(...)` did, so an
                        -- orderFail is only ever counted for an order that was
                        -- actually attempted.
                        local claimed = tryClaim(c.u.id, "DEFENSE")
                        local ordered = claimed and orderAttack(c.u, it.u)
                        if c4diag and c4d then
                            local a2 = c4d.act
                            if not claimed then
                                if not a2.claimIds[c.u.id] then
                                    a2.claimIds[c.u.id] = true
                                    a2.claimDenied = a2.claimDenied + 1
                                end
                            elseif not ordered then
                                if not a2.orderIds[c.u.id] then
                                    a2.orderIds[c.u.id] = true
                                    a2.orderFail = a2.orderFail + 1
                                end
                            end
                        end
                        if claimed and ordered then
                                    defenseState[c.u.id] = {
                                        targetId = it.id, orderedFrame = frame,
                                        -- M2-C4: the owning house, so the
                                        -- end-of-Update episode resolution
                                        -- can credit the win/loss to the
                                        -- right ledger.
                                        house = aiHouse,
                                    }
                            takenDef[c.u.id] = true
                            assigned = assigned + 1
                            diagOrder(frame, "POINT_DEFENSE", c.u, "Attack",
                                string.format("target=%s type=%s", tostring(it.id), tostring(it.t)))
                            local actual = readbackTargetId(c.u.u)
                            dlog("READBACK", frame, string.format(
                                "unit=%s expected_target=%s actual_target=%s match=%s",
                                tostring(c.u.id), tostring(it.id), tostring(actual),
                                tostring(actual == it.id)))
                            if actual ~= nil and actual ~= it.id then
                                dlog("READBACK_MISMATCH", frame, string.format(
                                    "unit=%s expected_target=%s actual_target=%s",
                                    tostring(c.u.id), tostring(it.id), tostring(actual)))
                                divLogged[c.u.id] = actual
                            end
                        end
                    end
                end
                if assigned > 0 then
                    local alert = string.format(
                        "[AI Commander - %s] Base defense: %d defender(s) on %s#%s!",
                        tostring(aiName), assigned, tostring(it.t), tostring(it.id))
                    Engine.PrintMessage(alert)
                    print("[LuaAPI] " .. alert)
                end
            end

            -- Tier 2: confirmed intrusion pulls marching ground units home.
            if confirmed and P.marchRecall then
                for _, u in ipairs(units) do
                    if u.oh == aiHouse and not u.idle and u.attacking == false
                        and u.k == "unit"
                        and not HARVESTER_TYPES[u.t] and not MCV_TYPES[u.t]
                        and not ARTILLERY_TYPES[u.t]
                        and not isOfficerAssigned(u.id) and u.hasPos
                        and dist2(u.x, u.y, bx, by) > RECALL_FAR * RECALL_FAR then
                        local rs = recallState[u.id]
                        if not rs or frame - rs.orderedFrame >= P.redirectEvery then
                            if tryClaim(u.id, "MARCH")
                                and orderMove(u, math.floor(bx + 0.5), math.floor(by + 0.5)) then
                                recallState[u.id] = { orderedFrame = frame }
                                diagOrder(frame, "MARCH_RECALL", u, "MoveTo",
                                    string.format("reason=CONFIRMED_INTRUSION dest=%d,%d",
                                        math.floor(bx + 0.5), math.floor(by + 0.5)))
                                local alert = string.format(
                                    "[AI Commander - %s] Base under attack: recalling %s#%s home!",
                                    tostring(aiName), tostring(u.t), tostring(u.id))
                                Engine.PrintMessage(alert)
                                print("[LuaAPI] " .. alert)
                            end
                        end
                    end
                end
            end

            -- Divergence watch (diagnostic only: never re-orders). For each
            -- live defender, compare the commanded target against the
            -- engine's reported target; log only on new divergence.
            -- The CTX line adds cause evidence WITHOUT new behavior:
            -- expected/actual liveness (dead expected = natural completion),
            -- actual identity, defender mission (existing GetMission), and
            -- whether SmartAI itself re-issued to this unit since.
            for did, ds in pairs(defenseState) do
                local def = findSnap(units, did)
                if def and def.oh == aiHouse then
                    local actual = readbackTargetId(def.u)
                    if actual ~= nil and actual ~= ds.targetId then
                        local expE = findSnap(units, ds.targetId)
                        if divLogged[did] ~= actual then
                            divLogged[did] = actual
                            dlog("TARGET_DIVERGENCE", frame, string.format(
                                "unit=%s commanded=%s actual=%s",
                                tostring(did), tostring(ds.targetId), tostring(actual)))
                            local actE = findSnap(units, actual)
                            local okM, mission = pcall(def.u.GetMission, def.u)
                            dlog("TARGET_DIVERGENCE_CTX", frame, string.format(
                                "unit=%s owner=%s type=%s commanded=%s expAlive=%s actual=%s actAlive=%s actType=%s actKind=%s actOwner=%s mission=%s reissue=%s",
                                tostring(did), tostring(def.on), tostring(def.t),
                                tostring(ds.targetId), tostring(expE ~= nil),
                                tostring(actual), tostring(actE ~= nil),
                                tostring(actE and actE.t), tostring(actE and actE.k),
                                tostring(actE and actE.on),
                                (okM and mission ~= nil) and tostring(mission) or "nil",
                                lastSmartAIOrder(did, ds.orderedFrame)))
                        end
                        -- Lease-lite: re-assert a live commanded target the
                        -- vanilla AI yanked (bounded), then release it.
                        -- Runs EVERY diverged scan (not just newly logged).
                        if expE and (ds.retries or 0) < LEASE_RETRIES
                            and frame - (ds.orderedFrame or 0) >= LEASE_RETRY_EVERY
                            and lastSmartAIOrder(did, ds.orderedFrame) == "none"
                            and tryClaim(did, "LEASE")
                            and orderAttack(def, expE.u) then
                            ds.orderedFrame = frame
                            ds.retries = (ds.retries or 0) + 1
                            dlog("LEASE_REASSERT", frame, string.format(
                                "unit=%s commanded=%s retry=%d",
                                tostring(did), tostring(ds.targetId), ds.retries))
                        elseif expE and (ds.retries or 0) >= LEASE_RETRIES
                            and not ds.released then
                            ds.released = true
                            dlog("LEASE_RELEASED", frame, string.format(
                                "unit=%s commanded=%s reason=RETRIES_EXHAUSTED",
                                tostring(did), tostring(ds.targetId)))
                        end
                    elseif actual == ds.targetId then
                        divLogged[did] = nil
                    end
                end
            end
        else
            intruderSeen[aiHouse] = nil
        end
        -- C4 detector diagnostic emit (DIAGNOSTIC ONLY). Single call site,
        -- after the #home branch, so NO_HOME_BUILDINGS and the ordinary
        -- funnel are reported the same way. Bounded to one line plus one
        -- sample line per house per SMARTAI_CENSUS_EVERY frames.
        if c4diag and c4d then
            dlog("C4DIAG", frame, c4diagLine(frame, aiName, c4d))
            if #c4d.samples > 0 then
                dlog("C4DIAG_SAMPLE", frame, string.format("house=%s n=%d | %s",
                    tostring(aiName), #c4d.samples,
                    table.concat(c4d.samples, " | ")))
            end
            -- M2-C4 ADAPT: the feedback loop's own state, so Act -> Observe
            -- result -> Adapt is legible in one line: what the house has
            -- won/lost, the streak that arms the adaptation, and whether the
            -- adaptation is currently allowed to pull.
            local e3 = dEff[aiHouse]
            local st3 = (e3 and e3.streak) or 0
    dlog("ADAPTSTATE", frame, string.format(
        "house=%s w=%d l=%d streak=%d armed=%s adaptUntil=%d "
            .. "totW=%d totL=%d totDraw=%d totPulls=%d totDenied=%d",
        tostring(aiName),
        (e3 and e3.w) or 0, (e3 and e3.l) or 0, st3,
        tostring(st3 >= ADAPT_STREAK_TRIGGER),
        (e3 and e3.adaptUntil) or 0,
        dEffStats.w, dEffStats.l, dEffStats.draw, dEffStats.pulls,
        dEffStats.denied))
        end
        ::next_defense::
    end
    end -- P.defense

    -- Commander + Officer share this scan (no extra World calls).
    -- Split: Commander takes base defense, Officer takes unit escort.
    local v3ids, defids = {}, {}
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_officer end -- Phase 1: silence
        local aiName = aiHouse:GetName()
        local ownUnits, ownBlds = {}, {}
        for _, e in ipairs(units) do
            if e.oh == aiHouse and e.k == "unit" then
                ownUnits[#ownUnits + 1] = e
            end
        end
        for _, b in ipairs(buildings) do
            if b.oh == aiHouse then
                ownBlds[#ownBlds + 1] = b
            end
        end

        if P.escort then
        -- OFFICER: escort for own artillery (units business).
        local taken = {}
        for _, e in ipairs(ownUnits) do
            if ARTILLERY_TYPES[e.t] then
                if e.hasPos then
                    v3ids[e.id] = true
                    local st = escortState[e.id]
                    if not st then
                        st = { guards = {}, ax = e.x, ay = e.y, orderedFrame = 0,
                               houseName = aiName, typename = e.t }
                        escortState[e.id] = st
                    end
                    -- Commander priority (blackboard, previous scan): a V3
                    -- inside an active breach sector rallies with the base;
                    -- escort stands down, hygiene releases + logs below.
                    local br = cmdState[aiHouse]
                    local standDown = br and frame - br.frame < 120
                        and dist2(e.x, e.y, br.x, br.y) <= 225
                    st.standDown = standDown or nil
                    local live = {}
                    for _, gid in ipairs(st.guards) do
                        local g = findSnap(units, gid)
                        if g and g.idle then
                            live[#live + 1] = gid
                            taken[gid] = true
                        end
                    end
                    st.guards = live
                    local moved = dist2(e.x, e.y, st.ax, st.ay)
                        > ESCORT_RADIUS * ESCORT_RADIUS
                    local need = ESCORT_N - #live
                    if not st.standDown
                        and (need > 0 or moved or frame - st.orderedFrame >= ESCORT_EVERY) then
                        local cands = {}
                        for _, c in ipairs(ownUnits) do
                            if c.id ~= e.id and not taken[c.id] and c.idle
                                and not isOfficerAssigned(c.id)
                                and not HARVESTER_TYPES[c.t] and not MCV_TYPES[c.t]
                                and not ARTILLERY_TYPES[c.t] and c.hasPos then
                                local d2 = dist2(c.x, c.y, e.x, e.y)
                                if d2 <= ESCORT_DRAFT_RADIUS * ESCORT_DRAFT_RADIUS then
                                    cands[#cands + 1] = { c = c, d2 = d2 }
                                end
                            end
                        end
                        table.sort(cands, function(a, b)
                            if a.d2 ~= b.d2 then return a.d2 < b.d2 end
                            return a.c.id < b.c.id
                        end)
                        local added = 0
                        for i = 1, math.min(need > 0 and need or #live, #cands) do
                            local g = cands[i].c
                            if need > 0 then
                                if tryClaim(g.id, "ESCORT") and orderMove(g, e.x, e.y) then
                                    st.guards[#st.guards + 1] = g.id
                                    taken[g.id] = true
                                    added = added + 1
                                    need = need - 1
                                    diagOrder(frame, "ESCORT", g, "MoveTo",
                                        string.format("principal=%s type=%s",
                                            tostring(e.id), tostring(e.t)))
                                end
                            end
                        end
                        if need <= 0 and #live > 0 and added == 0 then
                            -- Follow refresh: V3 moved or cap elapsed, guards hold.
                            local held = {}
                            for _, gid in ipairs(live) do
                                local g = findSnap(units, gid)
                                if g and orderMove(g, e.x, e.y) then
                                    held[#held + 1] = tostring(gid)
                                end
                            end
                            if #held > 0 then
                                dlog("ORDER", frame, string.format(
                                    "site=ESCORT_REFRESH principal=%s guards=%s",
                                    tostring(e.id), table.concat(held, ",")))
                            end
                        end
                        if added > 0 or moved then
                            st.ax, st.ay, st.orderedFrame = e.x, e.y, frame
                            -- HUD only on real change: a moved V3 with no new
                            -- guards spammed "+0 bodyguard(s)" every refresh
                            -- live (noise, no information).
                            if added > 0 then
                                local alert = string.format(
                                    "[AI Commander - %s] %s#%s escort +%d bodyguard(s).",
                                    tostring(aiName), tostring(e.t), tostring(e.id), added)
                                Engine.PrintMessage(alert)
                                print("[LuaAPI] " .. alert)
                            end
                        elseif #live > 0 then
                            st.orderedFrame = frame
                        end
                    end
                end
            end
        end
        end -- P.escort

        if P.garrison then
        -- COMMANDER: garrison for own defenses (base business; EXPERIMENTAL).
        for _, e in ipairs(ownBlds) do
            if typeIn(DEFENSE_TYPES, e.t) then
                defids[e.id] = true
                local gs = garrisonState[e.id]
                if e.hasPos and (not gs or frame - gs.orderedFrame >= GARRISON_EVERY) then
                    local cands = {}
                    for _, u in ipairs(units) do
                        if u.k == "infantry" and u.idle and u.oh == aiHouse and u.hasPos then
                            local d2 = dist2(u.x, u.y, e.x, e.y)
                            if d2 <= GARRISON_RADIUS * GARRISON_RADIUS then
                                cands[#cands + 1] = { u = u, d2 = d2 }
                            end
                        end
                    end
                    table.sort(cands, function(a, b)
                        if a.d2 ~= b.d2 then return a.d2 < b.d2 end
                        return a.u.id < b.u.id
                    end)
                        local sent = 0
                        for i = 1, math.min(GARRISON_N, #cands) do
                            local cu = cands[i].u
                            if tryClaim(cu.id, "GARRISON") and orderMove(cu, e.x, e.y) then
                                sent = sent + 1
                                diagOrder(frame, "GARRISON", cu, "MoveTo",
                                    string.format("dest=%d,%d building=%s type=%s",
                                        e.x, e.y, tostring(e.id), tostring(e.t)))
                            end
                        end
                    if sent > 0 then
                        garrisonState[e.id] = { orderedFrame = frame }
                        local alert = string.format(
                            "[AI Commander - %s] GARRISON? %s (%d inf) -> %s#%s (experimental).",
                            tostring(aiName), tostring(e.t), sent,
                            tostring(e.t), tostring(e.id))
                        Engine.PrintMessage(alert)
                        print("[LuaAPI] " .. alert)
                    end
                end
            end
        end
        end -- P.garrison
        ::next_officer::
    end

    -- NOTE: point-defense layer runs BEFORE the Officer layer (see above):
    -- home defense claims idle defenders before escort drafts bodyguards.

    -- COMMANDER acts (base): rally, working around Officer assignments.
    -- Same-breach memory: a persisting breach orders only FRESH reserves
    -- (units rallied already are remembered per spot, never re-spammed);
    -- a NEW breach position rallies immediately. Full re-rally to the same
    -- spot happens at most every rallyEvery frames (stale orders die live).
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_rally end -- Phase 1: silence
        local aiName = aiHouse:GetName()
        local breachedBuilding = pendingBreach[aiHouse]
        if breachedBuilding and breachedBuilding.hp ~= nil then
            local bPos = breachedBuilding.hasPos and
                { x = breachedBuilding.x, y = breachedBuilding.y } or nil
            if bPos then
                local rs = rallyState[aiHouse]
                local sameSpot = rs and rs.x == bPos.x and rs.y == bPos.y
                local ids = (sameSpot and rs.ids) or {}
                local capElapsed = not sameSpot
                    or (frame - (rs.frame or 0) >= P.rallyEvery)
                local ralliedCount = 0
                -- RALLYDIAG counters (diagnostic only). Every condition below
                -- is the original one, unchanged; the counters just record
                -- how far each candidate unit survived the pool filter so a
                -- missing RALLY_BREACH names the stage that stopped it.
                -- Short-circuit order preserved: dist2 is evaluated only for
                -- idle units, isOfficerAssigned only for idle+far ones.
                local dg = rallyDiag[aiHouse]
                if dg then
                    dg.sameSpot = not not sameSpot
                    dg.capElapsed = not not capElapsed
                    dg.since = (rs and (frame - (rs.frame or 0))) or 0
                end
                for _, u in ipairs(units) do
                    if u.oh == aiHouse and u.k == "unit" and u.hasPos then
                        if dg then dg.poolOwn = dg.poolOwn + 1 end
                        if u.idle then
                            if dg then dg.poolIdle = dg.poolIdle + 1 end
                            if dist2(u.x, u.y, bPos.x, bPos.y) > 36.0 then
                                if dg then dg.poolFar = dg.poolFar + 1 end
                                if not isOfficerAssigned(u.id) then
                                    if dg then dg.poolFree = dg.poolFree + 1 end
                                    if capElapsed or not ids[u.id] then
                                        if dg then dg.poolFresh = dg.poolFresh + 1 end
                                        -- Command reserve tank to reinforce the
                                        -- threatened flank!
                                        -- NOTE: MoveTo+Hunt back-to-back is a
                                        -- KNOWN SUSPECT (HOW_TO_USE): Hunt may void
                                        -- MoveTo, leaving the reserve hunting from
                                        -- place. Kept until a live coordinate
                                        -- check decides; point defense above is the
                                        -- verified home-defense path (Attack).
                                        if tryClaim(u.id, "RALLY") then
                                            if dg then dg.claims = dg.claims + 1 end
                                            if orderMove(u, bPos.x, bPos.y) then
                                                local okH = pcall(u.u.Hunt, u.u)
                                                if okH then
                                                    ralliedCount = ralliedCount + 1
                                                    if dg then dg.orders = dg.orders + 1 end
                                                    ids[u.id] = true
                                                    diagOrder(frame, "RALLY_BREACH", u, "MoveTo+Hunt",
                                                        string.format("dest=%d,%d", bPos.x, bPos.y))
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
                rallyState[aiHouse] = { x = bPos.x, y = bPos.y, frame = frame, ids = ids }
                if ralliedCount > 0 then
                    local alert = string.format("\u{1F6A8} [AI Commander - %s] Flank breach at (%d,%d)! Rallied %d reserve tanks to counter-attack!",
                        aiName, bPos.x, bPos.y, ralliedCount)
                    Engine.PrintMessage(alert)
                    print("[LuaAPI] " .. alert)
                end
            end
            local bp = breachedBuilding.hasPos and
                { x = breachedBuilding.x, y = breachedBuilding.y } or nil
            if bp then
                cmdState[aiHouse] = { x = bp.x, y = bp.y, frame = frame }
                lastThreatFrame[aiHouse] = frame -- M2-C3 quiet gate input
            end
        else
            cmdState[aiHouse] = nil
        end
        -- RALLYDIAG (diagnostic only): name the stage of the rally causal
        -- chain that stopped, or confirm the order went out. Emitted inside
        -- the per-house loop (so it covers every non-surrendered house) on the
        -- same census cadence as the C4 diagnostics. A surrendered house is
        -- covered by C4PRESET state=OFF_SURRENDERED instead.
        --
        -- EXCEPTION: an order that actually went out is an EVENT, not a
        -- state, so it is logged UNTHROTTLED. Throttling it made 6 successful
        -- rallies produce zero ORDERED lines in a live match, and a grep for
        -- this tag then reads as "rally never fires" -- the reverse of the
        -- truth. Rallies are rare (single digits per match), so the volume is
        -- bounded anyway.
        local dg = rallyDiag[aiHouse]
        if dg and (frame % SMARTAI_CENSUS_EVERY) < P.scan then
            local why
            if not breachedBuilding then
                why = "NO_THREAT"
            elseif not breachedBuilding.hasPos then
                why = "NO_BPOS"
            elseif breachedBuilding.hp == nil then
                why = "NO_HP_FIELD"
            elseif dg.orders > 0 then
                why = "ORDERED"
            elseif dg.poolFresh == 0 then
                if dg.poolOwn == 0 then why = "POOL_NO_OWN_UNITS"
                elseif dg.poolIdle == 0 then why = "POOL_NONE_IDLE"
                elseif dg.poolFar == 0 then why = "POOL_ALL_AT_SPOT"
                elseif dg.poolFree == 0 then why = "POOL_ALL_ASSIGNED"
                else why = "COOLDOWN_NO_FRESH" end
            else
                why = "ORDER_FAILED"
            end
            dlog("RALLYDIAG", frame, string.format(
                "house=%s why=%s own=%d threatened=%d bld=%s type=%s d2=%d R2=%d "
                    .. "poolOwn=%d poolIdle=%d poolFar=%d poolFree=%d poolFresh=%d "
                    .. "claims=%d orders=%d sameSpot=%s capElapsed=%s since=%d",
                tostring(aiName), why, dg.own, dg.threatened,
                dg.bId > 0 and tostring(dg.bId) or "-", dg.bType,
                dg.bestD2, RALLY_THREAT_R2,
                dg.poolOwn, dg.poolIdle, dg.poolFar, dg.poolFree,
                dg.poolFresh, dg.claims, dg.orders,
                tostring(dg.sameSpot), tostring(dg.capElapsed), dg.since))
        end
        if dg and dg.orders > 0 then
            dlog("RALLYORDERED", frame, string.format(
                "house=%s bld=%s type=%s d2=%d orders=%d claims=%d poolFresh=%d",
                tostring(aiName), dg.bId > 0 and tostring(dg.bId) or "-",
                dg.bType, dg.bestD2, dg.orders, dg.claims, dg.poolFresh))
        end
        ::next_rally::
    end

    -- COMMANDER: base-threat recall (presence, not damage). Vanilla fires
    -- heavies on schedule with zero threat assessment; idle combat
    -- vehicles far from a threatened base come home (broadened from
    -- V3/Kirov-only: a parked enemy army must pull home every idle tank,
    -- not just artillery). Harvesters/MCVs are never recalled. Engaged
    -- units are never yanked — holding those needs an order lease.
    if P.recall then
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_recall end -- Phase 1: silence
        local aiName = aiHouse:GetName()
        local bx, by, bn = 0, 0, 0
        for _, b in ipairs(buildings) do
            if b.oh == aiHouse and b.hasPos then
                bx, by, bn = bx + b.x, by + b.y, bn + 1
            end
        end
        if bn > 0 then
            bx, by = bx / bn, by / bn
            local hostiles = 0
            for _, u in ipairs(units) do
                if u.oh and u.oh ~= aiHouse and not allied(aiHouse, u.oh)
                    and not util.is_neutral_house(u.oh)
                    and not util.CIVIL_TYPES[u.t] and u.hasPos
                    and dist2(u.x, u.y, bx, by)
                        <= RECALL_THREAT_RADIUS * RECALL_THREAT_RADIUS then
                    hostiles = hostiles + 1
                end
            end
            if hostiles >= RECALL_THREAT_N then
                for _, u in ipairs(units) do
                    if u.oh == aiHouse and u.idle and u.hasPos then
                        local recallable = RECALL_TYPES[u.t]
                            or (u.k == "unit"
                                and not HARVESTER_TYPES[u.t]
                                and not MCV_TYPES[u.t])
                        if recallable and not isOfficerAssigned(u.id) then
                            if dist2(u.x, u.y, bx, by) > RECALL_FAR * RECALL_FAR then
                                local rs = recallState[u.id]
                                if not rs or frame - rs.orderedFrame >= RECALL_EVERY then
                                    if tryClaim(u.id, "RECALL")
                                        and orderMove(u, math.floor(bx + 0.5), math.floor(by + 0.5)) then
                                        recallState[u.id] = { orderedFrame = frame }
                                        diagOrder(frame, "IDLE_RECALL", u, "MoveTo",
                                            string.format("reason=BASE_THREAT_%d dest=%d,%d",
                                                hostiles, math.floor(bx + 0.5), math.floor(by + 0.5)))
                                        local alert = string.format(
                                            "[AI Commander - %s] Base threat (%d hostiles): recalling %s#%s home!",
                                            tostring(aiName), hostiles,
                                            tostring(u.t), tostring(u.id))
                                        Engine.PrintMessage(alert)
                                        print("[LuaAPI] " .. alert)
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        ::next_recall::
    end
    end -- P.recall

    -- M2-C1 BELIEF UPDATE (grudge slice): HP drops and kills of own
    -- economy with an enemy near are attributed to the nearest hostile
    -- and counted per (aiHouse, enemyHouse). Gated on P.raid: memory
    -- without a consumer decision path is not kept. Event + update share
    -- one throttled line (per-victim cooldown); repairs/new sightings
    -- only refresh the prev-tick store, never count.
    if P.raid then
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_belief end
        local aiName = aiHouse:GetName()
        -- Per-house snapshot store. Created on first sight of the house so
        -- the disappearance-diff below can only ever see THIS house's
        -- economy (see econSeen decl for why the flat form was a bug).
        local store = econSeen[aiHouse]
        if not store then store = {}; econSeen[aiHouse] = store end
        local nowSeen = {}
        local pool = {}
        for _, u in ipairs(units) do pool[#pool + 1] = u end
        for _, b in ipairs(buildings) do pool[#pool + 1] = b end
        for _, e in ipairs(pool) do
            if e.oh == aiHouse and e.hasPos and isEconAsset(e) then
                nowSeen[e.id] = true
                local prev = store[e.id]
                if e.hp ~= nil and prev and prev.hp ~= nil and e.hp < prev.hp
                    and frame - (prev.eventFrame or 0) >= ECON_EVENT_EVERY then
                    -- Nearest hostile to the victim: attribution, else skip.
                    local culprit, culpritD2 = nil, nil
                    for _, u in ipairs(units) do
                        if u.oh and u.oh ~= aiHouse
                            and not allied(aiHouse, u.oh)
                            and not util.is_neutral_house(u.oh)
                            and not util.CIVIL_TYPES[u.t]
                            and DEFENSE_KINDS[u.k] and u.hasPos then
                            local d2 = dist2(e.x, e.y, u.x, u.y)
                            if d2 <= GRUDGE_ATTRIB_R * GRUDGE_ATTRIB_R
                                and (not culpritD2 or d2 < culpritD2) then
                                culprit, culpritD2 = u.oh, d2
                            end
                        end
                    end
                    if culprit then
                        local gh = grudge[aiHouse]
                        if not gh then gh = {}; grudge[aiHouse] = gh end
                        local g = gh[culprit]
                        if not g then g = { n = 0, lastFrame = 0 }; gh[culprit] = g end
                        g.n = g.n + 1
                        g.lastFrame = frame
                        prev.eventFrame = frame
                        local _, con = pcall(culprit.GetName, culprit)
                        dlog("BELIEF_EVENT", frame, string.format(
                            "house=%s victim=%s type=%s hp=%s->%s culprit=%s grudge=%d",
                            tostring(aiName), tostring(e.id), tostring(e.t),
                            tostring(prev.hp), tostring(e.hp),
                            tostring(con), math.min(g.n, GRUDGE_CAP)))
                    end
                end
                if e.hp ~= nil then
                    store[e.id] = {
                        hp = e.hp, x = e.x, y = e.y,
                        -- First sighting must not eat the cooldown: a drop
                        -- on the next scan is a real first event.
                        eventFrame = (prev and prev.eventFrame)
                            or -ECON_EVENT_EVERY,
                    }
                end
            end
        end
        -- ECON_KILLED: economy object gone with a hostile near its last pos.
        -- Iterates THIS house's store only. A flat shared store made every
        -- other house's intact economy report as "killed" here, every scan.
        for id, prev in pairs(store) do
            if not nowSeen[id] and prev.hp ~= nil and prev.eventFrame ~= nil then
                local culprit, culpritD2 = nil, nil
                for _, u in ipairs(units) do
                    if u.oh and u.oh ~= aiHouse
                        and not allied(aiHouse, u.oh)
                        and not util.is_neutral_house(u.oh)
                        and not util.CIVIL_TYPES[u.t]
                        and DEFENSE_KINDS[u.k] and u.hasPos and prev.x then
                        local d2 = dist2(prev.x, prev.y, u.x, u.y)
                        if d2 <= GRUDGE_ATTRIB_R * GRUDGE_ATTRIB_R
                            and (not culpritD2 or d2 < culpritD2) then
                            culprit, culpritD2 = u.oh, d2
                        end
                    end
                end
                if culprit and frame - prev.eventFrame >= ECON_EVENT_EVERY then
                    local gh = grudge[aiHouse]
                    if not gh then gh = {}; grudge[aiHouse] = gh end
                    local g = gh[culprit]
                    if not g then g = { n = 0, lastFrame = 0 }; gh[culprit] = g end
                    g.n = g.n + 1
                    g.lastFrame = frame
                    local _, con = pcall(culprit.GetName, culprit)
                    dlog("BELIEF_EVENT", frame, string.format(
                        "house=%s victim=%s type=killed culprit=%s grudge=%d",
                        tostring(aiName), tostring(id),
                        tostring(con), math.min(g.n, GRUDGE_CAP)))
                end
                store[id] = nil -- gone: prune either way (no leak)
            end
        end
        ::next_belief::
    end
    end -- P.raid belief

-- Raid value multiplier for candidates owned by a grudged house.
-- Returns mult + count (1.0 + 0 when no memory — decision unchanged).
-- Test-only inspector (harnesses only, no gameplay use): exposes the
-- M2-C1 belief tables so tests can assert memory without log scraping.
-- econSeen is nested per house (econSeen[house][id]); a caller that needs
-- the old flat id->snapshot view must iterate the per-house subtables.
-- Returns live tables — tests must read, never write.
function SmartAI.BeliefInspect()
    return grudge, econSeen
end

local function grudgeMult(aiHouse, candOwner)
    if not aiHouse or not candOwner then return 1.0, 0 end
    local gh = grudge[aiHouse]
    if not gh then return 1.0, 0 end
    local g = gh[candOwner]
    if not g or not g.n or g.n <= 0 then return 1.0, 0 end
    local n = math.min(g.n, GRUDGE_CAP)
    return 1.0 + GRUDGE_W * n, n
end

    -- RETALIATE (M14.1-line self-preservation, point-defense family):
    -- an AI combat unit losing HP while holding another target turns onto
    -- its assumed attacker (nearest hostile). Vanilla owns positioning;
    -- SmartAI corrects only the target (native Attack + readback), so the
    -- unit stays in combat instead of chasing choppers to death.
    -- Cooldown-gated (no re-issue inside the window), skips assigned
    -- roles and non-combat types. Gated on P.defense.
    if P.defense then
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_retal end
        for _, e in ipairs(units) do
            if e.oh == aiHouse and e.k == "unit" and e.hasPos
                and not HARVESTER_TYPES[e.t] and not MCV_TYPES[e.t]
                and not ARTILLERY_TYPES[e.t]
                and not isOfficerAssigned(e.id) then
                local prev = combatSeen[e.id]
                local cur = e.hp
                if prev and prev.hp and cur and cur < prev.hp
                    and frame - (prev.retalFrame or -RETALIATE_EVERY)
                        >= RETALIATE_EVERY then
                    local okT, tgt = pcall(e.u.GetTarget, e.u)
                    local curTid = nil
                    if okT and tgt then
                        local okI, tid = pcall(tgt.GetId, tgt)
                        if okI then curTid = tid end
                    end
                    local culpE = nil
                    local culpD2 = nil
                    for _, u in ipairs(units) do
                        if u.oh and u.oh ~= aiHouse
                            and not allied(aiHouse, u.oh)
                            and not util.is_neutral_house(u.oh)
                            and not util.CIVIL_TYPES[u.t]
                            and (u.k == "unit" or u.k == "infantry"
                                or u.k == "aircraft")
                            and u.hasPos then
                            local d2 = dist2(e.x, e.y, u.x, u.y)
                            if d2 <= RETALIATE_R * RETALIATE_R
                                and (not culpD2 or d2 < culpD2) then
                                culpE, culpD2 = u, d2
                            end
                        end
                    end
                    if culpE and culpE.id ~= curTid
                        and tryClaim(e.id, "RETALIATE")
                        and orderAttack(e, culpE.u) then
                        prev.retalFrame = frame
                        local actual = readbackTargetId(e.u)
                        dlog("RETALIATE", frame, string.format(
                            "house=%s unit=%s type=%s was=%s now=%s culprit=%s readback=%s",
                            tostring(aiHouse:GetName()), tostring(e.id),
                            tostring(e.t), tostring(curTid),
                            tostring(culpE.id), tostring(culpE.t),
                            tostring(actual)))
                    end
                end
                if cur then
                    combatSeen[e.id] = {
                        hp = cur,
                        retalFrame = (prev and prev.retalFrame)
                            or -RETALIATE_EVERY,
                    }
                end
            end
        end
        ::next_retal::
    end
    end -- P.defense retaliate

    -- BOMBER FOCUS (user case 2026-09-25): an AI bomber holding a troop
    -- target while an enemy building sits within FOCUS_R ignores the
    -- troops and goes for the base (nearest enemy building). Cooldown per
    -- bomber, readback, arbiter-claimed. Skips assigned roles (a raider
    -- already on economy keeps it). Bombers never intercept as defenders
    -- (Tier-1 filter above) — this is their only SmartAI steering.
    local BOMBER_FOCUS_R = 25
    local BOMBER_FOCUS_EVERY = 300
    if P.defense or P.raid then
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if surrendered[aiHouse] then goto next_focus end
        for _, e in ipairs(units) do
            if e.oh == aiHouse and e.hasPos
                and (AIR_RAIDERS[e.t] or e.k == "aircraft")
                and e.t ~= "V3"
                and not isOfficerAssigned(e.id) then
                local fs = focusState[e.id]
                if not fs or frame - (fs.orderedFrame or 0) >= BOMBER_FOCUS_EVERY then
                    local okT, tgt = pcall(e.u.GetTarget, e.u)
                    local curKind = nil
                    if okT and tgt then
                        local okK, kk = pcall(tgt.GetKind, tgt)
                        if okK then curKind = kk end
                    end
                    if curKind == "building" then goto next_bomber end
                    local best, bestD2 = nil, nil
                    for _, b in ipairs(buildings) do
                        if b.oh and b.oh ~= aiHouse
                            and not allied(aiHouse, b.oh)
                            and not util.is_neutral_house(b.oh)
                            and not util.CIVIL_TYPES[b.t] and b.hasPos then
                            local d2 = dist2(e.x, e.y, b.x, b.y)
                            if d2 <= BOMBER_FOCUS_R * BOMBER_FOCUS_R
                                and (not bestD2 or d2 < bestD2) then
                                best, bestD2 = b, d2
                            end
                        end
                    end
                    if best
                        and tryClaim(e.id, "FOCUS")
                        and orderAttack(e, best.u) then
                        focusState[e.id] = { orderedFrame = frame }
                        local actual = readbackTargetId(e.u)
                        dlog("BOMBER_FOCUS", frame, string.format(
                            "house=%s unit=%s type=%s was=%s now=%s(%s) readback=%s",
                            tostring(aiHouse:GetName()), tostring(e.id),
                            tostring(e.t), tostring(curKind),
                            tostring(best.id), tostring(best.t),
                            tostring(actual)))
                    end
                end
                ::next_bomber::
            end
        end
        ::next_focus::
    end
    end

    -- M2-C3 RAIDER FORCE (first decision-system slice): when home is quiet
    -- (no intruders/breach for raidQuiet frames), idle combat surplus forms
    -- ONE hunter group that snipes enemy economy (refinery > power > tech >
    -- AA > harvester) by value, nearest-tiebreak, id-tiebreak. Unfavourable
    -- contact (hostiles near raid centroid >= members × mult) retreats the
    -- group home instead of dying in place. Members are id-tracked and count
    -- as officer-assigned, so escort/rally/recall stand off them.
    -- Easy preset: raid off (rally only). v1 scope: economy only, no meat.
    if P.raid then
    for _, aiHouse in ipairs(aiHouses) do
        local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
        if not P.raidEvery then goto next_raid end -- easy preset: raid off
        if surrendered[aiHouse] then
            if raidGroups[aiHouse] then
                raidGroups[aiHouse]:reset()
                raidGroups[aiHouse] = nil
            end
            goto next_raid
        end -- Phase 1: silence
        local aiName = aiHouse:GetName()
        local bx, by, bn = 0, 0, 0
        for _, b in ipairs(buildings) do
            if b.oh == aiHouse and b.hasPos then
                bx, by, bn = bx + b.x, by + b.y, bn + 1
            end
        end
        if bn == 0 then goto next_raid end
        bx, by = bx / bn, by / bn
        local quietSince = frame - (lastThreatFrame[aiHouse] or 0)
        local st = raidState[aiHouse]
        -- Live members, re-resolved from this tick's snapshot.
        local live = {}
        if st and st.members then
            for _, mid in ipairs(st.members) do
                local m = findSnap(units, mid)
                if m and m.oh == aiHouse and m.hasPos then
                    live[#live + 1] = m
                end
            end
        end
        local function raidCentroid(members)
            local cx, cy = 0, 0
            for _, m in ipairs(members) do cx, cy = cx + m.x, cy + m.y end
            return cx / #members, cy / #members
        end
        if #live > 0 then
            -- M2-C5 Director rule #1: home under HIGH threat recalls the
            -- offense — disband to the defense pool (no orders issued;
            -- members re-enter pools by mission state). Re-form needs a
            -- quiet home again. Severity read from this tick's defense
            -- pass (runs earlier), not recomputed here.
            if defSevLevel[aiHouse] == "HIGH" then
                dlog("RAID_STANDDOWN", frame, string.format(
                    "house=%s members=%d reason=HOME_HIGH",
                    tostring(aiName), #live))
                raidState[aiHouse] = nil
                raidCooldownUntil[aiHouse] = frame + P.raidEvery
                if raidGroups[aiHouse] then
                    raidGroups[aiHouse]:reset()
                    raidGroups[aiHouse] = nil
                end
                goto next_raid
            end
            -- M2-C5 execution layer: sync the ForceGroup (Observe via its
            -- tracker) and let Tactical decide FIGHT vs FLIGHT
            -- (continue/changetarget/find_target vs retreat/disengage).
            -- SmartAI keeps Act: target selection (grudge), claims, logs.
            local grp = raidGroups[aiHouse]
            if not grp then
                grp = ForceGroup.newGroup({
                    id = "raid", radius = 18, pulseEvery = P.scan,
                    getHouse = houseGetter(aiHouse),
                })
                for _, m in ipairs(live) do grp:add_member_by_id(m.id) end
                raidGroups[aiHouse] = grp
            else
                for _, m in ipairs(live) do
                    if not grp:has(m.id) then grp:add_member_by_id(m.id) end
                end
                for _, gid in ipairs(grp:ids()) do
                    if not findSnap(units, gid) then grp:remove_member(gid) end
                end
            end
            grp.tracker:update(frame)
            local snap = Tactical.buildSnapshot(grp.tracker, aiHouse, { radius = 18 })
            -- SmartAI owns target selection (grudge): hand the raid's
            -- current target to the evaluator. Without this the group
            -- sees only its 18-cell radius and sticks at find_target
            -- whenever SmartAI aims beyond it (proven live-shape 28-cell
            -- refinery run). Shape mirrors Tactical's own target records.
            local raidTgt = st.targetId and findSnap(units, st.targetId)
                or findSnap(buildings, st.targetId)
            if raidTgt then
                snap.target = {
                    id = raidTgt.id, kind = raidTgt.k, typeName = raidTgt.t,
                    ownerName = raidTgt.on, hp = raidTgt.hp or 0,
                    maxHp = raidTgt.maxhp or 0, x = raidTgt.x, y = raidTgt.y,
                    dist = 0,
                }
            end
            local res = grp.tactical:reassess(snap, frame)
            if res.changed then
                local met = res.metrics or {}
                dlog("GROUP", frame, string.format(
                    "house=%s group=raid decision=%s reason=%s ratio=%s",
                    tostring(aiName), tostring(res.decision),
                    tostring(res.reason),
                    met.ratio and string.format("%.2f", met.ratio) or "nil"))
            end
            -- Drive the existing group: pick/refresh target, or retreat.
            local rcx, rcy = raidCentroid(live)
            local hostilesNear = 0
            for _, u in ipairs(units) do
                if u.oh and u.oh ~= aiHouse and not allied(aiHouse, u.oh)
                    and not util.is_neutral_house(u.oh)
                    and not util.CIVIL_TYPES[u.t]
                    and DEFENSE_KINDS[u.k] and u.hasPos
                    and dist2(u.x, u.y, rcx, rcy) <= 144 then -- 12 cells
                    hostilesNear = hostilesNear + 1
                end
            end
            -- Fight/flight comes from the Tactical evaluator (C5); the old
            -- headcount rule (hostiles >= members x mult) is retired —
            -- hostilesNear stays as log context only.
            local wantRetreat = (res.decision == "retreat"
                or res.decision == "disengage")
            if wantRetreat then
                if not st.retreating
                    or frame - (st.orderedFrame or 0) >= P.raidEvery then
                    local moved = 0
                    for _, m in ipairs(live) do
                        if orderMove(m, math.floor(bx + 0.5), math.floor(by + 0.5)) then
                            moved = moved + 1
                        end
                    end
                    st.retreating = true
                    st.orderedFrame = frame
                    if moved > 0 then
                        dlog("RAID_RETREAT", frame, string.format(
                            "house=%s members=%d hostiles=%d dest=%d,%d",
                            tostring(aiName), #live, hostilesNear,
                            math.floor(bx + 0.5), math.floor(by + 0.5)))
                        local alert = string.format(
                            "[AI Commander - %s] Raid outmatched (%d vs %d): falling back!",
                            tostring(aiName), #live, hostilesNear)
                        Engine.PrintMessage(alert)
                        print("[LuaAPI] " .. alert)
                    end
                end
            else
                -- Best-value economy target within raidRange of home.
                -- M2-C1: value is belief-weighted — candidates owned by a
                -- grudged house score base × grudgeMult. bestNoMem tracks
                -- the winner WITHOUT memory so a flip is provable in-log.
                local best, bestVal, bestD2 = nil, 0, nil
                local bestNoMem, bestNoMemVal, bestNoMemD2 = nil, 0, nil
                local pool = {}
                for _, u in ipairs(units) do pool[#pool + 1] = u end
                for _, b in ipairs(buildings) do pool[#pool + 1] = b end
                for _, cand in ipairs(pool) do
                    if cand.oh and cand.oh ~= aiHouse
                        and not allied(aiHouse, cand.oh)
                        and not util.is_neutral_house(cand.oh)
                        and not util.CIVIL_TYPES[cand.t] and cand.hasPos then
                        local baseV = raidTargetValue(cand)
                        local mult, _ = grudgeMult(aiHouse, cand.oh)
                        local v = baseV * mult
                        if v > 0 and dist2(cand.x, cand.y, bx, by)
                            <= P.raidRange * P.raidRange then
                            local d2 = dist2(cand.x, cand.y, rcx, rcy)
                            if v > bestVal or (v == bestVal
                                and (not bestD2 or d2 < bestD2
                                    or (d2 == bestD2 and cand.id < best.id))) then
                                best, bestVal, bestD2 = cand, v, d2
                            end
                            if baseV > bestNoMemVal or (baseV == bestNoMemVal
                                and (not bestNoMemD2 or d2 < bestNoMemD2
                                    or (d2 == bestNoMemD2 and bestNoMem
                                        and cand.id < bestNoMem.id))) then
                                bestNoMem, bestNoMemVal, bestNoMemD2 =
                                    cand, baseV, d2
                            end
                        end
                    end
                end
                if best then
                    if not st.retreating then
                        -- Holding same target inside refresh window: no re-issue.
                        if st.targetId == best.id
                            and frame - (st.orderedFrame or 0) < P.raidEvery then
                            -- hold: silence
                        else
                            local issued = 0
                            for _, m in ipairs(live) do
                                -- M2-C2: a member claimed earlier this tick
                                -- obeys the higher-priority order instead
                                -- (prevents cross-tick tug-of-war).
                                if not tryClaim(m.id, "RAID") then goto next_raider end
                                if orderAttack(m, best.u) then
                                    issued = issued + 1
                                    local actual = readbackTargetId(m.u)
                                    if actual ~= nil and actual ~= best.id then
                                        dlog("READBACK_MISMATCH", frame, string.format(
                                            "unit=%s expected_target=%s actual_target=%s",
                                            tostring(m.id), tostring(best.id),
                                            tostring(actual)))
                                    end
                                end
                                ::next_raider::
                            end
                            st.targetId = best.id
                            st.orderedFrame = frame
                            if issued > 0 then
                                dlog("RAID_TARGET", frame, string.format(
                                    "house=%s members=%d target=%s type=%s value=%.1f",
                                    tostring(aiName), issued, tostring(best.id),
                                    tostring(best.t), bestVal))
                                -- M2-C1 causal proof: memory changed the winner.
                                if bestNoMem and bestNoMem.id ~= best.id then
                                    local _, gn = grudgeMult(aiHouse, best.oh)
                                    dlog("BELIEF_EFFECT", frame, string.format(
                                        "house=%s no_mem=%s(%s) with_mem=%s(%s) grudge=%d",
                                        tostring(aiName), tostring(bestNoMem.id),
                                        tostring(bestNoMem.t), tostring(best.id),
                                        tostring(best.t), gn))
                                end
                                local alert = string.format(
                                    "[AI Commander - %s] Raid: %d hunter(s) on %s#%s!",
                                    tostring(aiName), issued,
                                    tostring(best.t), tostring(best.id))
                                Engine.PrintMessage(alert)
                                print("[LuaAPI] " .. alert)
                            end
                        end
                    else
                        -- Was retreating, contact cleared: resume hunting.
                        -- Reset the order window: the retreat order must not
                        -- double as attack cooldown (else the group sits out
                        -- a full raidEvery after every fallback).
                        st.retreating = nil
                        st.orderedFrame = 0
                    end
                else
                    -- No economy in range: disband to the idle pool (no wander),
                    -- with a re-form cooldown so FORM/STANDDOWN cannot churn
                    -- every scan while the enemy economy stays out of reach.
                    dlog("RAID_STANDDOWN", frame, string.format(
                        "house=%s members=%d reason=NO_ECONOMY_IN_RANGE",
                        tostring(aiName), #live))
                    raidState[aiHouse] = nil
                    raidCooldownUntil[aiHouse] = frame + P.raidEvery
                    if raidGroups[aiHouse] then
                        raidGroups[aiHouse]:reset()
                        raidGroups[aiHouse] = nil
                    end
                end
            end
        elseif quietSince >= P.raidQuiet
            and frame >= (raidCooldownUntil[aiHouse] or 0) then
            -- Form: idle combat surplus first (ID-ordered, deterministic);
            -- vanilla AI rarely leaves armor idle, so marching-but-not-
            -- fighting units (attacking==false, same strictness as march
            -- recall; actively fighting units are NEVER yanked) fill the
            -- group as second choice. First N overall, idle preferred.
            local pool, march = {}, {}
            for _, u in ipairs(units) do
                if u.oh == aiHouse and u.k == "unit" and u.hasPos
                    and not HARVESTER_TYPES[u.t] and not MCV_TYPES[u.t]
                    and not ARTILLERY_TYPES[u.t]
                    and not isOfficerAssigned(u.id)
                    and not claimTick[u.id] then -- M2-C2: claimed earlier
                    if u.idle then pool[#pool + 1] = u
                    elseif u.attacking == false then march[#march + 1] = u end
                end
            end
            table.sort(pool, function(a, b) return a.id < b.id end)
            table.sort(march, function(a, b) return a.id < b.id end)
            local idleN = #pool
            for _, m in ipairs(march) do
                if #pool >= (P.raidN or 4) then break end
                pool[#pool + 1] = m
            end
            if #pool >= (P.raidMin or 3) then
                local members = {}
                for i = 1, math.min(P.raidN or 4, #pool) do
                    members[#members + 1] = pool[i].id
                    claimTick[pool[i].id] = "RAID" -- M2-C2: register picks
                end
                raidState[aiHouse] = {
                    members = members, targetId = nil,
                    orderedFrame = 0, formedFrame = frame,
                }
                dlog("RAID_FORM", frame, string.format(
                    "house=%s members=%s quiet=%d idle=%d march=%d",
                    tostring(aiName), table.concat(members, ","),
                    quietSince, idleN, #members - idleN))
                local alert = string.format(
                    "[AI Commander - %s] Hunter group out (%d): seeking enemy economy!",
                    tostring(aiName), #members)
                Engine.PrintMessage(alert)
                print("[LuaAPI] " .. alert)
            end
            if #pool < (P.raidMin or 3)
                and frame % SMARTAI_CENSUS_EVERY < P.scan then
                dlog("RAID_POOL_SHORT", frame, string.format(
                    "house=%s pool=%d(idle=%d) need=%d quiet=%d",
                    tostring(aiName), #pool, idleN,
                    (P.raidMin or 3), quietSince))
            end
        else
            -- Home not quiet and no live group: throttled wait note so the
            -- next log shows which gate blocks (quiet vs pool vs range).
            if frame % SMARTAI_CENSUS_EVERY < P.scan then
                local poolN, econN = 0, 0
                for _, u in ipairs(units) do
                    if u.oh == aiHouse and u.k == "unit" and u.hasPos
                        and not HARVESTER_TYPES[u.t] and not MCV_TYPES[u.t]
                        and not ARTILLERY_TYPES[u.t]
                        and not isOfficerAssigned(u.id)
                        and (u.idle or u.attacking == false) then
                        poolN = poolN + 1
                    end
                end
                for _, b in ipairs(buildings) do
                    if b.oh and b.oh ~= aiHouse
                        and raidTargetValue(b) > 0 and b.hasPos
                        and dist2(b.x, b.y, bx, by)
                            <= P.raidRange * P.raidRange then
                        econN = econN + 1
                    end
                end
                dlog("RAID_STATUS", frame, string.format(
                    "house=%s quiet=%d/%d pool=%d econ_in_range=%d group=%s",
                    tostring(aiName), quietSince, P.raidQuiet,
                    poolN, econN, st and "yes" or "no"))
            end
            if st then raidState[aiHouse] = nil end
        end
        ::next_raid::
    end
    end -- P.raid

    -- M2-C2 ARBITER summary: one line per tick that had contested picks —
    -- the visible proof that conflicts are arbitrated, not double-ordered.
    do
        local skipTotal, parts = 0, {}
        for site, c in pairs(arbSkips) do
            skipTotal = skipTotal + c
            parts[#parts + 1] = site .. "=" .. tostring(c)
        end
        if skipTotal > 0 then
            table.sort(parts)
            dlog("ARBITER", frame, "skips " .. table.concat(parts, " "))
        end
    end

    -- [SMARTAI][CENSUS] (diagnostic only): AI-house roster with id /
    -- owner / type / kind / pos, throttled, capped — answers "who exists
    -- under which house" without touching the target_reselect [M14.1]
    -- player-side census. Built from this tick's snapshot: zero extra
    -- native calls.
    if frame - lastCensusFrame >= SMARTAI_CENSUS_EVERY then
        lastCensusFrame = frame
        local parts, total = {}, 0
        for _, aiHouse in ipairs(aiHouses) do
            local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
            for _, e in ipairs(units) do
                if e.oh == aiHouse then
                    total = total + 1
                    if #parts < 40 and e.hasPos then
                        parts[#parts + 1] = string.format("%d:%s:%s:%s@%d,%d",
                            e.id, tostring(e.on), tostring(e.t),
                            tostring(e.k), e.x, e.y)
                    end
                end
            end
        end
        dlog("CENSUS", frame, string.format("ai_units=%d :: %s%s", total,
            table.concat(parts, " "), total > #parts and " (+" .. (total - #parts) .. " more)" or ""))
        -- [SMARTAI][BASE] (diagnostic only, same gate): per-AI-house base
        -- state — buildings with HP, unit count, power, credits. Read-only;
        -- answers "what does the base still have" for defeat analysis.
        -- No defeat flags exist in LuaAPI (see SURRENDER_CONDITIONS_RESEARCH).
        for _, aiHouse in ipairs(aiHouses) do
            local P = paramsFor(aiHouse) -- per-house preset (Beta M1)
            local bp, shown, bn = {}, 0, 0
            for _, b in ipairs(buildings) do
                if b.oh == aiHouse then
                    bn = bn + 1
                    if shown < 30 then
                        shown = shown + 1
                        local hp = (b.hp ~= nil and b.maxhp ~= nil and b.maxhp > 0)
                            and string.format("%d/%d", b.hp, b.maxhp) or "?"
                        bp[#bp + 1] = string.format("%d:%s:%s@%d,%d",
                            b.id, tostring(b.t), hp,
                            b.hasPos and b.x or -1, b.hasPos and b.y or -1)
                    end
                end
            end
            local un = 0
            for _, e in ipairs(units) do
                if e.oh == aiHouse then un = un + 1 end
            end
            local okPo, po = pcall(aiHouse.GetPowerOutput, aiHouse)
            local okPd, pd = pcall(aiHouse.GetPowerDrain, aiHouse)
            local okCr, cr = pcall(aiHouse.GetCredits, aiHouse)
            dlog("BASE", frame, string.format(
                "house=%s blds=%d units=%d power=%s/%s credits=%s :: %s%s",
                tostring(aiHouse:GetName()), bn, un,
                (okPo and type(po) == "number") and tostring(math.floor(po)) or "?",
                (okPd and type(pd) == "number") and tostring(math.floor(pd)) or "?",
                (okCr and type(cr) == "number") and tostring(math.floor(cr)) or "?",
                table.concat(bp, " "), bn > shown and " (+" .. (bn - shown) .. " more)" or ""))
        end
    end

    -- Officer state hygiene: drop principals that vanished; release
    -- stand-down escorts (Commander breach priority) with one log line.
    for id, st in pairs(escortState) do
        if not v3ids[id] then
            escortState[id] = nil
        elseif st.standDown then
            if st.guards and #st.guards > 0 then
                local alert = string.format(
                    "[AI Commander - %s] V3#%s escort released: base breach priority.",
                    tostring(st.houseName or "?"), tostring(id))
                Engine.PrintMessage(alert)
                print("[LuaAPI] " .. alert)
            end
            escortState[id] = nil
        end
    end
    for id in pairs(garrisonState) do
        if not defids[id] then garrisonState[id] = nil end
    end
    for id in pairs(recallState) do
        if not seenNow[id] then recallState[id] = nil end
    end
    for id, ds in pairs(defenseState) do
        local tgt = findSnap(units, ds.targetId)
        local defAlive = seenNow[id]
        if not defAlive or not tgt then
            -- M2-C4 ADAPT: Observe result. An assignment that ends here is a
            -- resolved episode. Only an UNRESOLVED one counts: a live
            -- defender still fighting is not yet a win and not yet a loss.
            -- Defender gone + intruder alive = loss (we traded and lost);
            -- defender alive + intruder gone = win; both gone = draw, neither.
            if ds.house and not surrendered[ds.house] then
                local e2 = dEff[ds.house]
                if not e2 then
                    e2 = { w = 0, l = 0, streak = 0, seenWin = {},
                           seenLoss = {}, seenDraw = {} }
                    dEff[ds.house] = e2
                end
                -- ONE credit per engagement, in BOTH directions. Two defenders
                -- dying on one intruder is ONE failed contact, not two: letting
                -- it count twice would reach streak=2 inside a single fight,
                -- which contradicts the stated rationale ("a single loss can
                -- be a cornered unit; two in a row is a signal"). Unit ids are
                -- not recycled in RA2, so targetId identifies the engagement
                -- for the whole match.
                --
                -- Per HOUSE: the dedup memory lives on the house ledger
                -- (dEff[house].seen*), so one intruder fought by two houses is
                -- an independent contact for each of them, while one intruder
                -- cannot be credited twice for the same house.
                --
                -- The memory MUST be a set, not a single "last target" slot.
                -- This loop resolves every defence record in ONE pass, so
                -- records for two different intruders interleave freely in
                -- pairs() order. With a single slot, the sequence
                --     [d1 -> t1]  [d2 -> t2]  [d3 -> t1]
                -- credits t1 twice: resolving d2 overwrites the slot with t2,
                -- so d3 no longer recognises t1 as already settled. That was
                -- observed live in the harness as w=2 then w=4 for the same
                -- intruder inside a single frame.
                --
                -- KNOWN LIMIT, deliberate: a defender that dies AFTER the
                -- threat is already dead is not a defensive failure, and its
                -- record is gone by then, so it is not counted at all. Only
                -- "defender died while the intruder lived" counts against us.
                --
                -- `emit` marks the record that FIRST resolves this target.
                -- Later defenders still attached to the same intruder are
                -- duplicates of one contact: no ledger credit and no event.
                local outcome, emit
                if not defAlive and tgt then
                    outcome = "LOSE"
                    local seen = e2.seenLoss
                    if not seen then seen = {}; e2.seenLoss = seen end
                    if not seen[ds.targetId] then
                        seen[ds.targetId] = true
                        e2.l = e2.l + 1
                        e2.streak = e2.streak + 1
                        dEffStats.l = dEffStats.l + 1
                        emit = true
                    end
                elseif defAlive and not tgt then
                    outcome = "WIN"
                    local seen = e2.seenWin
                    if not seen then seen = {}; e2.seenWin = seen end
                    if not seen[ds.targetId] then
                        seen[ds.targetId] = true
                        e2.w = e2.w + 1
                        e2.streak = 0 -- adaptation stands down on a win
                        dEffStats.w = dEffStats.w + 1
                        emit = true
                    end
                else
                    outcome = "DRAW" -- both gone: no w/l credit, but visible
                    local seen = e2.seenDraw
                    if not seen then seen = {}; e2.seenDraw = seen end
                    emit = not seen[ds.targetId]
                    if emit then
                        seen[ds.targetId] = true
                        dEffStats.draw = dEffStats.draw + 1
                    end
                end
                if emit then
                    -- EVENT, not a state: unthrottled, so the Act -> Observe
                    -- chain is readable line by line. The streak travels with
                    -- the event, so the streak progression is visible without
                    -- waiting for the next census. Exactly one event per
                    -- resolved contact.
                    dlog("CONTACT", frame, string.format(
                        "house=%s outcome=%s defender=%s intruder=%s "
                            .. "w=%d l=%d streak=%d armed=%s",
                        tostring(ds.house:GetName()), outcome, tostring(id),
                        tostring(ds.targetId), e2.w, e2.l, e2.streak,
                        tostring(e2.streak >= ADAPT_STREAK_TRIGGER)))
                end
            end
            defenseState[id] = nil -- defender or target gone: release
        end
    end
    for id in pairs(focusState) do
        if not seenNow[id] then focusState[id] = nil end -- bomber gone
    end
    for h, rst in pairs(raidState) do
        if rst.members then
            local kept = {}
            for _, mid in ipairs(rst.members) do
                if seenNow[mid] then kept[#kept + 1] = mid end
            end
            if #kept == 0 then
                raidState[h] = nil -- group wiped: release
            else
                rst.members = kept
                if rst.targetId and not findSnap(units, rst.targetId)
                    and not findSnap(buildings, rst.targetId) then
                    rst.targetId = nil -- target gone: re-pick next tick
                end
            end
        else
            raidState[h] = nil
        end
    end
    local houseSet = {}
    for _, h in ipairs(aiHouses) do houseSet[h] = true end
    for h in pairs(cmdState) do
        if not houseSet[h] then cmdState[h] = nil end
    end
    for h in pairs(rallyState) do
        if not houseSet[h] then rallyState[h] = nil end
    end
    for _, rs in pairs(rallyState) do
        if rs.ids then
            for id in pairs(rs.ids) do
                if not seenNow[id] then rs.ids[id] = nil end
            end
        end
    end
    for h in pairs(intruderSeen) do
        if not houseSet[h] then intruderSeen[h] = nil end
    end
    for h in pairs(raidState) do
        if not houseSet[h] then raidState[h] = nil end
    end
    for h in pairs(lastThreatFrame) do
        if not houseSet[h] then lastThreatFrame[h] = nil end
    end
    for h in pairs(raidCooldownUntil) do
        if not houseSet[h] then raidCooldownUntil[h] = nil end
    end
    for h in pairs(raidGroups) do
        if not houseSet[h] or not raidState[h] then
            if raidGroups[h] then raidGroups[h]:reset() end
            raidGroups[h] = nil
        end
    end
end

return SmartAI
