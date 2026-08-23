#include <windows.h>
#include <LuaAPI/logger.hpp>
#include <LuaAPI/lua_engine.hpp>

namespace {

DWORD WINAPI Bootstrap(LPVOID param) {
    auto hModule = static_cast<HMODULE>(param);

    LuaAPI::InitPaths(hModule);

    std::wstring dir = LuaAPI::GetModuleDirectory(hModule);
    LuaAPI::Logger::instance().Init(dir + L"\\LuaAPI.log");

    LUA_LOG_INFO("LuaAPI bootstrap thread started (hooking handled by Syringe)");
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // No heavy work under the loader lock: all initialization runs in a worker thread.
        if (CreateThread(nullptr, 0, Bootstrap, hModule, 0, nullptr) == nullptr) {
            // Logger may not be initialized yet; failure is silent here by design.
        }
        break;
    case DLL_PROCESS_DETACH:
        // Only log on explicit unload. When lpReserved is non-null the process is
        // terminating and the CRT/spdlog state may already be destroyed.
        if (lpReserved == nullptr && LuaAPI::Logger::instance().ready()) {
            LUA_LOG_INFO("LuaAPI unloading...");
        }
        break;
    }
    return TRUE;
}
