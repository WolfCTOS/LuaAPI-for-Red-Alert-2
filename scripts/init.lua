print("[LuaAPI] Universal ModLoader Online!")

-- Каталог самого скрипта (DLL-модуля), а не рабочий каталог процесса.
-- Лаунчер (injector.exe) пишет список включённых модов в АБСОЛЮТНЫЙ путь
-- <каталог-модуля>\scripts\active_mods.txt. Чтение из CWD-относительного пути
-- ломается, когда игру запускает внешний клиент (CnCNet/Syringe) с другим
-- рабочим каталогом: тогда лаунчер и ModLoader смотрят на ДВА РАЗНЫХ файла и
-- мод, включённый в лаунчере, не загружается.
local function moduleScriptDir()
    local src = debug.getinfo(1, "S").source
    if src and src:sub(1, 1) == "@" then src = src:sub(2) end
    src = src:gsub("\\", "/")
    return src:match("^(.*)/[^/]*$") or "."
end

local MODULE_DIR = moduleScriptDir()

-- Read enabled mod IDs from active_mods.txt (one per line, '#' comments).
-- Файл берётся из каталога модуля (где лежит init.lua), т.е. ровно тот же файл,
-- в который пишет лаунчер.
local function loadActiveModList()
    local active = {}
    local f = io.open(MODULE_DIR .. "/active_mods.txt", "r")
    if not f then
        f = io.open("scripts/active_mods.txt", "r")
    end
    if not f then
        f = io.open("active_mods.txt", "r")
    end

    if f then
        for line in f:lines() do
            local clean = line:match("^%s*(.-)%s*$")
            if clean and clean ~= "" and not clean:match("^#") then
                table.insert(active, clean)
            end
        end
        f:close()
    else
        -- Fallback defaults if file doesn't exist
        active = { "delayed_explosion", "smart_ai" }
    end
    return active
end

local ACTIVE_MODS = loadActiveModList()
local loadedMods = {}

-- Per-mod timing state: total_ms, max_ms, call_count indexed by mod name.
local modTiming = {}
local lastStatsReport = os.clock()

-- [[ДЕТЕРМИНИСТИЧНОЕ СИДИРОВАНИЕ RNG]]
-- Критически важно для CnCNet мультиплеера: использование os.clock() или os.time()
-- вызывает Out-of-Sync (OOS) рассинхронизацию между клиентами.
-- 
-- Базовый сид фиксирован и синхронизирован на всех клиентах.
-- Если модам требуется пересидирование во время игры, должно использоваться
-- только следящее за текущим кадром: math.randomseed(current_frame + 12345)
math.randomseed(12345)

for _, modName in ipairs(ACTIVE_MODS) do
    local ok, mod = pcall(require, "mods." .. modName .. ".main")
    if ok and mod then
        table.insert(loadedMods, mod)
        modTiming[modName] = modTiming[modName] or { total_ms = 0.0, max_ms = 0.0, calls = 0 }
        print(string.format("[LuaAPI] [+] Mod active: '%s'", modName))
    else
        print(string.format("[LuaAPI] [-] Failed to load mod: '%s' (%s)", modName, tostring(mod)))
    end
end

local welcomed = false

-- [[Событийные шины]]
-- Моды могут подписаться на эти события в main.lua:
--   function OnScenarioStart()  -- вызывается 1 раз при загрузке карты
--   function OnUnitDestroyed(victim, killer) -- вызывается при уничтожении юнита
-- глобальные функции автоматически дискpatchся из C++ движка.

function OnScenarioStart()
    -- Базовый пустой обработчик. Переопределите в main.lua своего мода.
end

function OnUnitDestroyed(victim, killer)
    -- Базовый пустой обработчик. Переопределите в main.lua своего мода.
    -- victim = TechnoClass pointer (или nil), killer = TechnoClass pointer (или nil)
end

function OnTick(frame)
    if not welcomed then
        local player = House.GetPlayer()
        if player then
            Engine.PrintMessage(string.format("Commander: %s | Active Mods: %d loaded",
                player:GetName(), #loadedMods))
            welcomed = true
        end
    end

    -- Report per-mod stats every 5 seconds
    local now = os.clock()
    if now - lastStatsReport >= 5.0 then
        lastStatsReport = now
        for name, t in pairs(modTiming) do
            local avg_ms = t.calls > 0 and (t.total_ms / t.calls) or 0.0
            print(string.format("[LuaAPI] Mod timing [%s]: Avg %.2f ms | Max %.2f ms | Calls %d",
                name, avg_ms, t.max_ms, t.calls))
        end
        -- Reset counters after reporting
        for name in pairs(modTiming) do
            modTiming[name] = { total_ms = 0.0, max_ms = 0.0, calls = 0 }
        end
    end

    -- Wrap each mod.Update in wall-clock timing
    for idx, mod in ipairs(loadedMods) do
        if mod and type(mod.Update) == "function" then
            local modName = ACTIVE_MODS[idx]
            local start = os.clock()
            local ok, err = pcall(mod.Update, frame)
            local elapsed_ms = (os.clock() - start) * 1000.0
            -- Accumulate timing for this mod
            if modTiming[modName] then
                modTiming[modName].total_ms = modTiming[modName].total_ms + elapsed_ms
                modTiming[modName].calls = modTiming[modName].calls + 1
                if elapsed_ms > modTiming[modName].max_ms then
                    modTiming[modName].max_ms = elapsed_ms
                end
            end
        end
    end
end