#include <LuaAPI/lua_engine.hpp>
#include <LuaAPI/logger.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>
#include <vector>

namespace LuaAPI {

namespace {

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                   static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        narrow.data(), size, nullptr, nullptr);
    return narrow;
}

lua_State* g_L = nullptr;

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

void SetupEnvironment(lua_State* L) {
    lua_register(L, "print", LuaPrint);

    lua_newtable(L);
    lua_pushliteral(L, "0.1.0");
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "Engine");
}

} // namespace

std::wstring GetModuleDirectory(HMODULE hModule) {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true) {
        len = GetModuleFileNameW(hModule, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) return L"";
        if (len < path.size() - 1 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
        path.resize(path.size() * 2);
    }
    path.resize(len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

void StartEngine(const std::wstring& moduleDir) {
    LUA_LOG_INFO("Initializing Lua engine");

    g_L = luaL_newstate();
    if (!g_L) {
        LUA_LOG_ERROR("luaL_newstate failed");
        return;
    }

    luaL_openlibs(g_L);
    SetupEnvironment(g_L);

    std::wstring scriptPath = moduleDir + L"\\scripts\\init.lua";

    DWORD attrs = GetFileAttributesW(scriptPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        LUA_LOG_WARN("Script not found: {}", Narrow(scriptPath));
        return;
    }

    int result = luaL_dofile(g_L, Narrow(scriptPath).c_str());
    if (result != LUA_OK) {
        const char* err = lua_tostring(g_L, -1);
        LUA_LOG_ERROR("Script error: {}", err ? err : "unknown error");
        lua_pop(g_L, 1);
        return;
    }

    LUA_LOG_INFO("Lua engine initialized, script executed");
}

} // namespace LuaAPI
