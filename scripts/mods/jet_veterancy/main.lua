-- Jet Veterancy: AoE bonus projectile on shot, scaled by veterancy.
--
-- Detect a shot via the ammo counter dropping, then fire a bonus projectile that
-- FLIES like a fighter missile (fast, homing — flight from Maverick) but DETONATES
-- like a Kirov bomb (AoE warhead from BlimpBomb). Rookies stay vanilla; veterans
-- get a tuned hit; elites additionally emit an EMP burst that freezes nearby
-- enemies for a few seconds (MVP: immobilize-only, targets stay killable).
--
-- EMP (MVP): immobilizes enemy units via Stop for ~150 frames.
-- Targets remain killable. No visual tint yet.
-- Planned (13.4): damage-path hook (M4 restoration) to allow the
-- Iron Curtain tint WITHOUT invulnerability.
--
-- Featured APIs:
--   unit:GetAmmo / unit:GetVeterancy / unit:GetTarget / unit:GetTypeName
--   unit:GetId / unit:IsAlive / unit:FireProjectile(weapon,target,dmg,flight) / unit:Stop
--   Engine.WeaponExists / World.GetUnits / World.GetUnitsInRadius
--   House.GetPlayer / house:IsAlliedWith / unit:GetOwner / house:GetName

local MOD = {}

local WATCHED_TYPES = { "HORNET", "ORCA", "BEAG" }   -- BEAG = Korean Black Eagle
local JET_TYPES = {}
for _, t in ipairs(WATCHED_TYPES) do JET_TYPES[t] = true end

-- AoE weapon (damage + warhead); the first existing id wins.
local AOE_CANDIDATES = { "BlimpBomb", "BlimpBombE" }
-- Flight weapon (fast, homing projectile) reused as the missile body.
local FLIGHT_WEAPON = "Maverick"

local VETERAN_DAMAGE  = 150             -- noticeable, not lethal vs Grizzly squad; ~2-3 salvos
local ELITE_DAMAGE    = 200             -- ~2 salvos on a Grizzly squad

local EMP_RADIUS = 4                    -- cells around the target
local EMP_FRAMES = 150                  -- stun duration (MVP: immobilize only)
local FRAMES_PER_CELL = 3               -- flight-time approximation per cell (tune by test)

local TEST_FORCE_VET = true             -- treat fresh jets as veteran (no grind needed); false in release

local NEUTRAL_HOUSES = { Neutral = true, Civilian = true, Special = true }
local CIVIL_VEHICLES = { CAR = true, PCV = true, BUS = true, TRUCK = true }

local startupLogged      = false
local AOE_WEAPON         = nil          -- resolved once at startup (frame ~300)
local weaponTried        = false        -- resolution attempted (even if no match)
local weaponFailedLogged = {}           -- unitId -> true  (one-off failure log per jet)
local lastAmmo           = {}           -- unitId -> last observed ammo
local lastTarget         = {}           -- unitId -> last known target (engine clears it on shot)
local stunned            = {}           -- unitId -> expiryFrame (EMP)
local pendingEmps        = {}           -- {frame, target, x, y} — burst on impact
local freezeLogged       = false        -- one-time freeze mechanism log

local function ownedBy(player, unit)
    local owner = unit:GetOwner()
    return owner ~= nil and (owner == player or owner:IsAlliedWith(player))
end

local function isEnemy(player, unit)
    if not unit or not unit:IsAlive() then return false end
    local owner = unit:GetOwner()
    if not owner then return false end
    if NEUTRAL_HOUSES[owner:GetName()] then return false end
    if CIVIL_VEHICLES[unit:GetTypeName()] then return false end
    return owner ~= player and not owner:IsAlliedWith(player)
end

local function msg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text) end
end

