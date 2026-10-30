-- M16 AUTO: barrel_elevation_diag (fully automatic mode)
--
-- NO HOTKEYS (user request: "make it automatic so the tank's AI decides
-- itself"). The mod turns the native global AUTO on once at match start and
-- then only observes and logs.
--
-- What happens natively (src/barrel_pitch.cpp):
--   * The DrawAsVXL detour decides the barrel pitch for EVERY unit with a
--     voxel turret - player units and AI units alike - from the live target
--     distance (8 deg at 4 cells .. 55 deg at 14 cells, linear).
--   * No target -> the unit draws vanilla. No simulation changes: draw-only,
--     client-local, CnCNet-safe.
--
-- FRAME CYCLER (path C probe, fully automatic):
--   Discord root-cause (Cranium [CCO]): stock YR tanks bake turret+barrel into
--   ONE voxel, so FireAngle has nothing to rotate. The TS mechanism is HVA
--   frames encoding barrel-pitch poses per voxel section. Every 2 seconds the
--   mod cycles TurretAnimFrame on every unit whose turret HVA has multiple
--   frames (stock: YTNK with 2; future: Chrono's turret(and barrel!) unit) and
--   logs it. If the model visibly changes pose, HVA frame transforms are
--   applied in the unit draw path -> path C is proven with a real asset.
--
-- Tuning: edit the AutoPitch* constants in src/barrel_pitch.cpp and rebuild
-- (the curve lives natively by design - it must be available inside the draw
-- detour, and Lua is not called per draw call).

local DIAG = {}

local HEARTBEAT_FRAMES = 60  -- one status line per second
local FRAME_CYCLE_FRAMES = 120 -- HVA frame cycle step: every 2 seconds

local function log(msg)
    print("[M16-AUTO] " .. msg)
end

local function hud(msg)
    Engine.PrintMessage("[M16-AUTO] " .. msg)
end

local function enableGlobalAuto(announce)
    Engine.SetBarrelPitchAutoAll(true)
    if not announce then return end -- silent re-assert after match restart
    log("global AUTO enabled - every voxel-turret unit pitches by target distance")
    hud("Barrel AUTO: pitch by distance (no keys)")
end

-- One status line per second: how many units are drawing with an AUTO-computed
-- pitch right now (native counter) plus a couple of live unit samples.
local function heartbeat()
    local active = Engine.GetBarrelPitchAutoCount()

    if active == 0 then
        log("heartbeat: no units pitching (waiting for engagements)")
        return
    end

    local samples = {}
    local units = World.GetUnits()
    if units then
        for _, unit in ipairs(units) do
            if #samples >= 3 then break end
            if unit and unit:IsAlive() then
                local _, deg = Engine.GetBarrelPitchAuto(unit:GetId())
                if deg then
                    samples[#samples + 1] = string.format("%s %.1fdeg",
                        unit:GetTypeName(), deg)
                end
            end
        end
    end

    log(string.format("heartbeat: pitching=%d (%s)",
        active, #samples > 0 and table.concat(samples, "; ") or "angles in native draw log"))
end

-- Path C probe: cycle the HVA frame on every multi-frame-turret unit in the
-- world (YTNK today, custom assets tomorrow). Sequence per unit per step:
--   frame 0 -> mid -> last -> 0 ... The engine rewrites TurretAnimFrame while
--   the turret rotates; we override every step, which is exactly the Gate 1
--   hold mechanism, now keyless.
local function cycleHvaFrames()
    local units = World.GetUnits()
    if not units then return end

    for _, unit in ipairs(units) do
        if unit and unit:IsAlive() then
            local count = unit:GetTurretAnimFrameCount()
            if count and count > 1 then
                local current = unit:GetTurretAnimFrame()
                local nextFrame
                if current >= count - 1 then
                    nextFrame = 0
                elseif current >= math.floor(count / 2) then
                    nextFrame = count - 1
                else
                    nextFrame = math.floor(count / 2)
                end

                local applied = unit:SetTurretAnimFrame(nextFrame)
                log(string.format("FRAME id=%d type=%s frame=%d/%d (cycled)",
                    unit:GetId(), unit:GetTypeName(), applied, count))
            end
        end
    end
end

function DIAG.Update(frame)
    -- Enable once per session; the native flag is idempotent (no log spam).
    if frame == 1 then
        enableGlobalAuto(true)
    end

    -- Re-assert every heartbeat so a match restart (native ResetSession
    -- clears the flag via ClearAll) recovers within a second.
    if frame % HEARTBEAT_FRAMES == 0 then
        enableGlobalAuto(false)
        heartbeat()
    end

    -- Keyless path C probe: walk all multi-frame HVA units through their
    -- pitch poses every 2 seconds.
    if frame % FRAME_CYCLE_FRAMES == 0 then
        cycleHvaFrames()
    end
end

return DIAG
