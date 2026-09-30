#include <windows.h>
#include <tlhelp32.h>
#include <LuaAPI/logger.hpp>
#include <LuaAPI/lua_engine.hpp>
#include <LuaAPI/crash_dump.hpp>
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

  // Log the base/size of every loaded module.
  //
  // SyringeEx owns the unhandled-exception filter, so our own CrashFilter never
  // runs and its dump is all we get. Its dumps contain absolute code addresses
  // with no module table, and a 2026-09-30 crash at 0x71C9E0AA turned out to be
  // outside gamemd.exe entirely (gamemd spans 0x00400000..0x00B93000). Without
  // this map an address in a fault report cannot be attributed to any module.
  void LogModuleMap() {
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
      if (snap == INVALID_HANDLE_VALUE) {
          LUA_LOG_WARN("module map: CreateToolhelp32Snapshot failed (error {})", GetLastError());
          return;
      }
      MODULEENTRY32W me{};
      me.dwSize = sizeof(me);
      int n = 0;
      if (Module32FirstW(snap, &me)) {
          do {
              // fmt only treats `const char*` as a string; a `wchar_t*` counts
              // as a non-void pointer and trips a compile-time static_assert in
              // this project. The module name is therefore narrowed by hand.
              char name[MAX_PATH] = {};
              WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, name, MAX_PATH, nullptr, nullptr);
              const unsigned long long base =
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(me.modBaseAddr));
              const unsigned long long size = static_cast<unsigned long long>(me.modBaseSize);
              LUA_LOG_INFO("module {:<24} base=0x{:08X} size=0x{:X} end=0x{:08X}",
                           name, base, size, base + size);
              ++n;
          } while (Module32NextW(snap, &me));
      }
      CloseHandle(snap);
      LUA_LOG_INFO("module map: {} modules", n);
  }


DWORD WINAPI Bootstrap(LPVOID param) {
    auto hModule = static_cast<HMODULE>(param);

    LuaAPI::InitPaths(hModule);

    std::wstring dir = LuaAPI::GetModuleDirectory(hModule);
    LuaAPI::Logger::instance().Init(dir + L"\\LuaAPI.log");

    LUA_LOG_INFO("LuaAPI bootstrap thread started");

    // First-chance AV -> one MiniDumpNormal into crash_evidence/ (diagnostic).
    // Installed early so even startup-time faults leave a stack.
    LuaAPI::InstallCrashDumper(dir);

    // Идентификация сборки: размер/таймстамп файла и базовый адрес модуля.
      LogGameBinaryInfo(dir);
      LogModuleMap();


    // Initialize hook profiler (QPC circular buffer, 5s rolling window).
    LuaAPI::HookProfilerModuleInit();

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
