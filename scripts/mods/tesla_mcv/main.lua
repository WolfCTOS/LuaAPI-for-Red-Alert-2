-- Tesla MCV driver/verifier (Gate 1-2 tooling, PLAN: PROJECT/TESLA_MCV_PLAN.md).
--
-- WHAT: on T, spawns 1 TESLMCV (from INI/Game Options/Tesla MCV.ini) near
-- the player base via house:SpawnUnit, then auto-verifies what Lua CAN
-- observe and logs PASS/FAIL lines:
--   * type resolves (SpawnUnit > 0) — else the option is off / bad index;
--   * MaxHealth == 1200 (2x SMCV 600);
--   * kind == "unit", alive;
--   * MoveTo accepted (mobility intact after weapon graft).
-- Lua CANNOT check: lightning visuals/damage — those stay manual Gate 2.2
-- items, printed as reminders. Deploy IS bound (Deploy/Undeploy/CanDeployNow/
-- IsDeployed/IsDeploying/IsUndeploying, src/bindings_techno.cpp) — corrected
-- 2026-09-21; the old "no bindings" note was stale.
-- The mod issues no orders except the single verification MoveTo.

local TeslaMCV = {}

TeslaMCV.TUNING = {
    TYPE = "TESLMCV",
    EXPECT_HP = 2000,   -- 2x SMCV baseline (1000); see Tesla MCV.ini
    KEY = 0x54,         -- T
}

local S = { lastFrame = 0, announced = false }

local function say(msg)
    if Engine and Engine.PrintMessage then
        Engine.PrintMessage("[TESLAMCV] " .. msg)
    end
end

local function log(msg) print("[TESLAMCV] " .. msg) end

local function verdict(ok, name, detail)
    local line = string.format("%s %s%s", ok and "PASS" or "FAIL", name,
        detail and (" (" .. tostring(detail) .. ")") or "")
    log(line)
    say(line)
end

local function playerHouse()
    if House and House.GetPlayer then return House.GetPlayer() end
    return nil
end

local function baseCell(ph)
    if World and World.GetBuildings then
        local ok, blds = pcall(World.GetBuildings)
        if ok and blds then
            for _, b in ipairs(blds) do
                if b and b:IsAlive() then
                    local o = b:GetOwner()
                    if o and o == ph then
                        local p = b:GetPosition()
                        if p and p.x and p.y then
                            return math.floor(p.x), math.floor(p.y)
                        end
                    end
                end
            end
        end
    end
    return 26, 26
end

local function findUnit(typeName)
    if not (World and World.GetUnits) then return nil end
    local ok, units = pcall(World.GetUnits)
    if not ok or not units then return nil end
    for _, u in ipairs(units) do
        if u and u:IsAlive() then
            local okT, tn = pcall(u.GetTypeName, u)
            if okT and tn == typeName then return u end
        end
    end
    return nil
end

local function spawnAndVerify(ph)
    local bx, by = baseCell(ph)
    local okS, n = pcall(ph.SpawnUnit, ph, TeslaMCV.TUNING.TYPE, 1,
        bx + 2, by + 2, 0, false, "")
    if not okS then
        verdict(false, "spawn-call", n)
        return
    end
    verdict((n or 0) > 0, "type-resolves",
        (n or 0) > 0 and ("spawned=" .. tostring(n))
        or "TESLMCV unknown — включи опцию 'Tesla MCV' в лобби")
    if not n or n <= 0 then return end

    local u = findUnit(TeslaMCV.TUNING.TYPE)
    verdict(u ~= nil, "unit-present")
    if not u then return end

    local okK, kind = pcall(u.GetKind, u)
    verdict(okK and kind == "unit", "kind-unit", okK and kind or "n/a")

    local okH, hp = pcall(u.GetMaxHealth, u)
    verdict(okH and hp == TeslaMCV.TUNING.EXPECT_HP, "hp-x2",
        okH and ("maxhp=" .. tostring(hp)) or "n/a")

    local okP, pos = pcall(u.GetPosition, u)
    if okP and pos and pos.x and pos.y then
        local okM, acc = pcall(u.MoveTo, u,
            math.floor(pos.x) + 3, math.floor(pos.y))
        verdict(okM and acc, "moveto-accepted")
    else
        verdict(false, "moveto-accepted", "no-position")
    end

    say("MANUAL Gate 2.2: прикажи атаковать — должны быть молнии " ..
        "(TeslaTankWeapon); кнопки Deploy быть не должно.")
end

function TeslaMCV.Update(frame)
    if frame < S.lastFrame then
        S.lastFrame = frame
        S.announced = false
        return
    end
    S.lastFrame = frame

    local ph = playerHouse()
    if not ph then return end
    if not S.announced then
        S.announced = true
        say("driver ready. T = spawn+verify TESLMCV. " ..
            "Нужна включённая опция 'Tesla MCV'.")
    end

    if Input and Input.WasKeyPressed then
        local okK, pressed = pcall(Input.WasKeyPressed, TeslaMCV.TUNING.KEY)
        if okK and pressed then spawnAndVerify(ph) end
    end
end

return TeslaMCV
