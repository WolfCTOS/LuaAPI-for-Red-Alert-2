-- Double Harvester per harvester building.
--
-- Grants extra harvesters to a house whenever a harvester building (Tiberium
-- refinery) appears on the map, for both base styles:
--   GAREFN  Allied Ore Refinery  ->  CMIN  Chrono Miner
--   NAREFN  Soviet Ore Refinery  ->  HARV  War Miner
--
-- Vanilla YR already hands out ONE free harvester per refinery through the
-- vanilla [BuildingType] FreeUnit key (GAREFN FreeUnit=CMIN, NAREFN
-- FreeUnit=HARV), so grant=1 below means "2 harvesters per refinery" in total.
-- Raise TUNING.TRIGGERS[*].grant to add more on top of the vanilla one.
--
-- MCV DEPLOY is a separate, currently DISABLED trigger (see GRANT_ON_MCV_DEPLOY):
-- an MCV deploy only puts a Construction Yard on the map and grants nothing in
-- vanilla, so there the pair has to be granted in full.
--
-- WHY A LUA MOD AND NOT rulesmd.ini (Ares / Phobos):
--   FreeUnit is a single UnitTypeClass*, not a list, so INI can only ever give
--   one. On top of that the key is consumed inside BuildingClass::Place (the
--   FreeUnit block at 0x446AAF, skipped by Ares' BuildingClass_Place_SkipFreeUnits
--   and re-targeted by Phobos' BuildingClass_Place_FreeUnit_NearByLocation),
--   which is why refineries get theirs and an MCV deploy does not: the deploy
--   routine creates the building and calls Put() instead of Place() (largest
--   vtable slot used there is 0x3C8, Place sits past vt_entry_4D8). Neither Ares
--   nor Phobos has an INI key for spawning units on deploy, and
--   Spawns/SpawnsNumber is hard-bound to AircraftTypeClass* in
--   SpawnManagerClass, so it cannot be reused for ground units.
--
-- DETECTION: "building id not seen last scan" == "the house just got one".
-- Buildings that already exist when the mod first scans the map are primed and
-- never grant, so campaign starts and save loads stay vanilla.
--
-- Determinism: no math.random / os.time / os.clock in gameplay paths; the mod
-- only reacts to state transitions, and the loader already gates mod.Update()
-- to one call per logical frame.

local M = {}

M.TUNING = {
    -- Building type id -> { harvester = unit type id, grant = extra units }
    TRIGGERS = {
        GAREFN = { harvester = "CMIN", grant = 1 },
        NAREFN = { harvester = "HARV", grant = 1 },
    },

    -- Optional second trigger set: the Construction Yard an MCV deploys into.
    GRANT_ON_MCV_DEPLOY = false,
    DEPLOY_TRIGGERS = {
        GACNST = { harvester = "CMIN", grant = 2 },
        NACNST = { harvester = "HARV", grant = 2 },
    },

    -- Cell offsets tried, in order, around the building for each harvester.
    -- Both refineries are 2x2, so +/-2 lands clear of the footprint;
    -- SpawnUnit also searches a small radius itself.
    OFFSETS = {
        { 2,  0 }, { 3,  0 }, { 0,  2 }, { 1,  2 },
        { 2,  2 }, { 3,  2 }, { 0, -2 }, { 1, -2 },
        { 2, -2 }, { 3, -2 }, { -2, 0 }, { -3, 0 },
    },

    -- true = only grant to human houses (AI keeps the vanilla refinery gift).
    SKIP_AI_HOUSES = false,
    LOG_PREFIX = "[2HARV]",
}

local S = {
    seen = {},
    lastFrame = -1,
    primed = false,
    announced = {},
}

local PREFIX = M.TUNING.LOG_PREFIX

local function log(msg)
    print(PREFIX .. " " .. msg)
end

local function say(msg)
    if Engine and Engine.PrintMessage then
        pcall(Engine.PrintMessage, PREFIX .. " " .. msg)
    end
end

local function resetState()
    S.seen = {}
    S.lastFrame = -1
    S.primed = false
    S.announced = {}
end

-- Active trigger table, merged from the two sets depending on the flag.
local function activeTriggers()
    if not M.TUNING.GRANT_ON_MCV_DEPLOY then
        return M.TUNING.TRIGGERS
    end
    local merged = {}
    for id, cfg in pairs(M.TUNING.TRIGGERS) do
        merged[id] = cfg
    end
    for id, cfg in pairs(M.TUNING.DEPLOY_TRIGGERS) do
        merged[id] = cfg
    end
    return merged
end

local function spawnOne(house, typeId, x, y, force)
    local ok, n = pcall(house.SpawnUnit, house, typeId, 1, x, y, 0, force, "")
    if not ok then
        log("SpawnUnit threw for " .. typeId .. ": " .. tostring(n))
        return 0
    end
    return n or 0
end

local function grantHarvesters(b, typeName, cfg)
    local house = b:GetOwner()
    if not house then
        log("no owner for " .. typeName .. " id=" .. tostring(b:GetId()))
        return
    end

    if M.TUNING.SKIP_AI_HOUSES then
        local okH, human = pcall(house.IsHuman, house)
        if okH and human == false then
            return
        end
    end

    local pos = b:GetPosition()
    if not pos or pos.x == nil or pos.y == nil then
        log("no position for " .. typeName)
        return
    end

    local baseX = math.floor(pos.x)
    local baseY = math.floor(pos.y)
    local offsets = M.TUNING.OFFSETS
    local wanted = cfg.grant
    local created = 0

    for i = 1, wanted do
        local off = offsets[((i - 1) % #offsets) + 1]
        local tx = baseX + off[1]
        local ty = baseY + off[2]
        -- Prefer a clean passable cell; fall back to a forced spawn so a
        -- temporarily crowded base never silently eats the gift.
        local n = spawnOne(house, cfg.harvester, tx, ty, false)
        if n == 0 then
            n = spawnOne(house, cfg.harvester, tx, ty, true)
        end
        created = created + n
    end

    local ownerName = "?"
    local okN, name = pcall(house.GetName, house)
    if okN and name then ownerName = name end

    local verdict = (created == wanted) and "PASS" or "PARTIAL"
    log(string.format("%s %s: wanted %d x %s, created %d (owner=%s, at %d,%d)",
        verdict, typeName, wanted, cfg.harvester, created, ownerName, baseX, baseY))
    if created > 0 then
        say(string.format("%s -> +%d %s", typeName, created, cfg.harvester))
    end
end

-- id -> { b = building, type = type id, cfg = trigger config }
local function scanTriggers()
    local found = {}
    if not (World and World.GetBuildings) then
        return found
    end

    local ok, buildings = pcall(World.GetBuildings)
    if not ok or not buildings then
        return found
    end

    local triggers = activeTriggers()

    for _, b in ipairs(buildings) do
        if b then
            local okA, alive = pcall(b.IsAlive, b)
            if okA and alive then
                local okT, typeName = pcall(b.GetTypeName, b)
                if okT and typeName then
                    local cfg = triggers[typeName]
                    if cfg then
                        local okI, id = pcall(b.GetId, b)
                        if okI and id then
                            found[id] = { b = b, type = typeName, cfg = cfg }
                        end
                    elseif typeName == "YAREFN" and not S.announced.YAREFN then
                        S.announced.YAREFN = true
                        log("YAREFN (Yuri refinery) seen: out of scope, nothing granted. " ..
                            "Yuri refineries carry no vanilla FreeUnit, so add " ..
                            "YAREFN = { harvester = \"<harvester>\", grant = 2 } to TUNING.TRIGGERS to enable.")
                    end
                end
            end
        end
    end

    return found
end

function M.Update(frame)
    frame = frame or 0

    -- Match restart inside the same process: frame counter goes backwards.
    if frame < S.lastFrame then
        log("frame counter reset -> state cleared, re-priming")
        resetState()
    end
    S.lastFrame = frame

    local found = scanTriggers()

    -- Drop ids that left the map (destroyed / sold) so a rebuild grants again.
    for id in pairs(S.seen) do
        if not found[id] then
            S.seen[id] = nil
        end
    end

    for id, entry in pairs(found) do
        if not S.seen[id] then
            S.seen[id] = true
            if S.primed then
                grantHarvesters(entry.b, entry.type, entry.cfg)
            end
        end
    end

    -- First successful scan of a match only records what already exists.
    S.primed = true
end

return M
