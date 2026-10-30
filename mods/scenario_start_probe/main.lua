-- scenario_start_probe (M1 runtime verification probe, Beta scaffolding).
--
-- Purpose: make OnScenarioStart visibly verifiable in a live match.
-- NOT in scripts/active_mods.txt by default. To run the M1 protocol:
--   1. Append `scenario_start_probe` to scripts/active_mods.txt.
--   2. Launch via injector, start a skirmish/scenario match.
--   3. Expect in LuaAPI.log + HUD:
--        [PROBE] OnScenarioStart fired at frame 1  (exactly once)
--        [PROBE] Update heartbeat (first dispatched Update)
--   4. Start a SECOND match in the same process (Gate 1.3 interplay):
--      the marker must appear exactly once per match.
--   5. Remove the id from active_mods.txt afterwards.
--
-- Uses the DOCUMENTED pattern (file-scope global). If this marker never
-- appears while Update heartbeats do, the M1 loader shadowing regressed.

local Probe = {}

local started = false
local heartbeated = false

-- Documented pattern: file-scope global. The loader must preserve this
-- (defaults are defined BEFORE mods load in scripts/init.lua).
function OnScenarioStart()
    if not started then
        started = true
        Engine.PrintMessage("[PROBE] OnScenarioStart fired at frame 1")
        print("[LuaAPI] [PROBE] OnScenarioStart fired at frame 1")
    end
end

function Probe.Update(frame)
    if not heartbeated then
        heartbeated = true
        Engine.PrintMessage("[PROBE] Update heartbeat at frame " .. tostring(frame))
        print("[LuaAPI] [PROBE] Update heartbeat at frame " .. tostring(frame))
    end
end

return Probe
