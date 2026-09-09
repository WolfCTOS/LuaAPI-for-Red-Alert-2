-- Combat State Tracker (M14 Gate 1) — in-game showcase.
--
-- Purpose: prove the state-tracking layer works during a real Yuri's Revenge
-- session. It does NOT make tactical decisions; it only observes.
--
-- What it demonstrates (the Gate 1 acceptance criteria):
--   1. Several units are tracked.
--   2. Units move and/or engage enemies.
--   3. HP changes.
--   4. Targets change.
--   5. A tracked unit is destroyed.
--   6. Its state is invalidated correctly.
--   7. Other tracked units keep working.
--   8. No crash.
--   9. No stale pointer is dereferenced.
--  10. The system stays stable over prolonged gameplay.
--
-- Safety contract enforced here (mirrors the whole framework):
--   * The tracker stores ONLY primitive snapshots, keyed by native id.
--   * Every frame re-resolves each unit from a FRESH World scan. No Techno /
--     House userdata is held by this mod or the tracker across frames.
--   * The script-held userdata (`scanUnit`) is re-fetched each update and only
--     ever used within that frame.
--
-- How to run: enable `combat_state_tracker` in the launcher and start a
-- skirmish against any AI. Watch LuaAPI.log for [CSTATE] lines. Point your
-- units at the enemy; the tracker logs their HP, target, and mission changes and
-- reports when a tracked unit is destroyed.

local CombatState = require("framework.combat_state")

local Mod = {}

local TICK_EVERY    = 15   -- frames between state reports (decision/scan cadence)
local MAX_TO_TRACK  = 6    -- cap so we don't churn log output on huge maps
local REPORT_EVERY  = 90   -- frames between full status dumps

-- Per-unit state used to log only meaningful transitions (throttled).
local tracked = {}         -- id -> { lastHp, lastMission, lastTargetId, lastReport }
local tracker = nil
local warnedNoUnit = false
local eventsWired = false

local function msg(text)
    if Engine and Engine.PrintMessage then
        Engine.PrintMessage(text)
    end
end

local function log(text)
    print("[CSTATE] " .. text)
end

-- Scan a unit's Gameplay fields defensively (pcall guard; never trust a stale
-- userdata). Returns nil if the unit is no longer valid.
local function scanUnit(unit)
    if not unit then
        return nil
    end
    local okAlive, alive = pcall(unit.IsAlive, unit)
    if not okAlive or not alive then
        return nil
    end
    local okId, id = pcall(unit.GetId, unit)
    if not okId or not id then
        return nil
    end
    local okHp, hp = pcall(unit.GetHealth, unit)
    local okMax, maxHp = pcall(unit.GetMaxHealth, unit)
    local okMission, mission = pcall(unit.GetMission, unit)
    local okTarget, target = pcall(unit.GetTarget, unit)

    local targetId
    if okTarget and target then
        local okTId, tid = pcall(target.GetId, target)
        if okTId then targetId = tid end
    end

    local ownerName
    local okOwner, owner = pcall(unit.GetOwner, unit)
    if okOwner and owner and owner.GetName then
        local okName, name = pcall(owner.GetName, owner)
        if okName then ownerName = name end
    end

    return {
        id      = id,
        hp      = okHp and hp or nil,
        maxHp   = okMax and maxHp or nil,
        mission = okMission and mission or nil,
        targetId = targetId,
        ownerName = ownerName,
    }
end

