local MultiTurretMod = {}

local SHIP_TYPE = "DRED"
local SCAN_RADIUS = 30     -- радиус подбора целей для раздельного огня
local LEASH_RADIUS = 20    -- максимум ухода от точки, где был отдан приказ атаки
local TICK_EVERY = 10

local anchors = {}         -- shipId -> {x, y} точка входа в режим атаки

local function printMsg(text)
    local f = (Engine and Engine.PrintMessage) or game_PrintMessage
    if f then f(text, 1) end
end

local function equipShip(ship)
    ship:AddSubTurret(2, -250, 0, 30, 10, 60)
    ship:AddSubTurret(3,    0, 0, 40, 10, 60)
    ship:AddSubTurret(4,  250, 0, 30, 10, 60)
    printMsg("[BATTLESHIP] 3 sub-turrets installed on DRED")
end

function MultiTurretMod.OnScenarioStart()
    printMsg("[BATTLESHIP] Multi-Turret Combat System Active!")
end

function MultiTurretMod.Update(frame)
    if frame % TICK_EVERY ~= 0 then return end

    local all = World.GetAllUnits()
    for _, ship in ipairs(all) do
        local id = ship:GetId()
        if ship:IsAlive() and ship:GetTypeName() == SHIP_TYPE then
            if ship:GetSubTurretCount() == 0 then
                equipShip(ship)
            end

            if ship:IsAttacking() then
                -- Opt-in: усиливаем только пока корабль реально атакует
                -- (приказ игрока ЛКМ или нативный авто-огонь Guard).
                local pos = ship:GetPosition()
                if not anchors[id] then
                    anchors[id] = { x = pos.x, y = pos.y }
                end

                -- Ограничение движения: не даём гоняться через всю карту.
                local a = anchors[id]
                local dx, dy = pos.x - a.x, pos.y - a.y
                if dx * dx + dy * dy > LEASH_RADIUS * LEASH_RADIUS then
                    ship:MoveTo(a.x, a.y)   -- возврат; заодно снимает погоню
                else
                    local targets = World.GetUnitsInRadius(pos.x, pos.y, SCAN_RADIUS)
                    if ship:SetSplitTargets(targets) then
                        ship:FireSplitSalvo()
                    end
                end
            else
                -- Guard / Move / Stop: мод молчит, игрок полностью управляет.
                anchors[id] = nil
            end
        else
            anchors[id] = nil
        end
    end
end

return MultiTurretMod