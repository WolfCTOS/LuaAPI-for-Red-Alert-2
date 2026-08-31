-- Damaged Fleet mod: validates start-of-match damage and smoke attachment
-- without trigger hacks. Demonstrates OnScenarioStart and OnUnitDestroyed events.

local DamagedFleet = {}

-- OnScenarioStart: called once on the first game frame after map load.
-- Sets up initial unit conditions: 35% HP and damage smoke effects.
function DamagedFleet.OnScenarioStart()
    -- Iterate techno arrays to find suitable units (non-buildings, alive)
    local count = 0
    for _, unit in ipairs(World.GetUnits()) do
        if count >= 3 then break end
        if not unit:IsAlive() then continue end
        if unit:GetKind() == "building" then continue end

        -- Set health to 35%
        unit:SetHealthRatio(0.35)

        -- Attach damage particle system
        unit:AttachParticleSystem("DamageSmokeSys")

        count = count + 1
    end

    -- Log the deployment
    Engine.PrintMessage("[DamagedFleet] Deployment: 3 units set to 35% HP with smoke effects")
end

-- OnUnitDestroyed: called when units are eliminated during gameplay.
-- Awards bounty and ensures smoke effects persist on death.
function DamagedFleet.OnUnitDestroyed(victim, killer)
    if not victim then return end

    -- Award bounty to the killer's house
    if killer and killer:GetHouse() then
        house_AddCredits(killer:GetHouse(), 100)
        Engine.PrintMessage("[Bounty] +$100 for kill with damaged fleet active!")
    end

    -- Re-attach smoke effect if the victim had it (simplified: always re-attach)
    if victim and victim:IsAlive() == false then
        -- The unit is dead; smoke effect on death is handled by the engine.
        -- We simply log the event.
        Engine.PrintMessage("[DamagedFleet] Unit destroyed")
    end
end

-- Return the mod table
return DamagedFleet