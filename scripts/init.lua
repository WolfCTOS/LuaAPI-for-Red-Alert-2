-- Minimal LuaAPI Proof of Concept
print("[LuaAPI] Lua script loaded successfully!")
if Engine then
    print("[LuaAPI] Engine table is present.")
end

print("[LuaAPI] Initializing gameplay scripts...")

-- Test HUD message directly on screen
if Engine.PrintMessage then
    Engine.PrintMessage("LuaAPI Connected! Welcome Commander.")
end

local tickCount = 0

function OnTick(frame)
    tickCount = tickCount + 1
    -- Print a message every 300 frames (~10-15 seconds)
    if tickCount % 300 == 0 then
        print("[LuaAPI] Game frame: " .. tostring(frame or tickCount))
    end
end
