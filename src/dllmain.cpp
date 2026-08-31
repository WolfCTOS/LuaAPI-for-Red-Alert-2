#include "sub_turret.h"
#include <windows.h>
#include <LuaAPI/logger.hpp>
#include <LuaAPI/lua_engine.hpp>
#include "hook_profiler.h"

namespace {

// Логируем размер, таймстамп файла gamemd.exe и базовый адрес модуля, чтобы
// сравнить ванильную и CnCNet-сборки в одном логе.
void LogGameBinaryInfo(const std::wstring& gameDir) {
    std::wstring exePath = gameDir + L"\\gamemd.exe";

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(exePath.c_str(), GetFileExInfoStandard, &fad)) {
        ULONGLONG size = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        SYSTEMTIME st{};
        FileTimeToSystemTime(&fad.ftLastWriteTime, &st);
        LUA_LOG_INFO("gamemd.exe file: size={} bytes, lastWrite={:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                     size, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    } else {
        LUA_LOG_WARN("gamemd.exe file: GetFileAttributesExW failed (error {})", GetLastError());
    }

    HMODULE gameMod = GetModuleHandleW(L"gamemd.exe");
    if (gameMod) {
        LUA_LOG_INFO("gamemd.exe module base = 0x{:08X}", reinterpret_cast<uintptr_t>(gameMod));
    } else {
        LUA_LOG_WARN("gamemd.exe module not loaded (GetModuleHandleW)");
    }
}

DWORD WINAPI Bootstrap(LPVOID param) {
    auto hModule = static_cast<HMODULE>(param);

    LuaAPI::InitPaths(hModule);

    std::wstring dir = LuaAPI::GetModuleDirectory(hModule);
    LuaAPI::Logger::instance().Init(dir + L"\\LuaAPI.log");

    LUA_LOG_INFO("LuaAPI bootstrap thread started");

    // Идентификация сборки: размер/таймстамп файла и базовый адрес модуля.
    LogGameBinaryInfo(dir);

    // Initialize hook profiler (QPC circular buffer, 5s rolling window).
    LuaAPI::HookProfilerModuleInit();
        LuaAPI::SubTurretManager::Instance().InitDrawHook();

    // Install game simulation hooks via MinHook
    // (ScenarioClass::Update @ 0x685650 + StringTable::LoadString watermark).
    LuaAPI::InstallGameHook();
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
