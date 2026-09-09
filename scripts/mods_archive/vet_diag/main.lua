local VetDiag = {}

local lastSeen = {}
local alive = false

local function report(unit)
    local id = unit:GetId()
    local vet = unit:GetVeterancy()
    local ammo = unit:GetAmmo()
    local key = vet .. "|" .. ammo
    if lastSeen[id] ~= key then
        lastSeen[id] = key
        Engine.PrintMessage(string.format("[VET] %s veterancy=%s ammo=%d",
            unit:GetTypeName(), vet, ammo), 1)
    end
end

function VetDiag.Update(frame)
    if not alive then
        alive = true
        Engine.PrintMessage("[VET] mod alive", 1)
    end
    if frame % 60 ~= 0 then return end

    local player = House.GetPlayer()
    if not player then return end

    for _, u in ipairs(World.GetUnits()) do
        if u:IsAlive() and u:GetKind() ~= "building" then
            local owner = u:GetOwner()
            if owner and (owner == player or owner:IsAlliedWith(player)) then
                report(u)
            end
        end
    end
end

return VetDiag