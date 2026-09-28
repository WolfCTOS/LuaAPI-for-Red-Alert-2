--[[========================================================================
  dynamic_fow  --  shroud that comes back when you walk away. AUTOMATIC.

  WHY v0.2 CHANGED ITS MIND
    v0.1 used World.IsCellLit (shroud frame == -1) as the "don't touch this,
    someone is watching" guard. Live evidence killed it: the lit count fell
    213 -> 113 -> 20 -> 4 -> 0 over 60 sweeps while reshrouded also fell to 0,
    and the screenshot showed the whole map going black around the player's own
    base.

    Cause: -1 is entry 0x00 of the 0x7F4194 neighbour table, i.e. "no NEIGHBOUR
    is in shroud" - a statement about the surroundings, not about this cell. The
    mod shrouded those surroundings, so the guard became false for everything
    it had touched. Positive feedback loop.

    v0.2 uses CellClass+0x130 ShroudCounter, the engine's own sight-source
    counter. It is maintained by Reduce/IncreaseShroudCounter from real unit
    sight and is > 0 exactly while a unit covers the cell, so it cannot be
    destroyed by the mod's own writes.

  WHY v0.3 REMOVED THE FRAME CLAUSE AND RE-ARMS THE REVEAL PATH
    v0.2 kept `or shroudFrame == -1` as a "conservative" second skip. That is
    a second permanence bug: after a unit leaves, the engine leaves the last
    frame at -1, so the clause skips the cell forever and black never comes
    back. Guard is now counter-only.
    And the C++ write re-arms Center/Edge (+0x140 &= ~0x03): without it,
    RevealArea1 skips fully-revealed interior cells every frame (0x567870),
    MapCellVisibility never recomputes +0x120, and the square is permanent -
    proven live 2026-09-28 (static black square, tank standing inside it,
    checkerboard holes where boundary cells did recompute).

  WHY v0.9 USES HYSTERESIS (Schmitt trigger, not grace)
    v0.8 log: reblack ≈ 50% of writes forever (patrols pacing + fire-reveal
    vs 15-frame regrow). Grace-4 turned blinking into 4-sweep holes; longer
    grace froze them (v0.7 lesson). The correct tool for an oscillating
    indicator is hysteresis, not delay: per-cell streak in [-1,+2], +1 per
    abandoned sweep, -1 per watched/protected sweep. Mark+write at +2 (two
    consecutive abandoned), unmark at 0 (two consecutive watched). A single
    blip either way changes nothing; sustained presence flips in ~2s each
    way. Grace table deleted (hysteresis subsumes it); reblack stays as a
    diagnostic meter only.

  WHY v0.8 MARKS EVERYTHING, GRACE 4 (holes were frozen grace)
    v0.7 log: grace=145 in one sweep, and the screenshot shows a frozen
    checkerboard of clear holes inside black. Mechanism: chronic re-explore
    (patrols pacing the same ground + fire-reveal) reblacks constantly, and
    a 20-sweep grace pins every footprint open longer than the patrol cycle -
    holes never close. Plus the 2-cell unmarked rim soaped blue on churn.
    Fix: mark every blackened cell (MARK_MARGIN=0, crisp staircase edge, no
    blue by construction) and shorten grace to 4 sweeps: firefights stay
    open through the fight, patrol footprints close a few seconds after the
    pass. Unmark on watch stays instant (a scout must open ground now).
    v0.6 proved the frontier oscillates (reblack 60%) and never converges to
    solid black: engine partials soap the mixed zone blue. But the pixel
    resolver substitution (0x69E740, frame 15) is proven live to change pixels
    deterministically (pixelPtrDiffers=509, mode=OVERRIDE, 2026-09-28 01:37) -
    the v0.2 "static black square" was this path working without unmark logic.
    So v0.7 splits the job: engine bits (0x18 clear) carry the SIM (radar /
    targeting honesty), resolver marks carry the VISUAL (crisp black diamond,
    no neighbour computation, no partials). Marked only deeper than sight +
    MARK_MARGIN so the 2-cell rim keeps vanilla soft edges; unmarked the
    moment a cell is watched/protected again. Plus a combat grace period:
    reblacked cells are spared GRACE_SWEEPS sweeps so fire-reveal
    (RevealOnFire r=3) stays visible through the fight instead of blinking.
    Isolated -2 cells render BLACK (the holes inside green), so the blue wash
    is partial frames on a mixed frontier, not a wrong bit combo: cleared and
    explored cells salt-and-pepper, every cell sees mixed neighbours. Two
    suspects: engine re-explore beyond the guard (sight underestimate) vs
    staggered sweeping. v0.6 adds the churn meter (reblack = our cells found
    explored again), deterministic object order, and raises the write cap
    900 -> 2500. reblack ~= reshrouded means oscillation (widen the guard);
    reblack ~= 0 means pure expansion (give it time).

  WHY v0.5 CLEARS THE EXPLORED BIT (the viewport fix)
    Live 2026-09-28, v0.4: 11539 writes, fail=0, zero visual change.
    Root cause from disassembly: CellClass::DrawFog @ 0x4801F0 recomputes
    +0x120/+0x121 from GetOcclusion on EVERY draw (0x480202/0x48023E, stored
    before blitting at 0x480207/0x480243), so frame-byte writes are
    overwritten within one draw and can never show. GetOcclusion shroud-mode
    tests neighbour AltFlags & 8 - the only engine-state write the viewport
    shows is clearing the explored bit itself, which the C++ write now does
    (v0.2 did it inside a fat write that worked visually; v0.3-0.4 wrote an
    unread field). Guard unchanged (sight geometry + counter canary).

  WHY v0.4 USES SIGHT DISTANCE AS THE GUARD
    Live 2026-09-28, v0.3: 50 sweeps / 750 frames / 26827 writes,
    scPosTotal=0. ShroudCounter > 0 NEVER fires in a live match -
    IncreaseShroudCounter (bIncrease=1) has no live caller, Reduce only
    decrements. The counter signal is dead, so the guard is geometric:
    a cell inside any player object's live sight radius is skipped.
    Sight comes from the unit:GetSight() binding (INI Sight=, cells;
    == final See radius for ground units, SHROUD_RCA §2.4). Fallback 8
    when the binding is absent (pre-v0.4 DLL).

  WHAT IT DOES
    Vanilla RA2/YR removes shroud on approach and never restores it:
    TechnoClass::See reaches DisplayClass::MapCellVisibility @ 0x4A9CA0 with
    bIncrease hard-coded to 0, so only ReduceShroudCounter ever runs. This mod
    re-shrouds explored cells that nobody is currently watching, so the
    explored area is only as big as your live vision.

  PROTECTION RULES (the bug the owner reported)
    A cell is skipped when ANY of these hold:
      1. it is inside any player object's live sight radius (unit:GetSight,
         at least PROTECT) - this is what stops shroud from eating the base
         and the units standing in it, and from flickering mid-ring
      2. shroudCounter > 0 - engine sight-source count (kept as a canary;
         proved dead live 2026-09-28, never fires)
    Only then is it re-shrouded. shroudFrame == -1 is NOT a guard (stale).

  SAFETY
    Hides ground, never reveals any - not the "fake vision" that
    FSM/FEASIBILITY_TRIAGE.md:268 rules out. It does change gameplay, and it
    runs automatically, so this is opt-out by removing the mod.

  BOUNDS
    per-object box of BOX half-size cells around up to SCAN_OBJECTS of your
    units/buildings (stride-sampled), MAX_PER_SWEEP writes per sweep,
    one sweep per SWEEP_INTERVAL frames.
========================================================================]]

