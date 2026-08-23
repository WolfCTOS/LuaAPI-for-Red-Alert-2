-- Tesla Overload mechanic
-- EMP-locks and damages enemy buildings within radius of player units,
-- or map-wide when DEBUG_MAP_WIDE is enabled.
local TeslaOverload = {}

-- Config settings
local DEBUG_MAP_WIDE = true     -- Set to true to test globally without moving units!
local OVERLOAD_RADIUS = 8.0     -- Normal radius in cells (used when DEBUG_MAP_WIDE = false)
local DAMAGE_PER_PULSE = 30     -- HP per shock
local PULSE_INTERVAL = 30       -- Every 30 frames (~1 second)

local lastPulseFrame = 0
local victoryLogged = false

function TeslaOverload.Update(frame)
    if frame - lastPulseFrame < PULSE_INTERVAL then
        return
    end
    lastPulseFrame = frame

    local player = House.GetPlayer()
    if not player then return end

    local buildings = World.GetBuildings()
    local units = World.GetUnits()

    -- Victory condition: no living enemy buildings left -> stop pulsing.
    local enemyBuildingCount = 0
    for _, bld in ipairs(buildings) do
        if bld:IsAlive() then
            local bldOwner = bld:GetOwner()
            if bldOwner and not player:IsAlliedWith(bldOwner) and bldOwner:GetName() ~= "Neutral" then
                enemyBuildingCount = enemyBuildingCount + 1
            end
        end
    end

    if enemyBuildingCount == 0 then
        if not victoryLogged then
            victoryLogged = true
            print("[LuaAPI] No enemy buildings remain. Tesla Overload standing by.")
            Engine.PrintMessage("All enemy structures destroyed!")
        end
        return
    end

    -- Find living player units
    local playerUnits = {}
    for _, u in ipairs(units) do
        if u:IsAlive() then
            local owner = u:GetOwner()
            if owner and owner:GetName() == player:GetName() then
                table.insert(playerUnits, u)
            end
        end
    end

    -- If player has no units on map, wait
    if #playerUnits == 0 then return end

    local overloadedCount = 0

    -- Check enemy buildings
    for _, bld in ipairs(buildings) do
        if bld:IsAlive() then
            local bldOwner = bld:GetOwner()
            -- Target only real enemies (exclude Neutral)
            if bldOwner and not player:IsAlliedWith(bldOwner) and bldOwner:GetName() ~= "Neutral" then

                local canOverload = false
                local targetDist = 0.0

                if DEBUG_MAP_WIDE then
                    canOverload = true
                    targetDist = 99.9
                else
                    for _, u in ipairs(playerUnits) do
                        local dist = u:GetDistanceTo(bld)
                        if dist and dist <= OVERLOAD_RADIUS then
                            canOverload = true
                            targetDist = dist
                            break
                        end
                    end
                end

                if canOverload then
                    overloadedCount = overloadedCount + 1

                    -- Apply real EMP power lockout
                    bld:Disable(45)

                    -- Deal progressive electrical damage
                    local hpLeft = bld:TakeDamage(DAMAGE_PER_PULSE)
                    local bldName = bld:GetTypeName()

                    -- HUD alert for key structures (throttled)
                    if overloadedCount <= 2 then
                        Engine.PrintMessage(string.format("⚡ [Tesla Overload] %s EMP Locked! HP: %d", bldName, hpLeft))
                    end
                    print(string.format("[LuaAPI] ⚡ Overloading %s (Owner: %s) -> HP: %d", bldName, bldOwner:GetName(), hpLeft))
                end
            end
        end
    end
end

return TeslaOverload
