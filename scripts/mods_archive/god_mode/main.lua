local GodMode = {}

-- Перехват и полная отмена регистрации урона по игроку
function GodMode.OnPreDamage(attacker, target, damage, dmg_type, frame, subc)
    local player = game_GetLocalPlayer and game_GetLocalPlayer() or (game.GetLocalPlayer and game.GetLocalPlayer())
    
    -- Если цель принадлежит локальному игроку — обнуляем урон полностью
    if target and player and target.GetHouse and target:GetHouse() == player then
        return 0 -- 0 урона на входе (полный иммунитет)
    end
    
    return nil -- По ботам урон проходит без изменений
end

-- Периодическое начисление денег каждые 60 кадров (~1-2 сек)
function GodMode.Update(frame)
    if frame % 60 == 0 then
        local player = game_GetLocalPlayer and game_GetLocalPlayer() or (game.GetLocalPlayer and game.GetLocalPlayer())
        if player and house_AddCredits then
            house_AddCredits(player, 25000)
        end
    end
end

function GodMode.OnRegister()
    game_RegisterEvent("OnPreDamage", GodMode.OnPreDamage)
end

return GodMode