-- Resolve the AoE weapon id once (first match), log the outcome + flight.
local function resolveWeapon()
    for _, id in ipairs(AOE_CANDIDATES) do
        if Engine.WeaponExists and Engine.WeaponExists(id) then
            AOE_WEAPON = id
            msg(string.format("[JET] AoE weapon resolved: %s, flight: %s", id, FLIGHT_WEAPON))
            return
        end
    end
    msg("[JET] no AoE weapon found, mod disabled")
end

-- EMP burst: freeze vehicles around (x, y) for ~EMP_FRAMES.
-- hitPlayerSide=true  -> EMP the PLAYER's own vehicles (an enemy jet attacking the player).
-- hitPlayerSide=false -> EMP the player's ENEMIES (a player jet attacking the enemy).
-- Mechanism: the engine's NATIVE paralysis (ParalysisTimer, as used by giant
-- squids) via unit:Disable(duration) — freezes even under an AI order, stays
-- killable, auto-recovers. (Temporal-warp/Locomotor/hook rejected: temporal
-- makes targets untargetable/unkillable, the rest are risky.)
local function spawnEmp(player, x, y, frame, hitPlayerSide)
    local nearby = World.GetUnitsInRadius(x, y, EMP_RADIUS)
    local count = 0
    if nearby then
        for _, c in ipairs(nearby) do
            -- Only vehicles (no buildings in 13.3); pick the correct side.
            if c:GetKind() == "unit" then
                local sideOk = hitPlayerSide and ownedBy(player, c) or (not hitPlayerSide and isEnemy(player, c))
                if sideOk then
                    local cid = c.GetId and c:GetId() or 0
                    if not stunned[cid] then
                        stunned[cid] = frame + EMP_FRAMES
                        c:Disable(EMP_FRAMES)   -- native paralysis (freeze, killable, auto-restore)
                        count = count + 1
                    end
                end
            end
        end
    end
    if count > 0 then
        msg(string.format("[JET] EMP burst at (%d,%d), stunned %d units", x, y, count))
    end
end

-- Per-jet tick: detect ammo transitions, fire the bonus projectile, EMP on elite.
-- Watches ALL jets of the watched types. Friendly jets behave as before
-- (TEST_FORCE_VET). Enemy jets only act when genuinely elite — that is the
-- registered "disable" capability consumed by the smart_ai provider.
local function onJetTick(player, unit, frame)
    local typeName = unit:GetTypeName()
    if not JET_TYPES[typeName] then return end   -- only watched jet types
    if not unit:IsAlive() then return end
    local jetFriendly = ownedBy(player, unit)

    local id = unit.GetId and unit:GetId() or 0
    local ammo = unit:GetAmmo()
    local prev = lastAmmo[id]

    local target = unit:GetTarget()
    if target then lastTarget[id] = target end

    if prev ~= nil and ammo ~= prev then
        msg(string.format("[JET] %s ammo %s -> %s vet=%s target=%s",
            typeName, tostring(prev), tostring(ammo),
            tostring(unit:GetVeterancy()),
            target and "yes" or "nil"))

        if ammo < prev then   -- a shot consumed ammo
            local useTarget = target or lastTarget[id]
            if useTarget then
                local vet = unit:GetVeterancy()
                if jetFriendly and TEST_FORCE_VET and vet == "rookie" then
                    vet = "veteran"   -- TEST: friendly jets act as veteran
                end
                local isElite = (vet == "elite")

                -- Enemy jets only trigger the capability when truly elite.
                local eligible = jetFriendly or isElite
                if eligible then
                    local ok = false
                    if isElite then
                        ok = unit:FireProjectile(AOE_WEAPON, useTarget, ELITE_DAMAGE, FLIGHT_WEAPON)
                    elseif vet == "veteran" then
                        ok = unit:FireProjectile(AOE_WEAPON, useTarget, VETERAN_DAMAGE, FLIGHT_WEAPON)
                    end

                    if ok then
                        msg(string.format("[JET] %s %s fired -> custom projectile", typeName, vet))
                    elseif not weaponFailedLogged[id] then
                        weaponFailedLogged[id] = true
                        msg(string.format("[JET] FireProjectile failed for %s", AOE_WEAPON))
                    end

                    -- EMP only for elite — queued to land on impact (homing delay).
                    if isElite then
                        local jp = unit:GetPosition()
                        local tp = useTarget:GetPosition()
                        if jp and tp then
                            local tx, ty = math.floor(tp.x), math.floor(tp.y)
                            local dist = math.sqrt((tp.x - jp.x) ^ 2 + (tp.y - jp.y) ^ 2)
                            local delay = math.floor(dist * FRAMES_PER_CELL)
                            table.insert(pendingEmps, {
                                frame = frame + delay, target = useTarget,
                                x = tx, y = ty, hitPlayerSide = not jetFriendly,
                            })
                            msg(string.format("[JET] EMP queued, delay=%d frames", delay))
                        end
                    end
                end
            else
                msg(string.format("[JET] %s shot detected but no target", typeName))
            end
        end
    end

    lastAmmo[id] = ammo
