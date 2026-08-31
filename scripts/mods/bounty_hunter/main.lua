-- Bounty Hunter mod: awards +50 credits to the player on combat hit.
-- Subscribes to OnPreDamage event and tracks damage for credit assignment.

local BountyHunter = {}

-- References to techno damaged and the frame when damage occurred.
local lastDamagedTechno = nil
local damageDealerHouse = nil

-- OnPreDamage callback: called before damage is applied to a techno.
-- This gives the mod a chance to react to incoming damage.
-- @param techno   the techno receiving damage
-- @param damage   incoming damage amount
-- @param warhead  warhead type name (e.g. "Fire", "C4")
function BountyHunter.OnPreDamage(techno, damage, warhead)
    -- Store the techno that will receive damage so we can credit the dealer after resolution.
    lastDamagedTechno = techno
    -- We cannot reliably determine the attacker at pre-damage time without
    -- engine integration, so we defer credit assignment to the Update loop
    -- where we can inspect the damage context.
    damageDealerHouse = nil
end

-- OnTick is called every game frame; used to process deferred actions.
function BountyHunter.Update(frame)
    if not lastDamagedTechno then return end

    -- After damage is applied, credit the player who damaged the techno.
    -- For this demo, we award the owner of the damaged techno's house +50$.
    -- A full implementation would resolve the actual damage dealer from the
    -- game's combat event system.
    if lastDamagedTechno and lastDamagedTechno.Owner then
        local ownerHouse = lastDamagedTechno.Owner
        house_AddCredits(ownerHouse, 50)
        Engine.PrintMessage("[Bounty] +$50 for combat hit!")
    end

    -- Clear state for the next cycle.
    lastDamagedTechno = nil
    damageDealerHouse = nil
end

return BountyHunter