local function observe(unit, frame)
    local s = scanUnit(unit)
    if not s then
        return -- invalid; tracker will pick up the death via re-resolution
    end
    local prev = tracked[s.id]
    if not prev then
        tracked[s.id] = {
            lastHp       = s.hp,
            lastMission  = s.mission,
            lastTargetId = s.targetId,
            lastOwner    = s.ownerName,
            lastReport   = frame,
        }
        log(string.format("unit #%d (%s, %s) now tracked: %d/%d HP",
            s.id, tostring(s.ownerName or "?"), tostring(s.mission or "?"),
            s.hp or -1, s.maxHp or -1))
        return
    end

    local ui = pcall(string.format,
        "unit #%d changes: hp %s->%s | mission %s->%s | target %s->%s",
        s.id, tostring(prev.lastHp), tostring(s.hp),
        tostring(prev.lastMission), tostring(s.mission),
        tostring(prev.lastTargetId), tostring(s.targetId))
    -- Only report HP / target / mission transitions (not every identical frame),
    -- so the log stays readable during a long session.
    local changed = (prev.lastHp ~= s.hp)
        or (prev.lastMission ~= s.mission)
        or (prev.lastTargetId ~= s.targetId)
    if changed then
        log("unit #" .. s.id .. ":" ..
            (prev.lastHp ~= s.hp and (" hp " .. prev.lastHp .. " -> " .. s.hp) or "") ..
            (prev.lastMission ~= s.mission and (" mission " .. prev.lastMission .. " -> " .. s.mission) or "") ..
            (prev.lastTargetId ~= s.targetId and (" target " .. prev.lastTargetId .. " -> " .. s.targetId) or ""))
        prev.lastHp = s.hp
        prev.lastMission = s.mission
        prev.lastTargetId = s.targetId
    end
end

function Mod.Update(frame)
    -- Initialise the tracker once per session (wired lazily to the first tick).
    if not tracker then
        tracker = CombatState.new({
            keepDead = true,   -- keep lastValid so post-mortem state is queryable
            invalidTtl = 180,  -- keep dead records ~3 s for reporting
            pulseEvery = 10,
        })
        log("CombatStateTracker initialized")
    end

    -- Track a few player units so we can observe them. Prefer units the player
    -- owns; fall back to any mobile unit if the player has none yet.
    if tracker:count() < MAX_TO_TRACK then
        local ok, units = pcall(World.GetUnits)
        if ok and units then
            local player = House.GetPlayer()
            for _, u in ipairs(units) do
                if tracker:count() >= MAX_TO_TRACK then
                    break
                end
                local okId, id = pcall(u.GetId, u)
                if okId and id and not tracker:has(id) then
                    local okA, alive = pcall(u.IsAlive, u)
                    if okA and alive then
                        local keep = false
                        if player then
                            local okO, owner = pcall(u.GetOwner, u)
                            if okO and owner and owner == player then
                                keep = true
                            end
                        end
                        if keep or not player then
                            tracker:track(u)
                            observe(u, frame)
                        end
                    end
                end
            end
        end
    end

    -- Incremental transitions every decision tick.
    if frame % TICK_EVERY == 0 then
        local ok, units = pcall(World.GetUnits)
        if ok and units then
            for _, u in ipairs(units) do
                local okId, id = pcall(u.GetId, u)
                if okId and id and tracker:has(id) then
                    observe(u, frame)
                end
            end
        end
    end

    -- Refresh tracker state (re-resolve by id, detect destruction / target loss).
    tracker:update(frame)

    -- Report invalidations (deferred, safe). This is the "unit destroyed" path.
    local invalid = tracker:drain_invalidated()
    for _, rec in ipairs(invalid) do
        local last = rec.lastValid or {}
        log(string.format(
            "unit #%d (%s) DESTROYED -> invalidated; last known: %d/%d HP, mission=%s, target=%s",
            rec.id, tostring(last.typeName or "?"),
            last.hp or -1, last.maxHp or -1,
            tostring(last.mission or "?"),
            tostring(last.targetId or "nil")))
        msg(string.format("[CSTATE] unit #%d (%s) destroyed", rec.id, tostring(last.typeName or "?")))
        tracked[rec.id] = nil
    end

    -- Periodic full status dump for long-run stability evidence.
    if frame % REPORT_EVERY == 0 then
        local alive, dead, missing = 0, 0, 0
        for _, id in ipairs(tracker:ids()) do
            if tracker:is_alive(id) then
                alive = alive + 1
            else
                dead = dead + 1
            end
        end
        log(string.format(
            "status @frame %d: tracked=%d alive=%d dead=%d",
            frame, tracker:count(), alive, dead))
    end
end

return Mod