local Mod = {}

local SWEEP_INTERVAL = 3     -- fast loop v0.9.17: everything calibrated in sweeps
                             -- keeps its ratios; wall-clock response x1.7 vs 5f
                             -- (one-pass wake ~(6+2)x3 = 24f ~= 1.6s). Sweep
                             -- cost ~0.5ms -> ~0.9ms, well inside the frame.
local BOX            = 16
local MAX_PER_SWEEP  = 4000   -- global backstop only (pathological cases)
local BOX_WRITE_BUDGET = 80   -- writes per object box per sweep: the wake fills
local TRAIL_WRITE_BUDGET = 120 -- writes per trail pass per sweep. Senior call
                               -- v0.9.14: live bursts run 20-40/sweep, so these
                               -- ceilings bind ONLY big expansion waves (which
                               -- read as harsh full-region flips) and never
                               -- touch the normal trickle. Fill stretches over
                               -- more sweeps instead of flashing at once.
                               -- WHY v0.9.6:
                               -- a whole matured region flipping in ONE sweep
                               -- reads as a harsh step chasing the unit (live
                               -- 2026-09-28: lower wake fills first, then the
                               -- upper, in visible chunks). Pacing the writes
                               -- makes the fill progressive; every box is still
                               -- served every sweep (no starvation by design:
                               -- budgets are per-source, the trail cursor
                               -- resumes where it stopped).
local SCAN_OBJECTS   = 24    -- max unit/building boxes per sweep (stride-sampled)
local PROTECT        = 2     -- cells around any player object that stay clear
local SIGHT_FALLBACK = 8     -- used when unit:GetSight() is absent (old DLL)
local SIGHT_MAX      = 30    -- sanity clamp (Spysat-like values stay sane)
local OVERRIDE_FRAME = 15    -- fully-occluded black diamond (proven live)
local VISUAL_CRISP   = false -- A/B: true = resolver marks (crisp black, strays
                             -- possible mid-flip); false = engine bits only
                             -- (vanilla soft edge, natural, no diamonds).
                             -- Flip to false for the natural-look test.
local LOG_EVERY      = 10
local TRAIL_PER_SWEEP = 12000 -- trail cells re-checked per sweep (rolling)
local MOVE_PAD       = 1     -- extra protected ring around every object
local STATIC_PAD     = 4     -- ring for objects that have not moved (buildings)
local STILL_SWEEPS   = 3     -- sweeps at the same cell before an object counts as static
local PROGRESS_MIN   = 2     -- cells of NET displacement over PROGRESS_WIN sweeps
                             -- to arm the fast cone (v0.9.13). WHY: per-sweep
                             -- integer deltas quantize slow walkers to 1,0,1,0
                             -- so a run counter never accumulated and the cone
                             -- cycled arm/disarm = filling in WAVES. Net
                             -- progress separates travel (meters) from milling
                             -- (circles/pacers net ~0) including sub-cell
                             -- speeds. Chrono-teleports can't false-fire: the
                             -- proximity match radius (4) drops them as new.
local FAST_VEL       = 1     -- cells/sweep: objects moving at least this fast get a
                             -- fast-blacken cone behind them (v0.9.7). WHY: the
                             -- delay exists to tell "passing through" from
                             -- "gone". A steady walker's wake is unambiguous -
                             -- the unit moves AWAY, it will not flip back next
                             -- sweep - so cells behind a fast mover skip the
                             -- sight-history wait and need a single abandoned
                             -- observation. Patrols/pacers (low net velocity
                             -- at the cell, direction flips) keep the full
                             -- delay + hysteresis: that is exactly where the
                             -- v0.8 oscillation lived. Matching is by
                             -- proximity (no stable unit IDs across sweeps).
