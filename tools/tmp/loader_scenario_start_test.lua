-- Regression harness for M1 (loader shadows mod OnScenarioStart).
-- Simulates scripts/init.lua load order with stubbed I/O: two fake mods,
-- one declaring a file-scope global OnScenarioStart (the documented pattern),
-- then asserts the mod handler survives init (pre-fix init overwrote it with
-- its empty default, so C++ frame-1 dispatch called the empty function).
-- Also asserts OnTick still dispatches mod.Update.
--
-- Run from repo root: lua_check.exe tools/tmp/loader_scenario_start_test.lua
-- Sensitivity check: run against the pre-fix loader
--   (git show HEAD:scripts/init.lua > tools/tmp/init_prefix.lua ...)
--   must FAIL the survival assert. The harness loads whichever init path is
--   given as arg[1] (default: scripts/init.lua).

local INIT_PATH = (arg and arg[1]) or "scripts/init.lua"

local passed, failed = 0, 0
local function check(name, cond)
    if cond then
        passed = passed + 1
        print("PASS: " .. name)
    else
        failed = failed + 1
        print("FAIL: " .. name)
    end
end

-- The documented mod pattern: file-scope global handler.
local function probeScenarioStart()
    return "probe_a:scenario-started"
end

local updatesA, updatesB = 0, 0

-- Stub require: fake mods as require() would execute them at file scope.
local realRequire = require
local function fakeRequire(name)
    if name == "mods.probe_a.main" then
        _G.OnScenarioStart = probeScenarioStart -- file-scope global, like a mod
        return { Update = function(_) updatesA = updatesA + 1 end }
    elseif name == "mods.probe_b.main" then
        return { Update = function(_) updatesB = updatesB + 1 end }
    end
    return realRequire(name)
end
_G.require = fakeRequire

-- Stub active_mods.txt (any path): two fake mods.
local realIoOpen = io.open
function io.open(path, mode)
    if type(path) == "string" and path:find("active_mods", 1, true) then
        local lines = { "probe_a", "probe_b" }
        local i = 0
        return {
            lines = function()
                return function()
                    i = i + 1
                    return lines[i]
                end
            end,
            close = function() end,
        }
    end
    return realIoOpen(path, mode)
end

-- Engine/House surface touched by init's OnTick.
_G.House = { GetPlayer = function() return nil end }
_G.Engine = { PrintMessage = function() end }

local chunk, err = loadfile(INIT_PATH)
check("init loads without syntax error", chunk ~= nil)
if not chunk then
    print("load error: " .. tostring(err))
    print(string.format("RESULT: %d passed, %d failed", passed, failed))
    os.exit(1)
end
chunk()

check("mod file-scope OnScenarioStart survives init (M1)",
    _G.OnScenarioStart == probeScenarioStart)
check("surviving handler is callable and returns probe marker",
    _G.OnScenarioStart ~= nil and _G.OnScenarioStart() == "probe_a:scenario-started")
check("OnTick dispatcher exists", type(_G.OnTick) == "function")

_G.OnTick(30)
check("mod Update still dispatched after fix (probe_a)", updatesA == 1)
check("mod Update still dispatched after fix (probe_b)", updatesB == 1)

print(string.format("RESULT: %d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
