-- M16 Gate 2B Diagnostic: SCHP Deploy Cycle Test
-- Tests Deploy(), Undeploy(), CanDeployNow(), IsDeployed(), IsDeploying(), IsUndeploying() on real SCHP

print("[M16] Gate 2B main.lua loaded")
Engine.PrintMessage("[M16-G2B] DIAGNOSTIC VERSION: 2026-09-16-G2B-v7")

local HeliDiag = {}

local CHECK_INTERVAL = 15  -- Check every 15 frames (~0.5s)
local frameCounter = 0
local testSCHPId = nil
local deployTriggered = false
local trackStartFrame = 0

-- Helper: log SCHP state
local function logSCHPState(unit, label)
    local id = unit:GetId()
    local typeName = unit:GetTypeName()
    local kind = unit:GetKind()
    local isOnFloor = unit:IsOnFloor()
    local isInAir = unit:IsInAir()
    local isLanding = unit:IsLanding()
    local isDeployed = unit:IsDeployed()
    local isDeploying = unit:IsDeploying()
    local isUndeploying = unit:IsUndeploying()
    local canDeploy = unit:CanDeployNow()
    local mission = unit:GetMission()
    local hp = unit:GetHealth()
    local maxHp = unit:GetMaxHealth()
    local hpPct = maxHp > 0 and math.floor((hp / maxHp) * 100) or 0
    
    Engine.PrintMessage(string.format("[M16-G2B] %s | %s (id=%d) | hp=%d/%d (%d%%) | kind=%s | mission=%s | floor=%s | air=%s | landing=%s | deployed=%s | deploying=%s | undeploying=%s | canDeploy=%s",
        label, typeName, id, hp, maxHp, hpPct, tostring(kind), tostring(mission),
        tostring(isOnFloor), tostring(isInAir), tostring(isLanding),
        tostring(isDeployed), tostring(isDeploying), tostring(isUndeploying), tostring(canDeploy)))
end

function HeliDiag.Update(frame)
    frameCounter = frameCounter + 1
    
    if frameCounter % CHECK_INTERVAL ~= 0 then
        return
    end
    
    local units = World.GetUnits()
    
    -- Find an airborne SCHP (not deployed, not deploying)
    local targetSCHP = nil
    for _, unit in ipairs(units) do
        if unit:IsAlive() and unit:GetTypeName() == "SCHP" then
            local isInAir = unit:IsInAir()
            local isOnFloor = unit:IsOnFloor()
            local isDeployed = unit:IsDeployed()
            
            if isInAir and not isOnFloor and not isDeployed then
                targetSCHP = unit
                break
            end
        end
    end
    
    if targetSCHP then
        local id = targetSCHP:GetId()
        
        if not deployTriggered then
            -- First time seeing this airborne SCHP - log initial state
            logSCHPState(targetSCHP, "DEPLOY TEST INITIAL")
            
            -- Check CanDeployNow
            local canDeploy = targetSCHP:CanDeployNow()
            Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST CanDeployNow=%s", tostring(canDeploy)))
            
            if canDeploy then
                -- Trigger Deploy()
                Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST Deploy() triggering | id=%d", id))
                local ok = targetSCHP:Deploy()
                Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST Deploy() result | id=%d | returned=%s", id, tostring(ok)))
                
                if ok then
                    deployTriggered = true
                    testSCHPId = id
                    trackStartFrame = frame
                else
                    Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST Deploy() returned false, will retry"))
                end
            else
                Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST CanDeployNow=false, waiting"))
            end
        elseif id == testSCHPId then
            -- Track the test SCHP through deploy
            logSCHPState(targetSCHP, "DEPLOY TEST TRACKING")
            
            -- Check for stable deployed state
            local isOnFloor = targetSCHP:IsOnFloor()
            local isInAir = targetSCHP:IsInAir()
            local isDeployed = targetSCHP:IsDeployed()
            local isDeploying = targetSCHP:IsDeploying()
            local framesTracked = frame - trackStartFrame
            
            if isDeployed and not isInAir and isOnFloor and not isDeploying then
                Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST LANDED | id=%d | framesTracked=%d", id, framesTracked))
                testSCHPId = nil
                deployTriggered = false
            elseif framesTracked > 2000 then  -- ~60 seconds timeout
                Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST TIMEOUT | id=%d | framesTracked=%d", id, framesTracked))
                testSCHPId = nil
                deployTriggered = false
            end
        end
    else
        -- Reset if we lost the SCHP or it deployed
        if testSCHPId and deployTriggered then
            Engine.PrintMessage(string.format("[M16-G2B] DEPLOY TEST LOST/COMPLETE | id=%d | resetting", testSCHPId))
            testSCHPId = nil
            deployTriggered = false
        end
    end
end

return HeliDiag