local TRAIL_ADD_MAX   = 12000 -- new trail cells registered per sweep, cap
local HEARTBEAT      = 600
-- v0.9.5 REMOVED the storm brake (was STORM_REBLACK/HOLD): holding OUR
-- writes while the engine keeps re-opening only drains the map open -
-- previously-black patrol paths leaked into permanent open diamonds with
-- nobody refilling them (live 2026-09-28: brake cycling hold=1/2, re=0
-- bursts, open-diamond speckle frozen). A one-sided pause cannot stop a
-- two-sided fight. Storms are prevented instead by a LONG sight history:
-- contested (recently-seen) ground is protected, never blackened, so there
-- is nothing to flip. Open stays open (patrolled - honest), black stays
-- black (abandoned - honest), no oscillation by construction.
local HISTORY_SWEEPS = 150   -- sight-history CAP (v0.9.16 adaptive window below)
local HISTORY_MIN    = 6     -- floor: one-pass wakes blacken fast, commuter
                             -- corridors hold long. Protection window per cell
                             -- = time it has been watched (lastSeen -
                             -- firstSeen), clamped [MIN, SWEEPS]. Corridor
                             -- seen every minute holds 150; a scout's single
                             -- pass (span 0-3) releases in ~6+2 sweeps
                             -- (~2.7s). WHY: fixed 150 bored everyone (dead
                             -- wakes wait the full window); fixed 4 churned
                             -- corridors. Watch-duration discriminates the
                             -- two, recency alone cannot. Cone still bypasses
                             -- for marchers.
                             -- last N sweeps count as watched. WHY v0.9.15:
                             -- unit audit 2026-09-29 - HISTORY WAS NEVER LONG
                             -- ENOUGH: 10 sweeps x5f = 50f ~= 3.3s, but the
                             -- harvester commute cycle is 30-60s (~100-180
                             -- sweeps). The guard never engaged; corridor
                             -- cells blackened between trips and every trip
                             -- re-opened them (live: static base + reblack
                             -- bursts + cleared 971->874->933 whipsaw). 150
                             -- sweeps x5f = 750f = 50s covers full cycles:
                             -- corridors stay open (honest - driven every
                             -- minute). Response for steady leavers still
                             -- comes from the progress cone (bypasses
                             -- history); dead-scout wakes wait the window.
                             -- last N sweeps count as watched (patrol-proof).
                             -- WHY: the guard used live positions only, but a
                             -- pacing patrol returns faster than hysteresis
                             -- converges (2 sweeps), so the mod blackened cells
                             -- the engine re-opened every frame - reblack ~50%
                             -- forever, visible as frontier shimmer (engine
                             -- bits) / black diamonds (marks). RCA 2026-09-28:
                             -- static army => reblack=0 (stable), moving army
                             -- => reblack bursts. Recently-seen ground IS
                             -- watched regularly, so protecting it removes the
                             -- fight instead of slowing it. Cost: patrol routes
                             -- stay open; far-abandoned ground still blackens.

local lastFrame  = -1
local sweeps     = 0
local total      = 0
local skipped    = 0
local protectedN = 0
local failures   = 0
local scPosObserved = 0   -- cells seen with shroudCounter > 0 this sweep
local scPosTotal    = 0   -- ... accumulated (settles whether the signal fires)
local clearedMemory = {}  -- cells WE blackened (mark+sim in sync)
local clearedCount  = 0
local reblackTotal  = 0   -- cleared cells the ENGINE re-explored (churn meter)
local reblackSweep  = 0
local streakV       = {}  -- hysteresis streak per cell, -1..+2 (numbers only)
local streakAt      = {}  -- sweep# of last touch (lazy expiry)
local hystSweep     = 0   -- cells held by hysteresis this sweep
local hystTotal     = 0
local overrideArmed = false
-- v0.9.1 TRAIL: every cell that was ever inside one of our objects' live
-- sight is remembered here, so it gets re-checked (and re-shrouded) after the
-- unit leaves, no matter how far away it is. Before this, only cells within
-- BOX of a CURRENT object were ever visited, so ground behind a far-away
-- scout was never touched again and stayed open forever.
local trailKeys, trailHas, trailCursor = {}, {}, 1
local trailSeen = {}  -- cell key -> sweep# it was last inside live sight
                      -- (sight history for the HISTORY_SWEEPS guard)
local firstSeen = {}  -- cell key -> sweep# of first sighting in the current
                      -- watch era (gap > cap restarts the era in trailAdd).
                      -- Span (lastSeen - firstSeen) sizes the guard window.
local prevPts = {}    -- previous sweep positions for velocity ({x,y} list)
local fastSweep = 0   -- diagnostic: writes via the fast cone this sweep
local censusTotal, censusMine, censusPosOk, censusSightLive = 0, 0, 0, 0
local censusPosFail = {}  -- up to 4 type names with unreadable position
local lastSeenAge = {}    -- cell key -> sweep# last inside live sight, NEVER
                          -- pruned on the guard window (unlike trailSeen): the
                          -- shroud logger measures abandonment-to-blacken age
                          -- from it. Lazy-pruned past 600 sweeps (stale).
local shroudAgeSum, shroudAgeN = 0, 0      -- mean age accumulator (window)
local shroudMaxBurst, shroudActiveSw = 0, 0 -- burst stats (window)
local shroudPrevTotal, shroudPrevFrame = 0, nil -- window baseline
                          -- (guard-hole candidates: no disc, yet possibly
                          -- seeing - or not seeing - per the engine)
local orphanSweep = 0 -- diagnostic: open cells nobody manages (not in trail,
local orphanSample = {} -- no streak, outside all sights). Persistent orphans
                      -- = coverage hole (dotted trails): visited by no box
                      -- and never stamped. Sampled on a coarse grid.
local blackWatchSweep = 0  -- diagnostic: cells black-in-data inside live sight
                           -- (engine reveal-path skipping them? v0.3 mechanism)
local blackWatchSample = {}  -- up to 8 coords of stuck cells per sweep
local stillPrev = {}   -- cell key -> consecutive sweeps an object stood there
local baseX, baseY = nil, nil
local initialised = false
local noBindReported = false

