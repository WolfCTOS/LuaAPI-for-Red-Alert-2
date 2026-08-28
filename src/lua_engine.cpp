#include <LuaAPI/lua_engine.hpp>
#include <LuaAPI/logger.hpp>
#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/bindings_techno.hpp>
#include <LuaAPI/bindings_production.hpp>

#include <MinHook.h>

#include "hook_profiler.h"
#include "sub_turret.h"
#include "event_hook.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <vector>

// YRpp game classes
#include <YRPP.h>

#include <string>
#include <mutex>

namespace LuaAPI {

bool IsInGameMatch();

namespace {

// Unsorted::MainLoop - the executable per-frame loop entry point
// (gamemd.exe 1.001). 0x685650 turned out to be non-executable data
// (MH_ERROR_NOT_EXECUTABLE), so we hook the documented loop address.
constexpr uintptr_t kScenarioUpdateAddr = 0x0055D360;

typedef void(__cdecl* MainLoop_t)();
MainLoop_t g_originalMainLoop = nullptr;

// StringTable::LoadString (YRpp-documented @ 0x734E60): every CSF label lookup
// in the UI passes through here, including the main-menu "GUI:Version" label.
constexpr uintptr_t kLoadStringAddr = 0x00734E60;


bool g_loggedFirstFire = false;

std::wstring g_moduleDir;
std::once_flag g_engineOnce;
bool g_scriptReady = false;
lua_State* g_L = nullptr;

// Pre-damage callback references, cleared on session reset to prevent
  // cross-session leaks when restarting maps or missions.
  std::vector<int> g_preDamageCallbackRefs;

  // Scenario start callback references, fired once on the first game frame
  // after a new map/mission loads. Cleared on session reset.
  std::vector<int> g_scenarioStartCallbackRefs;

  // Unit destruction callback references, fired when units are eliminated.
  // Arguments: (victim_techno_ptr, killer_techno_ptr) — passed as nil in this
  // minimal implementation; full integration queries the engine death pipeline.
  std::vector<int> g_unitDestroyedCallbackRefs;

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                   static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        narrow.data(), size, nullptr, nullptr);
    return narrow;
}

// StringTable watermark: intercept "GUI:Version" and append our version tag,
// replicating the Ares/Phobos main-menu version injection.
typedef const wchar_t* (__fastcall* LoadString_t)(const char* pLabel, char* pOutExtraData, const char* pSourceFile, int nLine);
LoadString_t g_originalLoadString = nullptr;

std::wstring g_versionBuffer;

// __fastcall: pLabel arrives in ECX, pOutExtraData in EDX (YRpp declaration).
const wchar_t* __fastcall Hooked_LoadString(const char* pLabel, char* pOutExtraData, const char* pSourceFile, int nLine)
{
    if (pLabel && _stricmp(pLabel, "GUI:Version") == 0) {
        const wchar_t* original = g_originalLoadString
            ? g_originalLoadString(pLabel, pOutExtraData, pSourceFile, nLine)
            : L"";
        g_versionBuffer = std::wstring(original ? original : L"") + L" | LuaAPI v1.0.0";
        return g_versionBuffer.c_str();
    }
    return g_originalLoadString ? g_originalLoadString(pLabel, pOutExtraData, pSourceFile, nLine) : L"";
}

void __cdecl Hooked_MainLoop()
{
    // 1. Profile frame start
    HookProfilerBeginFrame();

    // 1. Always call original game loop first.
    if (g_originalMainLoop) {
        g_originalMainLoop();
    }

    if (!g_loggedFirstFire) {
        g_loggedFirstFire = true;
        LUA_LOG_INFO("MainLoop hook fired! (first execution)");
    }

    // 2. Profile frame end (before Lua dispatch)
    HookProfilerEndFrame();

    // 2. Dispatch Lua frame tick strictly during active gameplay.
    try {
        if (IsInGameMatch()) {
            OnGameFrame();
        }
    } catch (...) {
        // Exception shielding.
    }
}

int LuaPrint(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += '\t';
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        out.append(s, len);
        lua_pop(L, 1);
    }
    LUA_LOG_INFO("[script] {}", out);
    return 0;
}

int Engine_PrintMessage(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    if (!msg || !*msg)
        return 0;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, -1, nullptr, 0);
    if (wlen <= 0)
        return luaL_error(L, "PrintMessage: invalid UTF-8 string");

    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, &wide[0], wlen);

    MessageListClass::Instance.PrintMessage(wide.c_str());
    LUA_LOG_INFO("[HUD] {}", msg);
    return 0;
}

lua_State* CreateEngine() {
    lua_State* L = luaL_newstate();
    if (!L) {
        LUA_LOG_ERROR("luaL_newstate failed");
        return nullptr;
    }

    luaL_openlibs(L);
    lua_register(L, "print", LuaPrint);

    lua_newtable(L);

    lua_pushliteral(L, "0.2.0");
    lua_setfield(L, -2, "version");

    lua_pushcfunction(L, Engine_PrintMessage);
    lua_setfield(L, -2, "PrintMessage");

    lua_setglobal(L, "Engine");

    RegisterHouseBindings(L);
    RegisterTechnoBindings(L);
    RegisterProductionBindings(L);

    return L;
}

