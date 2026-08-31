local AIProbe = {}

------------------------------------------------------------
-- CONFIG
------------------------------------------------------------

-- Decision tick. 30 frames ~= 0.5 s at 60 FPS.
local TICK = 30

-- Re-scan the building array less frequently than the AI tick.
local OBJECTIVE_SCAN_TICK = 300

-- true = detailed diagnostics in stdout/log.
local DEBUG = true

-- Maximum number of objectives processed during one session.
local MAX_OBJECTIVES = 20

-- Keep spatial queries small and bounded.
local CANDIDATE_RADIUS = 15

-- Number of candidates shown in diagnostics.
local DEBUG_CANDIDATES = 5

------------------------------------------------------------
-- STATE
------------------------------------------------------------

local lastTick = 0
local lastObjectiveScan = -OBJECTIVE_SCAN_TICK

-- [objectId] = true
local inspectedObjectives = {}

-- [objectId] = true
local commandedObjectives = {}

-- Prevents repeated startup diagnostics.
local sessionStarted = false

------------------------------------------------------------
-- LOGGING
------------------------------------------------------------

local function Log(message)
    if DEBUG then
        print("[LuaAPI] [AI-PROBE] " .. message)
    end
end

------------------------------------------------------------
-- SAFE ACCESS
------------------------------------------------------------

local function SafeAlive(object)
    if not object then
        return false
    end

    local ok, result = pcall(function()
        return object:IsAlive()
    end)

    return ok and result == true
end

local function SafeId(object)
    if not object then
        return nil
    end

    local ok, result = pcall(function()
        return object:GetId()
    end)

    if ok then
        return result
    end

    return nil
end

local function SafeType(object)
    if not object then
        return "<nil>"
    end

    local ok, result = pcall(function()
        return object:GetTypeName()
    end)

    if ok and result then
        return result
    end

    return "<unknown>"
end

local function SafeName(object)
    if not object then
        return "<nil>"
    end

    local ok, result = pcall(function()
        return object:GetName()
    end)

    if ok and result then
        return result
    end

    return "<unknown>"
end

local function SafeOwner(object)
    if not object then
        return nil
    end

    local ok, result = pcall(function()
        return object:GetOwner()
    end)

    if ok then
        return result
    end

    return nil
end

local function SafePosition(object)
    if not object then
        return nil
    end

    local ok, result = pcall(function()
        return object:GetPosition()
    end)

    if ok and result then
        return result
    end

    return nil
end

local function SafeKind(object)
    if not object then
        return nil
    end

    local ok, result = pcall(function()
        return object:GetKind()
    end)

    if ok then
        return result
    end

    return nil
end

local function SafeIdle(unit)
    if not unit then
        return false
    end

    local ok, result = pcall(function()
        return unit:IsIdle()
    end)

    return ok and result == true
end

local function SafeMoveTo(unit, x, y)
    if not SafeAlive(unit) then
        return false, "unit is no longer alive"
    end

    local ok, result = pcall(function()
        return unit:MoveTo(x, y)
    end)

    if not ok then
        return false, tostring(result)
    end

    return result == true, result == true and nil or "MoveTo rejected the command"
end

------------------------------------------------------------
-- OBJECTIVE DETECTION
------------------------------------------------------------

local function IsOilDerrick(object)
    return SafeAlive(object) and SafeType(object) == "CAOILD"
end

------------------------------------------------------------
-- OBJECTIVE DESCRIPTION
------------------------------------------------------------

local function DescribeObjective(objective)
    local id = SafeId(objective)
    local typeName = SafeType(objective)
    local owner = SafeOwner(objective)
    local ownerName = SafeName(owner)
    local position = SafePosition(objective)

    local x = -1
    local y = -1

    if position then
        x = position.x or -1
        y = position.y or -1
    end

    return string.format(
        "id=%s type=%s owner=%s pos=(%d,%d)",
        tostring(id),
        typeName,
        ownerName,
        x,
        y
    )
end

------------------------------------------------------------
-- CANDIDATE SELECTION
------------------------------------------------------------

