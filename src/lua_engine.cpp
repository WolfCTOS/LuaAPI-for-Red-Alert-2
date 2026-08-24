#include <LuaAPI/lua_engine.hpp>
#include <LuaAPI/logger.hpp>
#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/bindings_techno.hpp>

#include <MinHook.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// YRpp game classes
#include <YRPP.h>

#include <string>
#include <mutex>

namespace LuaAPI {

namespace {

// ScenarioClass::Update is __thiscall on ScenarioClass* (0x00685650 in gamemd.exe 1.001).
// It is ONLY called while an active scenario/battle is running, leaving the
// main menu 100% native (no interference with mouse/message dispatch).
constexpr uintptr_t kScenarioUpdateAddr = 0x00685650;

// StringTable::LoadString (YRpp-documented @ 0x734E60): every CSF label lookup
// in the UI passes through here, including the main-menu "GUI:Version" label.
constexpr uintptr_t kLoadStringAddr = 0x00734E60;

typedef void(__thiscall* ScenarioUpdate_t)(void* pScenario);
ScenarioUpdate_t g_originalScenarioUpdate = nullptr;
bool g_loggedFirstFire = false;

std::wstring g_moduleDir;
std::once_flag g_engineOnce;
bool g_scriptReady = false;
lua_State* g_L = nullptr;

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
typedef const wchar_t* (__stdcall* LoadString_t)(const char* pLabel, char* pOutExtraData, const char* pSourceFile, int nLine);
LoadString_t g_originalLoadString = nullptr;

std::wstring g_versionBuffer;

const wchar_t* __stdcall Hooked_LoadString(const char* pLabel, char* pOutExtraData, const char* pSourceFile, int nLine)
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

// __fastcall with dummy EDX to handle __thiscall safely in the detour.
void __fastcall Hooked_ScenarioUpdate(void* pScenario, void* edx_unused)
{
    // 1. Call original game scenario update.
    if (g_originalScenarioUpdate) {
        g_originalScenarioUpdate(pScenario);
    }

    if (!g_loggedFirstFire) {
        g_loggedFirstFire = true;
        LUA_LOG_INFO("ScenarioClass::Update hook fired! (first execution)");
    }

    // 2. Dispatch Lua frame tick only during a real battle.
    try {
        if (pScenario && ScenarioClass::Instance) {
            OnGameFrame();
        }
    } catch (...) {
        // Catch all exceptions to protect the game engine.
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

    MH_STATUS status = MH_CreateHook(
        reinterpret_cast<LPVOID>(kScenarioUpdateAddr),
        reinterpret_cast<LPVOID>(&Hooked_ScenarioUpdate),
        reinterpret_cast<LPVOID*>(&g_originalScenarioUpdate));

    LUA_LOG_INFO("MH_CreateHook(ScenarioClass::Update @ {:#x}) -> {} ({})", kScenarioUpdateAddr, MH_StatusToString(status), static_cast<int>(status));
    if (status != MH_OK) {
        return;
    }

    status = MH_EnableHook(reinterpret_cast<LPVOID>(kScenarioUpdateAddr));
    LUA_LOG_INFO("MH_EnableHook(ScenarioClass::Update) -> {} ({})", MH_StatusToString(status), static_cast<int>(status));

    // Main-menu version watermark (StringTable::LoadString).
    status = MH_CreateHook(
        reinterpret_cast<LPVOID>(kLoadStringAddr),
        reinterpret_cast<LPVOID>(&Hooked_LoadString),
        reinterpret_cast<LPVOID*>(&g_originalLoadString));
    LUA_LOG_INFO("MH_CreateHook(StringTable::LoadString @ {:#x}) -> {} ({})", kLoadStringAddr, MH_StatusToString(status), static_cast<int>(status));
    if (status != MH_OK) {
        return;
    }

    status = MH_EnableHook(reinterpret_cast<LPVOID>(kLoadStringAddr));
    LUA_LOG_INFO("MH_EnableHook(StringTable::LoadString) -> {} ({})", MH_StatusToString(status), static_cast<int>(status));
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

} // namespace LuaAPI