end

-- Keep the stunned table tidy: drop expired/dead/missing entries. The actual
-- freeze is handled by the native paralysis timer (unit:Disable) — no per-tick
-- Stop() needed (it loses the race against the vanilla AI).
local function processStun(live, frame)
    for cid, expiry in pairs(stunned) do
        local unit = live[cid]
        if unit and unit:IsAlive() and frame < expiry then
            -- still frozen (paralysis timer running)
        else
            stunned[cid] = nil
        end
    end
end

-- Fire queued EMPs when their impact frame arrives. The burst follows the
-- (homing) target if it is still alive; otherwise it uses the saved coords.
local function processPendingEmps(player, frame)
    for i = #pendingEmps, 1, -1 do
        local e = pendingEmps[i]
        if frame >= e.frame then
            local x, y = e.x, e.y
            local t = e.target
            if t and t.IsAlive and t:IsAlive() then   -- check before any deref
                local p = t:GetPosition()
                if p then
                    x, y = math.floor(p.x), math.floor(p.y)
                end
            end
            spawnEmp(player, x, y, frame, e.hitPlayerSide)
            table.remove(pendingEmps, i)
        end
    end
end

function MOD.Update(frame)
    if not startupLogged then
        startupLogged = true
        msg(string.format("[JET] jet_veterancy active; watched types: %s",
            table.concat(WATCHED_TYPES, ", ")))
    end

    if not weaponTried and frame >= 300 then
        weaponTried = true
        resolveWeapon()
    end

    -- One-time freeze mechanism report (engine-level paralysis via unit:Disable).
    if not freezeLogged then
        freezeLogged = true
        msg("[JET] EMP freeze mechanism: engine paralysis (ParalysisTimer)")
    end

    local player = House.GetPlayer()

    -- Queued EMPs resolve EVERY frame so the burst lands on the impact frame,
    -- independent of the 5-frame jet loop.
    if player and AOE_WEAPON then
        processPendingEmps(player, frame)
    end

    if frame % 5 ~= 0 then return end
    if not player then return end
    if not AOE_WEAPON then return end   -- nothing resolved -> idle

    local units = World.GetUnits()
    if not units then return end

    local live = {}
    for _, u in ipairs(units) do live[u.GetId and u:GetId() or 0] = u end

    for _, unit in ipairs(units) do
        onJetTick(player, unit, frame)
    end

    processStun(live, frame)

    -- Drop missing/dead jets so the trackers do not grow.
    for id in pairs(lastAmmo) do
        if not live[id] then
            lastAmmo[id] = nil
            lastTarget[id] = nil
            weaponFailedLogged[id] = nil
        end
    end
end

-- Register the "disable" (EMP) capability for elite jets, consumed by the
-- smart_ai provider. It is the EMP described above: an EMP/stun AoE.
_G.CapabilityRegistry = _G.CapabilityRegistry or {}
table.insert(_G.CapabilityRegistry, {
    id = "jet_emp",
    effect = "disable",
    unitTypes = JET_TYPES,
    minVeterancy = "elite",
})

return MOD
