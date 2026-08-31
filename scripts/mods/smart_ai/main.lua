```lua
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
    if not DEBUG then
        return
    end

    print("[LuaAPI] [AI-PROBE] " .. message)
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

    return ok and result
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

    return ok and result
end

------------------------------------------------------------
-- OBJECTIVE DETECTION
------------------------------------------------------------

local function IsOilDerrick(object)
    if not SafeAlive(object) then
        return false
    end

    return SafeType(object) == "CAOILD"
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
-- FIND BEST CANDIDATE
------------------------------------------------------------

local function FindBestCandidate(objective)
    if not SafeAlive(objective) then
        return nil, 0
    end

    local candidates = World.GetUnitsInRadius(
        objective,
        CANDIDATE_RADIUS
    )

    if not candidates then
        return nil, 0
    end

    local bestIdle = nil
    local bestIdleDistance = math.huge

    local bestAny = nil
    local bestAnyDistance = math.huge

    local candidateCount = 0

    for _, unit in ipairs(candidates) do
        if SafeAlive(unit) then
            local kind = unit:GetKind()

            if kind == "unit" then
                candidateCount = candidateCount + 1

                local distance = nil

                local ok, result = pcall(function()
                    return unit:GetDistanceTo(objective)
                end)

                if ok then
                    distance = result
                end

                if distance then
                    if distance < bestAnyDistance then
                        bestAny = {
                            unit = unit,
                            distance = distance
                        }

                        bestAnyDistance = distance
                    end

                    if SafeIdle(unit) and distance < bestIdleDistance then
                        bestIdle = {
                            unit = unit,
                            distance = distance
                        }

                        bestIdleDistance = distance
                    end
                end
            end
        end
    end

    -- Prefer the nearest idle unit.
    if bestIdle then
        return bestIdle, candidateCount
    end

    -- Otherwise use the nearest valid unit.
    return bestAny, candidateCount
end

------------------------------------------------------------
-- DEBUG CANDIDATES
------------------------------------------------------------

local function InspectCandidates(objective)
    if not DEBUG then
        return
    end

    local candidates = World.GetUnitsInRadius(
        objective,
        CANDIDATE_RADIUS
    )

    if not candidates then
        return
    end

    local inspected = 0

    for _, unit in ipairs(candidates) do
        if inspected >= DEBUG_CANDIDATES then
            break
        end

        if SafeAlive(unit) and unit:GetKind() == "unit" then
            local ok, distance = pcall(function()
                return unit:GetDistanceTo(objective)
            end)

            if ok and distance then
                inspected = inspected + 1

                Log(string.format(
                    "  candidate[%d] id=%s type=%s owner=%s distance=%.2f idle=%s",
                    inspected,
                    tostring(SafeId(unit)),
                    SafeType(unit),
                    SafeName(SafeOwner(unit)),
                    distance,
                    tostring(SafeIdle(unit))
                ))
            end
        end
    end
end

------------------------------------------------------------
-- MOVE TEST
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

    local ok, result = pcall(function()
        return unit:MoveTo(
            position.x,
            position.y
        )
    end)

    if not ok then
        Log(
            "  MoveTo ERROR: " ..
            tostring(result)
        )

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

    --------------------------------------------------------
    -- Diagnostic candidate inspection.
    --------------------------------------------------------

    InspectCandidates(objective)

    --------------------------------------------------------
    -- Select best candidate.
    --------------------------------------------------------

    local selected, candidateCount =
        FindBestCandidate(objective)

    Log(string.format(
        "  valid candidates=%d",
        candidateCount
    ))

    if not selected then
        Log("  RESULT: no valid unit within radius")
        Log("========================================")
        return
    end

    local unit = selected.unit

    Log(string.format(
        "  selected id=%s type=%s distance=%.2f idle=%s",
        tostring(SafeId(unit)),
        SafeType(unit),
        selected.distance,
        tostring(SafeIdle(unit))
    ))

    --------------------------------------------------------
    -- Test MoveTo once.
    --------------------------------------------------------

    if commandedObjectives[id] then
        Log("  MoveTo already tested for this objective")
    else
        local success = TestMoveTo(
            unit,
            objective
        )

        -- Only mark as commanded if the native call succeeded.
        if success then
            commandedObjectives[id] = true
        end
    end

    Log("========================================")
end

------------------------------------------------------------
-- SCAN OBJECTIVES
------------------------------------------------------------

local function ScanObjectives(buildings)
    local processed = 0

    for _, building in ipairs(buildings) do
        if processed >= MAX_OBJECTIVES then
            break
        end

        if IsOilDerrick(building) then
            local id = SafeId(building)

            if id and not inspectedObjectives[id] then
                processed = processed + 1

                TestObjective(building)
            end
        end
    end
end

------------------------------------------------------------
-- BASIC API TEST
------------------------------------------------------------

local function InitialDiagnostic()
    Log("========================================")
    Log("LuaAPI AI Probe v2 started")
    Log("Spatial candidate queries enabled")
    Log("Global unit scan disabled")
    Log("Candidate sorting disabled")
    Log("Repeated MoveTo prevented")
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

    --------------------------------------------------------
    -- One-time initialization.
    --------------------------------------------------------

    if not sessionStarted then
        sessionStarted = true
        InitialDiagnostic()
    end

    --------------------------------------------------------
    -- Player.
    --------------------------------------------------------

    local human = House.GetPlayer()

    if not human then
        return
    end

    --------------------------------------------------------
    -- Objective scan.
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
```