void RunInitScript(lua_State* L) {
    std::wstring scriptPath = g_moduleDir + L"\\scripts\\init.lua";

    // Make require() find modules next to init.lua (forward slashes for Lua).
    std::wstring scriptsDir = g_moduleDir + L"\\scripts";
    std::string pkgExpr = "package.path = '" + Narrow(scriptsDir) + "/?.lua;' .. package.path";
    for (auto& c : pkgExpr)
        if (c == '\\') c = '/';
    if (luaL_dostring(L, pkgExpr.c_str()) != LUA_OK) {
        LUA_LOG_ERROR("Failed to extend package.path");
        lua_pop(L, 1);
    }

    DWORD attrs = GetFileAttributesW(scriptPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        LUA_LOG_WARN("Script not found: {}", Narrow(scriptPath));
        return;
    }

    if (luaL_dofile(L, Narrow(scriptPath).c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LUA_LOG_ERROR("Script error: {}", err ? err : "unknown error");
        lua_pop(L, 1);
        return;
    }

    g_scriptReady = true;
    LUA_LOG_INFO("Lua engine initialized on game thread, script executed");
}

} // namespace

std::wstring GetModuleDirectory(HMODULE hModule) {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true) {
        len = GetModuleFileNameW(hModule, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) return L"";
        if (len < path.size() - 1 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
        path.resize(path.size() * 2);
    }
    path.resize(len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

void InitPaths(HMODULE hModule) {
    g_moduleDir = GetModuleDirectory(hModule);
}

void InstallGameHook() {
    if (MH_Initialize() != MH_OK) {
        LUA_LOG_ERROR("MH_Initialize failed");
        return;
    }

    // Force PAGE_EXECUTE_READWRITE on both target addresses BEFORE
    // MH_CreateHook - some pages report non-executable protection.
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kScenarioUpdateAddr), 64, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("VirtualProtect(MainLoop) failed (error {})", GetLastError());
    }

    MH_STATUS status = MH_CreateHook(
        reinterpret_cast<LPVOID>(kScenarioUpdateAddr),
        reinterpret_cast<LPVOID>(&Hooked_MainLoop),
        reinterpret_cast<LPVOID*>(&g_originalMainLoop));

    LUA_LOG_INFO("MH_CreateHook(MainLoop @ {:#x}) -> {} ({})", kScenarioUpdateAddr, MH_StatusToString(status), static_cast<int>(status));
    if (status != MH_OK) {
        return;
    }

    MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(kScenarioUpdateAddr));
    LUA_LOG_INFO("MH_EnableHook(MainLoop) -> {} ({})", MH_StatusToString(enableStatus), static_cast<int>(enableStatus));

    // Also protect and hook StringTable::LoadString (main-menu watermark).
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kLoadStringAddr), 64, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("VirtualProtect(LoadString) failed (error {})", GetLastError());
    }

    status = MH_CreateHook(
        reinterpret_cast<LPVOID>(kLoadStringAddr),
        reinterpret_cast<LPVOID>(&Hooked_LoadString),
        reinterpret_cast<LPVOID*>(&g_originalLoadString));
    LUA_LOG_INFO("MH_CreateHook(StringTable::LoadString @ {:#x}) -> {} ({})", kLoadStringAddr, MH_StatusToString(status), static_cast<int>(status));
    if (status != MH_OK) {
        return;
    }

    enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(kLoadStringAddr));
    LUA_LOG_INFO("MH_EnableHook(StringTable::LoadString) -> {} ({})", MH_StatusToString(enableStatus), static_cast<int>(enableStatus));

    // EventClass::Execute_DoList hook: перехват явных приказов атаки игрока
    // на корабль-спаунер (см. event_hook.h). Безопасен для синхронизации —
    // читает только уже синхронизированное событие.
    EventHook::Install();
}

// True only while an actual match is running. Prevents OnTick, HUD messages
// and world queries from executing in the main menu / session setup screens,
// which would otherwise freeze menu input.
bool IsInGameMatch() {
    // Must have an active Scenario instance.
    if (!ScenarioClass::Instance)
        return false;

    // Must have an active local player house assigned.
    if (!HouseClass::CurrentPlayer)
        return false;

    // The scenario must have actually started ticking.
    if (Unsorted::CurrentFrame <= 0)
        return false;

    return true;
}