local function ensureInit()
    if initialised then return end
    initialised = true
    -- Drop stale PoC renderer marks from pre-v0.7 runs, then arm the visual
    -- override at runtime (no env dependency). Engine bits stay the sim path.
    if DynamicFow and type(DynamicFow.ClearCells) == "function" then
        DynamicFow.ClearCells()
    end
    overrideArmed = false
    if VISUAL_CRISP and DynamicFow and type(DynamicFow.SetOverrideFrame) == "function" then
        local ok, res = pcall(DynamicFow.SetOverrideFrame, OVERRIDE_FRAME)
        overrideArmed = ok and res == true
    end
    if overrideArmed then
        print("[DFOW] visual = resolver override frame 15 (crisp black).")
    else
        print("[DFOW] visual = engine bits only (override unavailable).")
    end
    print("[DFOW] START dynamic_fow v0.9.1 (trail), AUTOMATIC (no hotkeys).")
    print("[DFOW] guard = live sight radius per object (unit:GetSight).")
    print("[DFOW] sim = engine 0x18-pair clear; visual = resolver marks.")
    print("[DFOW] grace 4 sweeps for reblacked (combat) cells.")
    print(string.format("[DFOW] box=+%d per object (max %d)  protect=%d cells around every object  sweep/%df  cap %d  history=%d sweeps",
                        BOX, SCAN_OBJECTS, PROTECT, SWEEP_INTERVAL, MAX_PER_SWEEP, HISTORY_SWEEPS))
end

local function hasBindings()
    return type(World) == "table"
       and type(World.SetCellShrouded) == "function"
       and type(World.GetFogState) == "function"
       and type(World.GetAllUnits) == "function"
       and type(House) == "table"
       and type(House.GetPlayer) == "function"
end

