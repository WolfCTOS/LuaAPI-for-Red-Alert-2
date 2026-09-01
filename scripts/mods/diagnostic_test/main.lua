local DiagnosticTest = {}

function DiagnosticTest.Update(frame)
    if frame ~= 300 then return end
    
    local player = House.GetPlayer()
    if not player then
        Engine.PrintMessage("[DIAG] no player house", 2)
        return
    end
    
    Engine.PrintMessage("[DIAG] player house found", 1)
    
    local units = World.GetUnits()
    Engine.PrintMessage("[DIAG] total units: " .. #units, 1)
    
    local playerUnit = nil
for _, u in ipairs(units) do
    if u:IsAlive() then
        local owner = u:GetOwner()
        -- Сравниваем через IsAlliedWith вместо ==
        if owner and (owner == player or owner:IsAlliedWith(player)) then
            playerUnit = u
            break
        end
    end
end

    if not playerUnit then
        Engine.PrintMessage("[DIAG] no player unit found", 2)
        return
    end
    
    Engine.PrintMessage("[DIAG] player unit: " .. playerUnit:GetTypeName(), 1)
    
    -- Check the new bindings
    if playerUnit.GetMission then
        Engine.PrintMessage("[DIAG] mission: " .. tostring(playerUnit:GetMission()), 1)
    end
    
    if playerUnit.IsIdle then
        Engine.PrintMessage("[DIAG] idle: " .. tostring(playerUnit:IsIdle()), 1)
    end
    
    if playerUnit.GetTarget then
        local target = playerUnit:GetTarget()
        Engine.PrintMessage("[DIAG] target: " .. tostring(target), 1)
    end
    
    -- Try MoveTo
    local pos = playerUnit:GetPosition()
    if playerUnit.MoveTo then
        local result = playerUnit:MoveTo(pos.x + 3, pos.y)
        Engine.PrintMessage("[DIAG] MoveTo result: " .. tostring(result), 1)
    end
end

return DiagnosticTest