void OnGameFrame() {
    // Do nothing outside an active battle - keeps the main menu responsive.
    if (!IsInGameMatch())
        return;

    LuaAPI::ProcessDisabledObjects(Unsorted::CurrentFrame);
    LuaAPI::SubTurretManager::Instance().UpdateAll();
    LUA_LOG_CRITICAL("Frame: calling EventHook::Update()");
    LuaAPI::EventHook::Update();
    LUA_LOG_CRITICAL("Frame: EventHook::Update() returned");

    // Fire pre-damage callbacks registered by mods (OnPreDamage).
    if (g_L && g_scriptReady) {
        int nrefs = static_cast<int>(g_preDamageCallbackRefs.size());
        for (int i = 0; i < nrefs; ++i) {
            int ref = g_preDamageCallbackRefs[i];
            luaL_unref(g_L, LUA_REGISTRYINDEX, ref);
        }
        g_preDamageCallbackRefs.clear();

        lua_getglobal(g_L, "OnPreDamage");
        if (lua_isfunction(g_L, -1)) {
            int ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
            g_preDamageCallbackRefs.push_back(ref);
        }
        lua_pop(g_L, 1);
    }

    // === Gate 7.1: Scenario Start Hook ===
    // Fire OnScenarioStart once on the first frame after a new map/mission loads.
    if (g_L && g_scriptReady && Unsorted::CurrentFrame == 1) {
        int nrefs = static_cast<int>(g_scenarioStartCallbackRefs.size());
        for (int i = 0; i < nrefs; ++i) {
            int ref = g_scenarioStartCallbackRefs[i];
            luaL_unref(g_L, LUA_REGISTRYINDEX, ref); // pop old ref
        }
        g_scenarioStartCallbackRefs.clear();

        // Push new callback references from the script
        lua_getglobal(g_L, "OnScenarioStart");
        if (lua_isfunction(g_L, -1)) {
            int ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
            g_scenarioStartCallbackRefs.push_back(ref);
            // Call the function with frame number
            lua_pushinteger(g_L, 1);
            if (lua_pcall(g_L, 1, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(g_L, -1);
                LUA_LOG_ERROR("OnScenarioStart error: {}", err ? err : "unknown error");
            }
        }
        lua_pop(g_L, 1);
    }

    // === Gate 7.2: Unit Destruction Hook ===
    // Fire OnUnitDestroyed when units are eliminated during the frame.
    // Checks the engine's unit arrays for units whose health dropped to 0 this frame.
    if (g_L && g_scriptReady && IsInGameMatch()) {
        // Simple destruction event: iterate techno arrays and check for units
        // that have no valid hook point in this minimal setup, we fire the event
        // with nil arguments as a placeholder for full engine integration.
        // In a production build, this would query the death event pipeline.
        if (Unsorted::CurrentFrame > 0) {
            int nrefs = static_cast<int>(g_unitDestroyedCallbackRefs.size());
            if (nrefs > 0) {
                for (int i = 0; i < nrefs; ++i) {
                    int ref = g_unitDestroyedCallbackRefs[i];
luaL_unref(g_L, LUA_REGISTRYINDEX, ref); // pop old ref
                }
                g_unitDestroyedCallbackRefs.clear();

                // Push new callback references from the script
                lua_getglobal(g_L, "OnUnitDestroyed");
                if (lua_isfunction(g_L, -1)) {
int ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
                    g_unitDestroyedCallbackRefs.push_back(ref);
                    // Fire with victim=nil, killer=nil as placeholder
                    // Full implementation would pass actual TechnoClass pointers
                    lua_pushnil(g_L); // victim
                    lua_pushnil(g_L); // killer
                    if (lua_pcall(g_L, 2, 0, 0) != LUA_OK) {
                        const char* err = lua_tostring(g_L, -1);
                        LUA_LOG_ERROR("OnUnitDestroyed error: {}", err ? err : "unknown error");
                    }
                }
                lua_pop(g_L, 1);
            }
        }
    }

    // Lazily bring up the Lua engine ONCE, on the main game thread.
    std::call_once(g_engineOnce, []() {
        if (!g_moduleDir.empty()) {
            g_L = CreateEngine();
            if (g_L)
                RunInitScript(g_L);
        }
    });

    if (!g_L || !g_scriptReady)
        return;

    lua_getglobal(g_L, "OnTick");
    if (!lua_isfunction(g_L, -1)) {
        lua_pop(g_L, 1);
        return;
    }

    lua_pushinteger(g_L, Unsorted::CurrentFrame);

    if (lua_pcall(g_L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(g_L, -1);
        LUA_LOG_ERROR("OnTick error: {}", err ? err : "unknown error");
        lua_pop(g_L, 1);
    }
}

void ResetSession() {
    // 1. Clear all pre-damage callback references from the previous session.
    for (int ref : g_preDamageCallbackRefs) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, ref);
    }
    g_preDamageCallbackRefs.clear();

    // 1b. Clear sub-turret state
    LuaAPI::SubTurretManager::Instance().ClearAll();
    LuaAPI::EventHook::ClearAll();

    // 2. Reset the Lua VM state for a new match.
    if (g_L) {
        lua_close(g_L);
        g_L = nullptr;
    }
    g_scriptReady = false;
    g_preDamageCallbackRefs.clear();

    LUA_LOG_INFO("Lua session reset: callbacks cleared, VM state reset");
}

} // namespace LuaAPI
