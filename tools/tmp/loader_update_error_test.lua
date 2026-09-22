-- Regression harness for E1 (per-mod Update errors were swallowed).
-- Drives the REAL scripts/init.lua with stubbed I/O: one fake mod whose
-- Update throws, one healthy mod. Asserts: error logged with mod name,
-- healthy mod still dispatched, loader continues past the failure,
-- identical repeat errors are deduped, new error text re-logs.
--
-- Run from repo root: lua_check.exe tools/tmp/loader_update_error_test.lua

local INIT_PATH = (arg and arg[1]) or "scripts/init.lua"

local passed, failed = 0, 0
local realPrint = print -- test verdicts bypass the captured print below
local function check(name, cond)
    if cond then
        passed = passed + 1
        realPrint("PASS: " .. name)
    else
        failed = failed + 1
        realPrint("FAIL: " .. name)
    end
end

local PRINTED = {}
function _G.print(...) -- capture loader log output (goes to LuaAPI.log live)
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
    PRINTED[#PRINTED + 1] = table.concat(parts, "\t")
end

local healthyUpdates = 0
local errText = "boom-phase-1"
local realRequire = require
function _G.require(name)
    if name == "mods.broken.main" then
        return { Update = function(_) error(errText, 0) end }
    elseif name == "mods.healthy.main" then
        return { Update = function(_) healthyUpdates = healthyUpdates + 1 end }
    end
    return realRequire(name)
end

local realIoOpen = io.open
function io.open(path, mode)
    if type(path) == "string" and path:find("active_mods", 1, true) then
        local lines = { "broken", "healthy" }
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

_G.House = { GetPlayer = function() return nil end }
_G.Engine = { PrintMessage = function() end }

local function errLines()
    local out = {}
    for _, l in ipairs(PRINTED) do
        if l:find("Update error", 1, true) then out[#out + 1] = l end
    end
    return out
end

local chunk, err = loadfile(INIT_PATH)
check("init loads without syntax error", chunk ~= nil)
if not chunk then
    print("load error: " .. tostring(err))
    print(string.format("RESULT: %d passed, %d failed", passed, failed))
    os.exit(1)
end
chunk()

_G.OnTick(30)
local e1 = errLines()
check("broken mod error logged once", #e1 == 1)
check("error names the mod", #e1 == 1 and e1[1]:find("'broken'", 1, true) ~= nil)
check("error carries the Lua message", #e1 == 1 and e1[1]:find("boom-phase-1", 1, true) ~= nil)
check("healthy mod still dispatched despite failure", healthyUpdates == 1)

_G.OnTick(31) -- identical repeat: dedup, no new line
check("identical repeat error deduped", #errLines() == 1)
check("healthy mod dispatched again", healthyUpdates == 2)

errText = "boom-phase-2"
_G.OnTick(32) -- new message: re-logged once
local e2 = errLines()
check("changed error re-logged", #e2 == 2)
check("new message carried", e2[2]:find("boom-phase-2", 1, true) ~= nil)
check("loader continues to healthy mod", healthyUpdates == 3)

realPrint(string.format("RESULT: %d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
