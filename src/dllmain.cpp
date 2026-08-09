#include <windows.h>
#include <LuaAPI/logger.hpp>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        LUA_LOG_INFO("LuaAPI DLL loaded");
        break;
    case DLL_PROCESS_DETACH:
        LUA_LOG_INFO("LuaAPI shutting down...");
        LUA_LOG_INFO("Shutdown complete");
        break;
    }
    return TRUE;
}
