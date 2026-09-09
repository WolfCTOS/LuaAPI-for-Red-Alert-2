-- Squad Speed Sync
--
-- Горячая клавиша Ctrl+F включает режим, при котором выделенный отряд
-- мобильных техно (юниты И пехота через World.GetSelectedTechnos; здания и
-- авиация пропускаются) движется с одинаковой скоростью — скоростью самого
-- медленного юнита (якоря).
--
-- Реализация: НЕ MoveTo-дёрганье, а прямая штатная механика движка.
-- FootClass::SpeedMultiplier/SpeedPercentage (биндинг unit:SetSpeedPercent)
-- ставит долю полной скорости юнита. Быстрым машинам выставляется доля
--   pct = speed(якорь) / speed(юнит),
-- поэтому их эффективная скорость ровно равна скорости якоря.
--
-- Устойчивость:
--   * Скорость якоря ЗАЛОЧЕНА на момент активации (lockSpeed). Она НЕ
--     пересчитывается каждый кадр, поэтому если пехота (самая медленная)
--     выпадет из выделения, танки НЕ сорвутся обратно на полный ход.
--   * Однажды замедленный юнит остаётся замедленным до выхода из режима:
--     он пережatживается по id даже если его сняли с выделения (лёгкий
--     обход World.GetAllUnits несколько раз в секунду). Это убирает сценарий
--     «танки покидают отряд и быстрее добегают до точки».
--   * Сброс на исходную скорость только при выходе из режима / смене сценария.

local MOD = {}

---------------------------------------------------------------
-- Configuration
---------------------------------------------------------------

local CONFIG = {
    CTRL_VK = 0x11, -- VK_CONTROL
    KEY_VK  = 0x46, -- 'F'
    CTRL_WINDOW = 40, -- сколько кадров ждём "F" после нажатия Ctrl

    MIN_PERCENT = 0.05, -- нижний порог доли скорости (чтобы не ползти ползком)
    REFRESH_EVERY = 5,  -- как часто переобрезать замедленных (и разъездающихся) юнитов
}

local active = false
local ctrlArmed = false
local ctrlArmedFrame = -100000

-- id -> true  для юнитов, которым сейчас выставлена доля < 1.0.
local slowed = {}

-- id -> изначальный SpeedMultiplier (до clamp'а). Храним, чтобы при возврате
-- вернуть ветеранский/прочий бонус скорости, а не просто 1.0.
local saved = {}

-- Скорость якоря (TechnoTypeClass::Speed), ЗАЛОЧЕННАЯ при активации режима.
local lockSpeed = nil

local tick = 0

---------------------------------------------------------------
-- Utility (все вызовы движка под pcall — безопасная обёртка)
---------------------------------------------------------------

local function msg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text) end
end

local function safeCall(fn, ...)
    if not fn then return false end
    local ok, res = pcall(fn, ...)
    if not ok then return false end
    return res
end

local function alive(unit)
    if not unit then return false end
    return safeCall(unit.IsAlive, unit) == true
end

local function idOf(unit)
    if not unit then return 0 end
    return safeCall(unit.GetId, unit) or 0
end

local function baseSpeed(unit)
    if not unit then return 0 end
    return safeCall(unit.GetBaseSpeed, unit) or 0
end

local function speedFactor(unit)
    if not unit then return 1.0 end
    return safeCall(unit.GetSpeedFactor, unit) or 1.0
end

-- Самая медленная базовая скорость в отряде (или nil, если никто не подходит).
local function pickAnchorSpeed(squad)
    local best = nil
    for _, u in ipairs(squad) do
        if alive(u) then
            local bs = baseSpeed(u)
            if bs > 0 and (not best or bs < best) then
                best = bs
            end
        end
    end
    return best
end

---------------------------------------------------------------
-- Hotkey: Ctrl (edge) потом F (edge) в течение CTRL_WINDOW
---------------------------------------------------------------

