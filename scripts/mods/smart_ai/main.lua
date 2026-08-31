local AIProbe = {}

------------------------------------------------------------
-- CONFIG
------------------------------------------------------------

local TICK = 30

-- true = выводить подробную диагностику в stdout/log
local DEBUG = true

-- Максимум объектов, которые probe тестирует за сессию
local MAX_OBJECTIVES = 20

-- Радиус поиска кандидатов вокруг objective
local CANDIDATE_RADIUS = 15

------------------------------------------------------------
-- STATE
------------------------------------------------------------

local lastTick = 0

-- [objectId] = true
local inspectedObjectives = {}

-- [objectId] = true
local commandedObjectives = {}

-- Prevents repeated global scans from producing identical logs
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

local function SafeDistance(a, b)
    if not a or not b then
        return nil
    end

    local ok, result = pcall(function()
        return a:GetDistanceTo(b)
    end)

    if ok then
        return result
    end

    return nil
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
-- FIND CANDIDATES
------------------------------------------------------------

local function FindCandidates(objective, units)
    local candidates = {}

    for _, unit in ipairs(units) do
        if SafeAlive(unit) then
            local kind = unit:GetKind()

            if kind == "unit" then
                local distance = SafeDistance(
                    unit,
                    objective
                )

                if distance and distance <= CANDIDATE_RADIUS then
                    table.insert(
                        candidates,
                        {
                            unit = unit,
                            distance = distance
                        }
                    )
                end
            end
        end
    end

    table.sort(
        candidates,
        function(a, b)
            return a.distance < b.distance
        end
    )

    return candidates
end

------------------------------------------------------------
-- INSPECT CANDIDATE
------------------------------------------------------------

local function InspectCandidate(candidate, index)
    local unit = candidate.unit

    local id = SafeId(unit)
    local typeName = SafeType(unit)

    local owner = SafeOwner(unit)
    local ownerName = SafeName(owner)

    local idle = SafeIdle(unit)

    Log(string.format(
        "  candidate[%d] id=%s type=%s owner=%s distance=%.2f idle=%s",
        index,
        tostring(id),
        typeName,
        ownerName,
        candidate.distance,
        tostring(idle)
    ))
end

------------------------------------------------------------
-- SELECT TEST UNIT
------------------------------------------------------------

local function SelectTestUnit(candidates)
    if #candidates == 0 then
        return nil
    end

    -- Prefer idle unit.
    for _, candidate in ipairs(candidates) do
        if SafeIdle(candidate.unit) then
            return candidate
        end
    end

    -- Otherwise closest unit.
    return candidates[1]
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

    Log(
        "  MoveTo accepted"
    )

    return true
end

------------------------------------------------------------
-- TEST ONE OBJECTIVE
------------------------------------------------------------

local function TestObjective(objective, units)
    if not SafeAlive(objective) then
        return
    end

    local id = SafeId(objective)

    if not id then
        Log(
            "Objective has no valid ID; skipping"
        )

        return
    end

    --------------------------------------------------------
    -- Never inspect same objective twice.
    --------------------------------------------------------

    if inspectedObjectives[id] then
        return
    end

    inspectedObjectives[id] = true

    Log(
        "========================================"
    )

    Log(
        "OBJECTIVE DETECTED"
    )

    Log(
        "  " .. DescribeObjective(objective)
    )

    --------------------------------------------------------
    -- Find units.
    --------------------------------------------------------

    local candidates =
        FindCandidates(
            objective,
            units
        )

    Log(string.format(
        "  candidates=%d",
        #candidates
    ))

    --------------------------------------------------------
    -- Show first five candidates.
    --------------------------------------------------------

    local inspectCount = math.min(
        #candidates,
        5
    )

    for i = 1, inspectCount do
        InspectCandidate(
            candidates[i],
            i
        )
    end

    --------------------------------------------------------
    -- No candidates.
    --------------------------------------------------------

    if #candidates == 0 then
        Log(
            "  RESULT: no units within radius"
        )

        Log(
            "========================================"
        )

        return
    end

    --------------------------------------------------------
    -- Select unit.
    --------------------------------------------------------

    local selected =
        SelectTestUnit(
            candidates
        )

    if not selected then
        Log(
            "  RESULT: unable to select unit"
        )

        Log(
            "========================================"
        )

        return
    end

    --------------------------------------------------------
    -- Test MoveTo once.
    --------------------------------------------------------

    if commandedObjectives[id] then
        Log(
            "  MoveTo already tested for this objective"
        )
    else
        commandedObjectives[id] = true

        TestMoveTo(
            selected.unit,
            objective
        )
    end

    Log(
        "========================================"
    )
end

------------------------------------------------------------
-- SCAN OBJECTIVES
------------------------------------------------------------

local function ScanObjectives(buildings, units)
    local count = 0

    for _, building in ipairs(buildings) do
        if IsOilDerrick(building) then
            local id = SafeId(building)

            if id and not inspectedObjectives[id] then
                count = count + 1

                if count <= MAX_OBJECTIVES then
                    TestObjective(
                        building,
                        units
                    )
                end
            end
        end
    end
end

------------------------------------------------------------
-- BASIC API TEST
------------------------------------------------------------

local function InitialDiagnostic()
    Log("========================================")
    Log("LuaAPI AI Probe started")
    Log("Testing confirmed gameplay surface")
    Log("Engine.PrintMessage = NOT USED")
    Log("Repeated MoveTo = PREVENTED")
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
    -- World.
    --------------------------------------------------------

    local human = House.GetPlayer()

    if not human then
        return
    end

    local units = World.GetUnits()

    if not units then
        Log(
            "World.GetUnits() returned nil"
        )

        return
    end

    local buildings = World.GetBuildings()

    if not buildings then
        Log(
            "World.GetBuildings() returned nil"
        )

        return
    end

    --------------------------------------------------------
    -- Scan oil derricks.
    --------------------------------------------------------

    ScanObjectives(
        buildings,
        units
    )
end

------------------------------------------------------------
-- RESET
------------------------------------------------------------

function AIProbe.Reset()
    lastTick = 0

    inspectedObjectives = {}
    commandedObjectives = {}

    sessionStarted = false

    Log(
        "AI Probe state reset"
    )
end

return AIProbe