-- Find the best candidate without sorting the entire candidate list.
-- Priority:
--   1. idle unit
--   2. shortest distance
--
-- Returns:
--   selectedCandidate, candidateCount, debugCandidates
local function FindBestCandidate(objective)
    local position = SafePosition(objective)

    if not position then
        return nil, 0, {}
    end

    local nearby = World.GetUnitsInRadius(
        position.x,
        position.y,
        CANDIDATE_RADIUS
    )

    if not nearby then
        return nil, 0, {}
    end

    local bestIdle = nil
    local bestAny = nil
    local count = 0

    -- Only keep a tiny debug list. The AI itself never sorts all candidates.
    local debugCandidates = {}

    for _, unit in ipairs(nearby) do
        if SafeAlive(unit) and SafeKind(unit) == "unit" then
            local unitPosition = SafePosition(unit)

            if unitPosition then
                local dx = unitPosition.x - position.x
                local dy = unitPosition.y - position.y
                local distanceSq = dx * dx + dy * dy

                if distanceSq <= CANDIDATE_RADIUS * CANDIDATE_RADIUS then
                    count = count + 1

                    local candidate = {
                        unit = unit,
                        distanceSq = distanceSq
                    }

                    if not bestAny or distanceSq < bestAny.distanceSq then
                        bestAny = candidate
                    end

                    if SafeIdle(unit) and
                        (not bestIdle or distanceSq < bestIdle.distanceSq) then
                        bestIdle = candidate
                    end

                    if DEBUG then
                        debugCandidates[#debugCandidates + 1] = candidate
                    end
                end
            end
        end
    end

    if DEBUG and #debugCandidates > 1 then
        table.sort(debugCandidates, function(a, b)
            return a.distanceSq < b.distanceSq
        end)

        while #debugCandidates > DEBUG_CANDIDATES do
            debugCandidates[#debugCandidates] = nil
        end
    end

    return bestIdle or bestAny, count, debugCandidates
end

------------------------------------------------------------
-- DEBUG CANDIDATE REPORT
------------------------------------------------------------

local function InspectCandidates(candidates)
    for index, candidate in ipairs(candidates) do
        local unit = candidate.unit
        local id = SafeId(unit)
        local typeName = SafeType(unit)
        local owner = SafeOwner(unit)
        local ownerName = SafeName(owner)
        local idle = SafeIdle(unit)
        local distance = math.sqrt(candidate.distanceSq)

        Log(string.format(
            "  candidate[%d] id=%s type=%s owner=%s distance=%.2f idle=%s",
            index,
            tostring(id),
            typeName,
            ownerName,
            distance,
            tostring(idle)
        ))
    end
end

------------------------------------------------------------
-- COMMAND
------------------------------------------------------------

local function TestMoveTo(unit, objective)
    if not SafeAlive(unit) then
        Log("  COMMAND ABORTED: selected unit is dead")
        return false
    end

    if not SafeAlive(objective) then
        Log("  COMMAND ABORTED: objective is dead")
        return false
    end

    local position = SafePosition(objective)

    if not position then
        Log("  COMMAND FAILED: objective position unavailable")
        return false
    end

    local unitId = SafeId(unit)
    local unitType = SafeType(unit)

    Log(string.format(
        "  COMMAND: unit id=%s type=%s -> MoveTo(%d,%d)",
        tostring(unitId),
        unitType,
        position.x,
        position.y
    ))

    local ok, errorMessage = SafeMoveTo(
        unit,
        position.x,
        position.y
    )

    if not ok then
        Log("  MoveTo FAILED: " .. tostring(errorMessage))
        return false
    end

    Log("  MoveTo accepted")
    return true
end

------------------------------------------------------------
-- TEST ONE OBJECTIVE
------------------------------------------------------------

local function TestObjective(objective)
    if not SafeAlive(objective) then
        return
    end

    local id = SafeId(objective)

    if not id then
        Log("Objective has no valid ID; skipping")
        return
    end

    if inspectedObjectives[id] then
        return
    end

    inspectedObjectives[id] = true

    Log("========================================")
    Log("OBJECTIVE DETECTED")
    Log("  " .. DescribeObjective(objective))

    local selected, candidateCount, debugCandidates =
        FindBestCandidate(objective)

    Log(string.format("  candidates=%d", candidateCount))

    if DEBUG then
        InspectCandidates(debugCandidates)
    end

    if not selected then
        Log("  RESULT: no suitable units within radius")
        Log("========================================")
        return
    end

    if commandedObjectives[id] then
        Log("  MoveTo already tested for this objective")
        Log("========================================")
        return
    end

    commandedObjectives[id] = true

    local success = TestMoveTo(
        selected.unit,
        objective
    )

    if not success then
        -- Do not permanently consume an objective when the command failed.
        commandedObjectives[id] = nil
    end

    Log("========================================")
end

------------------------------------------------------------
-- SCAN OBJECTIVES
------------------------------------------------------------

local function ScanObjectives(buildings)
    local processed = 0

    for _, building in ipairs(buildings) do
        if IsOilDerrick(building) then
            local id = SafeId(building)

            if id and not inspectedObjectives[id] then
                if processed >= MAX_OBJECTIVES then
                    break
                end

                processed = processed + 1
                TestObjective(building)
            end
        end
    end

    return processed
end

------------------------------------------------------------
-- BASIC API TEST
------------------------------------------------------------

local function InitialDiagnostic()
    Log("========================================")
    Log("LuaAPI AI Probe v2 started")
    Log("Perception: World.GetUnitsInRadius")
    Log("Selection: single-pass nearest/idle candidate")
    Log("Action: native MoveTo")
    Log("Global unit scan: DISABLED")
    Log("Repeated MoveTo: PREVENTED")
    Log("========================================")
end

------------------------------------------------------------
-- MAIN UPDATE
------------------------------------------------------------

function AIProbe.Update(frame)
    if frame - lastTick < TICK then
        return
    end

    lastTick = frame

    if not sessionStarted then
        sessionStarted = true
        InitialDiagnostic()
    end

    local human = House.GetPlayer()

    if not human then
        return
    end

    --------------------------------------------------------
    -- Objective discovery is intentionally slower than the
    -- decision tick. Candidate search is spatial and local.
    --------------------------------------------------------

    if frame - lastObjectiveScan >= OBJECTIVE_SCAN_TICK then
        lastObjectiveScan = frame

        local buildings = World.GetBuildings()

        if not buildings then
            Log("World.GetBuildings() returned nil")
            return
        end

        ScanObjectives(buildings)
    end
end

------------------------------------------------------------
-- RESET
------------------------------------------------------------

function AIProbe.Reset()
    lastTick = 0
    lastObjectiveScan = -OBJECTIVE_SCAN_TICK

    inspectedObjectives = {}
    commandedObjectives = {}

    sessionStarted = false

    Log("AI Probe state reset")
end

return AIProbe