local function pollHotkey(frame)
    local ctrlNow = safeCall(Input.WasKeyPressed, CONFIG.CTRL_VK) == true
    if ctrlNow then
        ctrlArmed = true
        ctrlArmedFrame = frame
    end

    if ctrlArmed and (frame - ctrlArmedFrame > CONFIG.CTRL_WINDOW) then
        ctrlArmed = false
    end

    local keyNow = safeCall(Input.WasKeyPressed, CONFIG.KEY_VK) == true
    if keyNow and ctrlArmed then
        ctrlArmed = false
        return true
    end
    return false
end

---------------------------------------------------------------
-- Сброс
---------------------------------------------------------------

local function resetById(id)
    local f = saved[id] or 1.0
    local units = safeCall(World.GetAllUnits) or {}
    for _, u in ipairs(units) do
        if idOf(u) == id and alive(u) then
            safeCall(u.SetSpeedPercent, u, f)
            break
        end
    end
    saved[id] = nil
end

local function resetAll()
    for id, _ in pairs(slowed) do
        resetById(id)
    end
    slowed = {}
    saved = {}
end

---------------------------------------------------------------
-- Прижать один юнит к темпу якоря
---------------------------------------------------------------

local function clampUnit(u, targetSpeed)
    local id = idOf(u)
    if not alive(u) then return end

    -- Изначальный множитель: щёлкаем один раз, до любых правок.
    if not saved[id] then
        saved[id] = speedFactor(u)
    end

    local bs = baseSpeed(u)
    local pct
    if bs > 0 and bs > targetSpeed then
        -- Быстрый юнит: доля = скорость якоря / его скорость.
        pct = targetSpeed / bs
        if pct < CONFIG.MIN_PERCENT then
            pct = CONFIG.MIN_PERCENT
        end
    else
        -- Якорь / не быстрее: вернуть исходную долю.
        pct = saved[id]
    end

    safeCall(u.SetSpeedPercent, u, pct)

    if pct < 1.0 then
        slowed[id] = true
    else
        slowed[id] = nil
    end
end

local function clampList(squad, targetSpeed)
    for _, u in ipairs(squad) do
        clampUnit(u, targetSpeed)
    end
end

---------------------------------------------------------------
-- Основной цикл
---------------------------------------------------------------

function MOD.Update(frame)
    tick = tick + 1

    if pollHotkey(frame) then
        if not active then
            active = true
            slowed = {}
            saved = {}
            lockSpeed = nil
            local squad = safeCall(World.GetSelectedTechnos)
            lockSpeed = pickAnchorSpeed(squad)
            if lockSpeed and squad and #squad >= 1 then
                clampList(squad, lockSpeed)
                msg("[squad_speed_sync] Синхронизация ВКЛ (Ctrl+F): скорость отряда = самый медленный юнит.")
            else
                lockSpeed = nil
                msg("[squad_speed_sync] Синхронизация ВКЛ (Ctrl+F): выделите отряд из техники/пехоты.")
            end
        else
            active = false
            lockSpeed = nil
            resetAll()
            msg("[squad_speed_sync] Синхронизация ВЫКЛ.")
        end
    end

    if not active then return end
    if not lockSpeed then return end

    -- Каждый кадр: держим темп у выделенных юнитов.
    local sel = safeCall(World.GetSelectedTechnos)
    if sel and #sel >= 1 then
        clampList(sel, lockSpeed)
    end

    -- Периодически переобрезаем тех, кого уже замедляли, но кто мог сняться с
    -- выделения или уйти по другому маршруту — чтобы они не «сорвались» вперёд.
    if (tick % CONFIG.REFRESH_EVERY == 0) and next(slowed) then
        local all = safeCall(World.GetAllUnits) or {}
        for _, u in ipairs(all) do
            if alive(u) then
                local id = idOf(u)
                if id ~= 0 and slowed[id] then
                    clampUnit(u, lockSpeed)
                end
            end
        end
    end
end

function MOD.OnScenarioStart()
    active = false
    slowed = {}
    saved = {}
    lockSpeed = nil
    ctrlArmed = false
end

return MOD
