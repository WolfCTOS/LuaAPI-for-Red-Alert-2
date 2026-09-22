-- waypoint_probe (M2 runtime verification probe, Beta scaffolding).
--
-- Purpose: prove GetWaypoint(id) returns real map waypoints, not origin.
-- NOT in scripts/active_mods.txt by default. To run the M2 protocol:
--   1. Append `waypoint_probe` to scripts/active_mods.txt.
--   2. Launch via injector, start a skirmish match on a map with known
--      waypoints (any standard map defines several [Waypoints] entries).
--   3. Expect in LuaAPI.log:
--        [WPPROBE] id=<n> x=<cell> y=<cell>   for defined waypoints
--        [WPPROBE] id=<n> nil                 for undefined/invalid ids
--      Two or more defined ids must report DIFFERENT positions, matching
--      the map's [Waypoints] section (map file, same scenario).
--   4. Return to menu, start another match: the probe re-runs (one block
--      per match) — proves post-reset behavior (Gate 1.3 interplay).
--   5. Remove the id from active_mods.txt afterwards.

local Probe = {}

local lastFrame = 0
local probed = false

local IDS = { 0, 1, 2, 3, 4, 5, 6, 7, -1, 701, 702 }

function Probe.Update(frame)
    -- New match (frame counter restarted): probe again.
    if frame < lastFrame then probed = false end
    lastFrame = frame

    if probed then return end
    if frame < 30 then return end -- let the scenario settle
    probed = true

    for _, id in ipairs(IDS) do
        local ok, pos = pcall(World.GetWaypoint, id)
        if ok and type(pos) == "table" and pos.x and pos.y then
            local msg = string.format("[WPPROBE] id=%d x=%d y=%d", id, pos.x, pos.y)
            Engine.PrintMessage(msg)
            print("[LuaAPI] " .. msg)
        else
            local msg = string.format("[WPPROBE] id=%d nil (ok=%s)", id, tostring(ok))
            print("[LuaAPI] " .. msg)
        end
    end
end

return Probe
