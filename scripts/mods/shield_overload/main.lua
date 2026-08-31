-- Shield Overload mod: absorbs 50% of incoming damage via OnPreDamage event
local ShieldOverload = {}

-- Called from C++ via game_RegisterEvent("OnPreDamage", callback)
function ShieldOverload.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    -- Absorb 50% of all energy and explosive damage
    local absorbed_types = {"energy", "explosive", "emp"}
    for _, dt in ipairs(absorbed_types) do
        if dmg_type == dt then
            -- Return half damage; the C++ side will apply the reduction
            return damage * 0.5
        end
    end
    -- Return nil to indicate "no change" (original damage applied)
    return nil
end

-- Register the event with the C++ injector
-- This must be called after the module is loaded
function ShieldOverload.OnRegister()
    game_RegisterEvent("OnPreDamage", ShieldOverload.OnPreDamage)
    print("[Shield Overload] OnPreDamage event registered")
end

-- Standard Update function (no special handling needed for events)
function ShieldOverload.Update(frame)
    -- Events are fired by the C++ hook; Update can remain empty
    -- or perform per-frame logic if needed
end

return ShieldOverload