-- All player objects, with positions and sight radii. Includes buildings where
-- the binding exposes them, because the owner's report was about the base too.
-- Sight is read once per sweep via pcall (old DLLs lack the binding → fallback).
local sightModeLogged = false
local function playerObjects()
    local player = House.GetPlayer()
    if not player then return nil, nil end
    local objs = World.GetAllUnits()
    if not objs or #objs == 0 then return nil, nil end
    local pts, n = {}, 0
    censusTotal, censusMine, censusPosOk, censusSightLive = #objs, 0, 0, 0
    censusPosFail = {}
    for _, u in ipairs(objs) do
        if u:GetOwner() == player then
            censusMine = censusMine + 1
            local p = u:GetPosition()
            if p and p.x and p.y then
                censusPosOk = censusPosOk + 1
                local sight = SIGHT_FALLBACK
                local ok, s = pcall(function() return u:GetSight() end)
                if ok and type(s) == "number" and s > 0 then
                    sight = math.min(s, SIGHT_MAX)
                    censusSightLive = censusSightLive + 1
                end
                if not sightModeLogged then
                    sightModeLogged = true
                    if ok and type(s) == "number" and s > 0 then
                        print("[DFOW] sight-aware guard (unit:GetSight live).")
                    else
                        print("[DFOW] sight FALLBACK-8 guard (unit:GetSight absent).")
                    end
                end
                n = n + 1
                pts[n] = { x = p.x, y = p.y, sight = math.max(sight, PROTECT) }
            else
                -- position unreadable: guard-hole candidate, record the type
                if #censusPosFail < 4 then
                    local ok2, tn = pcall(function() return u:GetTypeName() end)
                    censusPosFail[#censusPosFail + 1] = (ok2 and tn) or "?"
                end
            end
        end
    end
    if n == 0 then return nil, nil end
    return pts, n
end

local function centroid(pts, n)
    local sx, sy = 0, 0
    for i = 1, n do sx = sx + pts[i].x; sy = sy + pts[i].y end
    return math.floor(sx / n), math.floor(sy / n)
end

-- One cell decision. Returns "reshrouded" | "watched" | "protected" | "grace"
-- | "skip" | "fail". Mark <-> memory sync: marks ARE the visual, memory the
-- sim ledger; added and removed together, so a stale mark can never outlive
-- the engine state it depicts (the v0.2 "static square" failure).
local function markOn(x, y, key)
    if not clearedMemory[key] then
        clearedMemory[key] = true
        clearedCount = clearedCount + 1
    end
    if overrideArmed and DynamicFow
       and type(DynamicFow.SetCell) == "function" then
        pcall(DynamicFow.SetCell, x, y, true)
    end
end

local function markOff(x, y, key)
    if clearedMemory[key] then
        clearedMemory[key] = nil
        clearedCount = clearedCount - 1
    end
    streakV[key] = nil
    streakAt[key] = nil
    if overrideArmed and DynamicFow
       and type(DynamicFow.SetCell) == "function" then
        pcall(DynamicFow.SetCell, x, y, false)
    end
end

-- Hysteresis touch: +1 abandoned, -1 seen; clamp [-1,+2], prune at 0.
-- Returns the new value (0 = fresh). Only abandoned (+1) unconditionally
-- enters the table; seen-paths touch only cells already tracked or marked,
-- so the table stays bounded by the visited frontier (pruned periodically).
local function touch(key, dir, tracked)
    if dir < 0 and not tracked and streakV[key] == nil then return 0 end
    local v = (streakV[key] or 0) + dir
    if v > 2 then v = 2 end
    if v < -1 then v = -1 end
    if v == 0 then
        streakV[key] = nil
        streakAt[key] = nil
        return 0
    end
    streakV[key] = v
    streakAt[key] = sweeps
    return v
end

local function decideCell(x, y, pts, n)
    -- nearest-object overshoot: over <= 0 means inside live sight.
    local minOver = nil
    for i = 1, n do
        local dx = x - pts[i].x
        local dy = y - pts[i].y
        local r = pts[i].sight or PROTECT
        local d2 = dx * dx + dy * dy
        local over = d2 - r * r
        if minOver == nil or over < minOver then minOver = over end
    end
    -- v0.9.2: single coherent rule. The v0.9.1 far fast-path (immediate write
    -- on first visit) produced isolated writes whose neighbours blackened on
    -- later trail passes - speckle of partials/diamonds until coverage
    -- completed (the persisting artifact). Now every cell needs two abandoned
    -- observations; trail-cell streaks are exempt from the 3-sweep prune, so
    -- irregular revisits converge instead of resetting (the original reason
    -- for the fast path). No isolated writes by construction.
    local key = y * 512 + x
    -- v0.9.10 trail admission on visit: every touched cell joins the trail
    -- (box corners included - visited but never stamped, so a fast mover's
    -- corner cells pruned their streak at 1 and dotted the wake permanently:
    -- live 2026-09-28 dotted trail behind a Rhino). Members are prune-exempt
    -- and rolling-revisited: everything admitted converges. Black cells still
    -- leave the trail on "skip" (self-cleaning); only open ground
    -- accumulates, bounded by the explored area.
    if not trailHas[key] then
        trailHas[key] = true
        trailKeys[#trailKeys + 1] = key
    end
    if minOver ~= nil and minOver <= 0 then
        -- inside live sight: decay toward open, unmark at 0.
        if clearedMemory[key] or streakV[key] ~= nil then
            if touch(key, -1, true) <= 0 then markOff(x, y, key) end
        end
        -- diagnostic only: black-in-data inside TRUE (unpadded) engine sight?
        -- The padded ring (MOVE/STATIC_PAD) we protect but the engine never
        -- watches, so black there is honest FOW, not stuck. Only true-sight
        -- black counts: fresh writes re-open within a frame (small transient
        -- numbers OK); large persistent numbers mean the reveal path skips
        -- our cells (v0.3 stuck recurring).
        do
            local s = World.GetFogState(x, y)
            if s and s.shrouded then
                for i = 1, n do
                    local dx = x - pts[i].x
                    local dy = y - pts[i].y
                    local r = pts[i].baseSight or pts[i].sight or PROTECT
                    if dx * dx + dy * dy <= r * r then
                        blackWatchSweep = (blackWatchSweep or 0) + 1
                        if #blackWatchSample < 8 then
                            blackWatchSample[#blackWatchSample + 1] =
                                x .. "," .. y
                        end
                        break
                    end
                end
            end
        end
        return "protected"
    end
    -- fast cone (v0.9.7): cells behind a fast-moving object skip the history
    -- wait - the object is unambiguously leaving. Computed before the history
    -- check so it can bypass it; the hysteresis below still applies (single
    -- observation for fast cells, two otherwise).
    local fast = false
    do
        -- sustained travel only: 4-sweep net displacement >= PROGRESS_MIN.
        -- Millers/circlers/pacers net ~0 and never qualify; steady walkers
        -- do, including sub-cell speeds. Direction = the net vector itself.
        local pp = PROGRESS_MIN * PROGRESS_MIN
        for i = 1, n do
            if (pts[i].prog2 or 0) >= pp then
                local pdx = pts[i].pdx or 0
                local pdy = pts[i].pdy or 0
                local dot = (x - pts[i].x) * pdx + (y - pts[i].y) * pdy
                if dot <= -1 then fast = true break end
            end
        end
    end
    -- adaptive sight history (v0.9.16): protection window = watch duration
    -- (lastSeen - firstSeen) clamped [HISTORY_MIN, HISTORY_SWEEPS]. Same
    -- handling as live sight: decay, unmark at 0, no blackening.
    -- Fast-cone cells bypass it (they are being left, not paced).
    do
        local seen = trailSeen[key]
        if not fast and seen ~= nil then
            local first = firstSeen[key] or seen
            local window = seen - first
            if window < HISTORY_MIN then window = HISTORY_MIN end
            if window > HISTORY_SWEEPS then window = HISTORY_SWEEPS end
            if sweeps - seen < window then
                if clearedMemory[key] or streakV[key] ~= nil then
                    if touch(key, -1, true) <= 0 then markOff(x, y, key) end
                end
                return "protected"
            end
        end
    end

    local s = World.GetFogState(x, y)
    if not s or s.shrouded then return "skip" end
    -- churn meter: a cell WE blackened that reads explored again was
    -- re-explored by the engine between sweeps (sight underestimate, combat
    -- reveal, ...). reblack ~= reshrouded every sweep means the frontier
    -- oscillates; reblack ~= 0 means expansion. Diagnostic only - the
    -- hysteresis below decides, not this count.
    if clearedMemory[key] then
        reblackSweep = (reblackSweep or 0) + 1
        if touch(key, -1, true) <= 0 then markOff(x, y, key) end
        return "hyst"
    end
    -- rule 1: the engine says a unit's sight covers this cell. Counter ONLY
    -- (kept as a canary + future signal; live it never fires - see scPos).
    -- shroudFrame == -1 is deliberately NOT a skip: it is the stale last
    -- value after the unit leaves, and skipping on it protects the cell
    -- forever (v0.3 fix for the second permanence bug).
    if s.shroudCounter and s.shroudCounter > 0 then
        scPosObserved = (scPosObserved or 0) + 1
        if streakV[key] ~= nil then touch(key, -1, true) end
        return "watched"
    end
    -- abandoned: two abandoned observations to blacken (+2), one for fast-cone
    -- cells. Observations need not be consecutive sweeps for trail cells
    -- (prune-exempt below), but a single blip either way still changes nothing
    -- on the normal path - that is the whole point.
    if touch(key, 1, false) < (fast and 1 or 2) then
        hystSweep = (hystSweep or 0) + 1
        return "hyst"
    end
    if World.SetCellShrouded(x, y) then
        -- C++ clears the 0x18 pair + Center/Edge (sim honesty). The mark
        -- carries the visual over the whole blackened cell.
        markOn(x, y, key)
        -- shroud logger: abandonment age of this cell (sweeps since last
        -- seen). Cells never seen (box corners admitted on visit) carry no
        -- stamp and are excluded from the mean.
        do
            local ls = lastSeenAge[key]
            if ls ~= nil then
                shroudAgeSum = shroudAgeSum + (sweeps - ls)
                shroudAgeN = shroudAgeN + 1
            end
        end
        -- HOLE FACTORY FIX (v0.9.8): box corners (BOX=16) reach past sight
        -- discs, so written cells there were never trail members. If the
        -- engine later re-explored one (reblack) while out of box range, it
        -- became a permanent open hole nobody revisited. Written cells join
        -- the trail: the rolling pass re-checks (and re-blackens) them.
        if not trailHas[key] then
            trailHas[key] = true
            trailKeys[#trailKeys + 1] = key
        end
        if fast then fastSweep = (fastSweep or 0) + 1 end
        return "reshrouded"
    end
    streakV[key] = 1  -- write failed: retry next sweep, not stuck at 2
    streakAt[key] = sweeps
    return "fail"
end

local function trailAdd(pts, n)
    local added = 0
    for i = 1, n do
        local p = pts[i]
        local r = math.floor(p.sight)
        local r2 = r * r
        for dy = -r, r do
            local y = p.y + dy
            if y >= 0 and y <= 511 then
                for dx = -r, r do
                    if dx * dx + dy * dy <= r2 then
                        local x = p.x + dx
                        if x >= 0 and x <= 511 then
                            local key = y * 512 + x
                            -- sight-history refresh: every sight cell, every
                            -- sweep (not just new trail entries). lastSeenAge
                            -- feeds the shroud logger (never window-pruned).
                            -- firstSeen starts a new watch era after a long
                            -- gap (capped), otherwise the span persists.
                            if trailSeen[key] == nil
                               or sweeps - trailSeen[key] > HISTORY_SWEEPS then
                                firstSeen[key] = sweeps
                            end
                            trailSeen[key] = sweeps
                            lastSeenAge[key] = sweeps
                            if not trailHas[key] then
                                trailHas[key] = true
                                trailKeys[#trailKeys + 1] = key
                                added = added + 1
                            end
                        end
                    end
                end
            end
        end
        if added >= TRAIL_ADD_MAX then break end
    end
end

local function sweep(frame)
    local pts, n = playerObjects()
    if not pts then return end
    -- v0.9.1: pad the protected radius. Buildings never move, and the engine
    -- reveals for them only once (at placement), so any cell the mod blackens
    -- inside their real vision (foundation size, disc shape differences)
    -- never comes back. Objects standing still for STILL_SWEEPS get STATIC_PAD.
    do
        local newStill = {}
        for i = 1, n do
            local k = pts[i].y * 512 + pts[i].x
            local c = (stillPrev[k] or 0) + 1
            newStill[k] = c
            pts[i].baseSight = pts[i].sight
            pts[i].sight = pts[i].sight + ((c >= STILL_SWEEPS) and STATIC_PAD or MOVE_PAD)
        end
        stillPrev = newStill
    end
    -- motion history by proximity matching (no stable IDs): nearest previous
    -- position within 4 cells; unmatched objects start fresh (conservative:
    -- no fast cone for unknowns). Each entry carries 3 back-positions, so
    -- NET progress over PROGRESS_WIN=4 sweeps is measurable with sub-cell
    -- resolution (v0.9.13: per-sweep integer run quantized slow walkers to
    -- 1,0,1,0 and the cone cycled = waves).
    do
        for i = 1, n do
            local best, bd2 = nil, 16
            for j = 1, #prevPts do
                local dx = pts[i].x - prevPts[j].x
                local dy = pts[i].y - prevPts[j].y
                local d2 = dx * dx + dy * dy
                if d2 < bd2 then best, bd2 = j, d2 end
            end
            if best ~= nil then
                local pv = prevPts[best]
                pts[i].h1x, pts[i].h1y = pv.x, pv.y
                pts[i].h2x, pts[i].h2y = pv.h1x, pv.h1y
                pts[i].h3x, pts[i].h3y = pv.h2x, pv.h2y
            else
                pts[i].h1x, pts[i].h1y = pts[i].x, pts[i].y
                pts[i].h2x, pts[i].h2y = pts[i].x, pts[i].y
                pts[i].h3x, pts[i].h3y = pts[i].x, pts[i].y
            end
            -- 4-sweep net displacement (travel vs milling signal).
            local pdx = pts[i].x - (pts[i].h3x or pts[i].x)
            local pdy = pts[i].y - (pts[i].h3y or pts[i].y)
            pts[i].pdx, pts[i].pdy = pdx, pdy
            pts[i].prog2 = pdx * pdx + pdy * pdy
        end
        local keep = {}
        for i = 1, n do
            keep[i] = { x = pts[i].x, y = pts[i].y,
                        h1x = pts[i].x, h1y = pts[i].y,
                        h2x = pts[i].h1x, h2y = pts[i].h1y,
                        h3x = pts[i].h2x, h3y = pts[i].h2y }
        end
        prevPts = keep
    end
    local cx, cy = centroid(pts, n)
    if not baseX then baseX, baseY = cx, cy end

    local did, skip, prot, bad, hyst = 0, 0, 0, 0, 0
    scPosObserved = 0
    reblackSweep = 0
    blackWatchSweep = 0
    blackWatchSample = {}
    fastSweep = 0
    hystSweep = 0
    -- Deterministic object order (sort by position) so the stride-sampled
    -- subset is stable across sweeps instead of staggering with array order.
    table.sort(pts, function(a, b)
        if a.x ~= b.x then return a.x < b.x end
        return a.y < b.y
    end)
    -- Per-object boxes, not one centroid box: with a split army (base at one
    -- corner, scout at another) the centroid sits in empty middle ground and
    -- a single BOX around it covers neither. Dedupe via `seen` so overlap
    -- costs one decision, and stop BOTH loops at the write cap.
    -- Read bound: at most SCAN_OBJECTS boxes per sweep (stride-sampled when
    -- the army is bigger), so worst case is 24 * 33 * 33 GetFogState calls.
    -- Each box reaches past that object's sight (sight+2) so the reshroud
    -- frontier always extends beyond live vision.
    trailAdd(pts, n)
    local seen = {}
    local stopped = false
    local step = math.max(1, math.ceil(n / SCAN_OBJECTS))
    for i = 1, n, step do
        if stopped then break end
        local px, py = pts[i].x, pts[i].y
        local half = math.max(BOX, (pts[i].sight or PROTECT) + 2)
        local x0 = math.max(0, px - half)
        local y0 = math.max(0, py - half)
        local x1 = math.min(511, px + half)
        local y1 = math.min(511, py + half)
        local boxDid = 0
        local boxDone = false
        for y = y0, y1 do
            if stopped or boxDone then break end
            for x = x0, x1 do
                if did >= MAX_PER_SWEEP then stopped = true break end
                if boxDid >= BOX_WRITE_BUDGET then boxDone = true break end
                local key = y * 512 + x
                if not seen[key] then
                    seen[key] = true
                    local r = decideCell(x, y, pts, n)
                    if r == "reshrouded" then
                        did = did + 1
                        boxDid = boxDid + 1
                    elseif r == "watched" then skip = skip + 1
                    elseif r == "protected" then prot = prot + 1
                    elseif r == "hyst" then hyst = (hyst or 0) + 1
                    elseif r == "fail" then bad = bad + 1 end
                end
            end
        end
    end

    -- v0.9.1: rolling pass over the trail (cells our units have seen at some
    -- point). Cells already black are dropped from the trail; everything else
    -- goes through the same hysteresis as the box cells above.
    do
        local cnt = #trailKeys
        local budget = math.min(TRAIL_PER_SWEEP, cnt)
        local i = trailCursor
        local trailDid = 0
        for _ = 1, budget do
            if cnt == 0 or did >= MAX_PER_SWEEP then break end
            -- paced writes: budget spent -> park the cursor, resume next sweep
            -- (unvisited cells keep their streaks: prune-exempt, so no reset).
            if trailDid >= TRAIL_WRITE_BUDGET then break end
            if i > cnt then i = 1 end
            local key = trailKeys[i]
            local x = key % 512
            local y = math.floor(key / 512)
            local r = "seen"
            if not seen[key] then
                seen[key] = true
                r = decideCell(x, y, pts, n)
            end
            if r == "skip" then
                trailHas[key] = nil
                trailKeys[i] = trailKeys[cnt]
                trailKeys[cnt] = nil
                cnt = cnt - 1
            else
                if r == "reshrouded" then
                    did = did + 1
                    trailDid = trailDid + 1
                elseif r == "watched" then skip = skip + 1
                elseif r == "protected" then prot = prot + 1
                elseif r == "hyst" then hyst = hyst + 1
                elseif r == "fail" then bad = bad + 1 end
                i = i + 1
            end
        end
        trailCursor = i
    end

    -- orphan scan (v0.9.9 diagnostic): coarse grid (every 16th cell) probes
    -- for OPEN cells under no management: not in trail, no streak building,
    -- outside every sight disc. Reads are cheap (~1k GetFogState); the inner
    -- object loop runs only for unmanaged-open cells (rare).
    do
        orphanSweep = 0
        orphanSample = {}
        for gy = 0, 511, 16 do
            for gx = 0, 511, 16 do
                local key = gy * 512 + gx
                if not trailHas[key] and not clearedMemory[key]
                   and streakV[key] == nil then
                    local s = World.GetFogState(gx, gy)
                    if s and not s.shrouded then
                        local outside = true
                        for i = 1, n do
                            local dx = gx - pts[i].x
                            local dy = gy - pts[i].y
                            local r = pts[i].sight or PROTECT
                            if dx * dx + dy * dy <= r * r then
                                outside = false
                                break
                            end
                        end
                        if outside then
                            orphanSweep = orphanSweep + 1
                            if #orphanSample < 8 then
                                orphanSample[#orphanSample + 1] = gx .. "," .. gy
                            end
                        end
                    end
                end
            end
        end
    end

    -- Westwood recipe (Encroach_Shadow ends with a full tactical redraw):
    -- flush on writes, plus a periodic mop-up (did-independent): with did=0
    -- for hundreds of sweeps any stale pixel would otherwise survive until
    -- scroll. One full-viewport dirty per LOG_EVERY sweeps bounds staleness
    -- to ~3s. Nil-safe: pre-flush DLLs simply keep stale pixels.
    if (did > 0 or sweeps % LOG_EVERY == 0) and World.FlushShroudRedraw then
        pcall(World.FlushShroudRedraw)
    end

    -- shroud-logger burst stats (window).
    if did > 0 then shroudActiveSw = shroudActiveSw + 1 end
    if did > shroudMaxBurst then shroudMaxBurst = did end

    sweeps   = sweeps + 1
    total    = total + did
    skipped  = skipped + skip
    protectedN = protectedN + prot
    failures = failures + bad
    scPosTotal = scPosTotal + scPosObserved
    reblackTotal = reblackTotal + (reblackSweep or 0)
    hystTotal = hystTotal + (hystSweep or 0)

    -- periodic prune: streak entries older than 3 sweeps that are not backing
    -- a live mark are dead weight (cells that left the boxes). Marked cells
    -- are exempt via clearedMemory; trail cells are exempt via trailHas (their
    -- revisits come at irregular gaps - pruning them caused the v0.9.1
    -- reset-checkerboard the far fast-path was papering over).
    if sweeps % LOG_EVERY == 0 then
        for k, at in pairs(streakAt) do
            if sweeps - at > 3 and not clearedMemory[k] and not trailHas[k] then
                streakAt[k] = nil
                streakV[k] = nil
            end
        end
        -- sight-history prune: entries older than the guard window can never
        -- protect again; drop them (the trail itself keeps re-checking).
        for k, seen in pairs(trailSeen) do
            if sweeps - seen > HISTORY_SWEEPS then
                trailSeen[k] = nil
            end
        end
        -- firstSeen follows trail membership (left the trail = era over).
        for k, _ in pairs(firstSeen) do
            if not trailHas[k] then
                firstSeen[k] = nil
            end
        end
        -- lastSeenAge lazy prune (stale only): 600 sweeps ~ several minutes.
        for k, seen in pairs(lastSeenAge) do
            if sweeps - seen > 600 then
                lastSeenAge[k] = nil
            end
        end
        -- shroud logger window report: volumes as rate + mean abandonment
        -- age + burst shape. Seconds: frames/15; sweeps: x5 frames.
        do
            local dt = frame - (shroudPrevFrame or frame)
            local dTotal = total - (shroudPrevTotal or 0)
            local rate = dt > 0 and (dTotal / (dt / 15)) or 0
            local meanAge = (shroudAgeN or 0) > 0
                and (shroudAgeSum / shroudAgeN) or 0
            print(string.format(
                "[DFOW] SHROUD f=%d window=%df total=%d +%d cells rate=%.1f/s meanAge=%.1fsw (%.1fs) maxBurst=%d activeSw=%d",
                frame, dt, total, dTotal, rate, meanAge, meanAge * 5 / 15,
                shroudMaxBurst or 0, shroudActiveSw or 0))
            shroudPrevTotal, shroudPrevFrame = total, frame
            shroudAgeSum, shroudAgeN = 0, 0
            shroudMaxBurst, shroudActiveSw = 0, 0
        end
        -- v0.9.11 trail shuffle: insertion order is row-major (disc/box
        -- row loops), so contiguous cursor slices under a binding budget
        -- painted parallel dotted ROWS (live 2026-09-28: banded speckle
        -- after v0.9.10 ballooned the trail into slicing). Fisher-Yates
        -- every LOG_EVERY sweeps makes each slice a uniform sample: same
        -- coverage rate, no spatial bands. Unseeded PRNG = deterministic
        -- (no os.time per MP-determinism rule); one fixed permutation still
        -- breaks row contiguity. Cursor resets (old index meaningless).
        if #trailKeys > 1 then
            for k = #trailKeys, 2, -1 do
                local j = math.random(k)
                trailKeys[k], trailKeys[j] = trailKeys[j], trailKeys[k]
            end
            trailCursor = 1
        end
    end

    if sweeps % LOG_EVERY == 0 or bad > 0 then
        print("[DFOW] trail=" .. #trailKeys)
        print(string.format(
            "[DFOW] SWEEP#%d f=%d base=(%d,%d) reshrouded=%d watched=%d near_object=%d hyst=%d fail=%d total=%d scPos=%d scPosTotal=%d reblack=%d reblackTotal=%d cleared=%d blackWatch=%d fast=%d orphan=%d",
            sweeps, frame, cx, cy, did, skip, prot, hyst, bad, total, scPosObserved, scPosTotal,
            (reblackSweep or 0), reblackTotal, clearedCount,
            (blackWatchSweep or 0), (fastSweep or 0), (orphanSweep or 0)))
        if (blackWatchSweep or 0) > 0 then
            print("[DFOW] blackWatch@ " .. table.concat(blackWatchSample, " "))
        end
        if (orphanSweep or 0) > 0 then
            print("[DFOW] orphan@ " .. table.concat(orphanSample, " "))
        end
        print(string.format(
            "[DFOW] census objs=%d mine=%d posok=%d sightlive=%d posfail=%s",
            censusTotal or 0, censusMine or 0, censusPosOk or 0,
            censusSightLive or 0, table.concat(censusPosFail or {}, ",")))
    end
end

function Mod.Update(frame)
    if lastFrame >= 0 and frame <= lastFrame then return end
    lastFrame = frame
    ensureInit()

    if not hasBindings() then
        if frame % HEARTBEAT == 0 or not noBindReported then
            noBindReported = true
            print("[DFOW] NOBIND World.SetCellShrouded/GetFogState missing - " ..
                  "is LuaAPI.dll the 2026-09-26 build?")
        end
        return
    end
    noBindReported = false

    if frame % HEARTBEAT == 0 then
        print(string.format(
            "[DFOW] HEARTBEAT f=%d sweeps=%d reshrouded=%d watched=%d near_object=%d hyst=%d fail=%d scPosTotal=%d reblackTotal=%d cleared=%d",
            frame, sweeps, total, skipped, protectedN, hystTotal, failures, scPosTotal, reblackTotal, clearedCount))
    end

    if frame % SWEEP_INTERVAL ~= 0 then return end
    sweep(frame)
end

function Mod.GetStats()
    return { sweeps = sweeps, reshrouded = total, watched = skipped,
             near_object = protectedN, failures = failures,
             scPosTotal = scPosTotal, reblackTotal = reblackTotal,
             hystTotal = hystTotal, cleared = clearedCount }
end
function Mod.OnScenarioStart()
    lastFrame = -1
    sweeps, total, skipped, protectedN, failures = 0, 0, 0, 0, 0
    scPosObserved, scPosTotal = 0, 0
    clearedMemory, clearedCount = {}, 0
    reblackSweep, reblackTotal = 0, 0
    streakV, streakAt = {}, {}
    hystSweep, hystTotal = 0, 0
    trailKeys, trailHas, trailCursor = {}, {}, 1
    trailSeen = {}
    firstSeen = {}
    prevPts = {}
    censusTotal, censusMine, censusPosOk, censusSightLive = 0, 0, 0, 0
    censusPosFail = {}
    lastSeenAge = {}
    shroudAgeSum, shroudAgeN = 0, 0
    shroudMaxBurst, shroudActiveSw = 0, 0
    shroudPrevTotal, shroudPrevFrame = 0, nil
    blackWatchSweep = 0
    blackWatchSample = {}
    stillPrev = {}
    baseX, baseY = nil, nil
end

function OnScenarioStart()
    if Mod.OnScenarioStart then Mod.OnScenarioStart() end
end

return Mod