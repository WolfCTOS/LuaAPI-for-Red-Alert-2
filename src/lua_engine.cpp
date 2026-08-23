#include <LuaAPI/lua_engine.hpp>
#include <LuaAPI/logger.hpp>
#include <LuaAPI/bindings_house.hpp>

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

// YRpp documents this as the game's main loop (Unsorted::MainLoop, gamemd.exe 1.001).
// Fires every frame in both menus and battles; safer than guessing member addresses.
constexpr uintptr_t kMainLoopAddr = 0x0055D360;

using MainLoop_t = void(__fastcall*)();

MainLoop_t g_originalMainLoop = nullptr;
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

void __fastcall MainLoop_Detour()
{
    if (!g_loggedFirstFire) {
        g_loggedFirstFire = true;
        LUA_LOG_INFO("MainLoop hook fired! (first execution)");
    }

    g_originalMainLoop();

    try {
        OnGameFrame();
    } catch (...) {
        // Never let an exception escape into the game.
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

    return L;
}

void RunInitScript(lua_State* L) {
    std::wstring scriptPath = g_moduleDir + L"\\scripts\\init.lua";

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
        reinterpret_cast<LPVOID>(kMainLoopAddr),
        reinterpret_cast<LPVOID>(&MainLoop_Detour),
        reinterpret_cast<LPVOID*>(&g_originalMainLoop));

    LUA_LOG_INFO("MH_CreateHook(MainLoop @ {:#x}) -> {} ({})", kMainLoopAddr, MH_StatusToString(status), static_cast<int>(status));
    if (status != MH_OK) {
        return;
    }

    status = MH_EnableHook(reinterpret_cast<LPVOID>(kMainLoopAddr));
    LUA_LOG_INFO("MH_EnableHook -> {} ({})", MH_StatusToString(status), static_cast<int>(status));
}

void OnGameFrame() {
    // Only run while a scenario is active.
    if (!ScenarioClass::Instance)
        return;

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
