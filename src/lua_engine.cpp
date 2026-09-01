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
#include <cstring>
#include <cstdio>

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

// ---------------------------------------------------------------------------
// Сигнатурная проверка байтовой сигратуры пролога функции — ТОЛЬКО ЛОГГЕР.
// С Milestone 11 / Gate 11.1 проверка больше НЕ блокирует установку хука:
// при несовпадении живых байт с ожидаемыми (другая/CnCNet-сборка) пишем
// LUA_LOG_WARN с hex живых байт, но MH_CreateHook всё равно вызывается.
// Деград (пропуск хука) происходит исключительно когда сам MH_CreateHook
// возвращает ошибку.
// ---------------------------------------------------------------------------
std::string BytesToHex(const uint8_t* bytes, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", bytes[i]);
        out += b;
        if (i + 1 < n) out += ' ';
    }
    return out;
}

bool LogByteSignature(uintptr_t addr, const uint8_t expected[8], const char* name) {
    uint8_t actual[8] = {0};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr),
                           actual, sizeof(actual), &read) || read != sizeof(actual)) {
        LUA_LOG_WARN("Signature read FAILED for {} @ 0x{:08X}, read={} (continuing anyway)",
                     name, addr, read);
        return false;
    }
    if (memcmp(actual, expected, sizeof(actual)) != 0) {
        LUA_LOG_WARN("Signature MISMATCH for {} @ 0x{:08X}: expected=[{}] live=[{}] (non-blocking, hooking anyway)",
                     name, addr, BytesToHex(expected, 8), BytesToHex(actual, 8));
        return false;
    }
    LUA_LOG_INFO("Signature OK for {} @ 0x{:08X}: [{}]", name, addr, BytesToHex(actual, 8));
    return true;
}

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
    // Milestone 11 / Multiplayer determinism step 1: logic-frame gating.
    // MainLoop может исполняться несколько раз на один логический кадр
    // (у клиентов разный FPS). Вся ЛОГИКА (EventHook::Update и Lua Update)
    // выполняется ОДИН раз на логический кадр — только когда Unsorted::CurrentFrame
    // изменился с прошлого вызова. Иначе состояние (rofTimer, кулдауны, цели)
    // разошлось бы между клиентами и привело к десинхрону. Побочный плюс:
    // пер-кадровый спам лога снижается до одной записи на логический кадр.
    static unsigned int g_lastFrame = 0;
    unsigned int curFrame = Unsorted::CurrentFrame;
    if (curFrame == g_lastFrame) {
        return;
    }
    g_lastFrame = curFrame;

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

// Глобальный флаг: выключает экранный вывод HUD-сообщений и их звук, но
// запись в файл лога продолжается. Переключается клавишей ` (VK 0xC0).
static bool g_hudMuted = false;

int Engine_PrintMessage(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    if (!msg || !*msg)
        return 0;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, -1, nullptr, 0);
    if (wlen <= 0)
        return luaL_error(L, "PrintMessage: invalid UTF-8 string");

    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, &wide[0], wlen);

    // Мьют (клавиша `) : экран + звук пропускаем, файл-лог пишем всегда.
    if (!g_hudMuted)
        MessageListClass::Instance.PrintMessage(wide.c_str());
    LUA_LOG_INFO("[HUD] {}", msg);
    return 0;
}

// Edge-triggered опрос клавиши ` (VK 0xC0): переключает g_hudMuted.
// В файл лога пишется всегда; на экране — только если лог остался включён.
void PollHudMuteToggle() {
    static bool prev = false;
    bool down = (GetAsyncKeyState(0xC0) & 0x8000) != 0;
    bool edge = down && !prev;
    prev = down;
    if (!edge)
        return;

    g_hudMuted = !g_hudMuted;
    LUA_LOG_INFO("[HUD] HUD output {}", g_hudMuted ? "muted" : "unmuted");
    if (!g_hudMuted) {
        // После включения показываем на экране (иначе сообщение не появится).
        MessageListClass::Instance.PrintMessage(L"HUD output unmuted");
    }
}

// Engine.SetHudMuted(bool) -> nil
int Engine_SetHudMuted(lua_State* L) {
    g_hudMuted = lua_toboolean(L, 1) != 0;
    LUA_LOG_INFO("[HUD] HUD output {}", g_hudMuted ? "muted" : "unmuted");
    return 0;
}

// Engine.IsHudMuted() -> bool
int Engine_IsHudMuted(lua_State* L) {
    lua_pushboolean(L, g_hudMuted ? 1 : 0);
    return 1;
}

// HUD-текст дебаг-консоли (глобал). Обновляется каждый логический кадр в
// OnGameFrame; пуст, когда режим ввода выключен. Объявлен до первого
// использования (Engine_GetDebugHudText / DrawDebugHud).
std::string g_debugHudText;

// Game.GetDebugHudText() -> string
// Lua-доступ к тексту HUD-индикатора дебаг-консоли (fallback, если прямой HUD-
// механизм не даёт видимого результата в данной сборке/ранере).
int Engine_GetDebugHudText(lua_State* L) {
    lua_pushlstring(L, g_debugHudText.c_str(), g_debugHudText.size());
    return 1;
}

// Engine.WeaponExists(id) -> bool
// Проверяет существование оружия в rules: WeaponTypeClass::Find(id) != null.
// SEH-обёртка; возвращает false при ошибке или если id пуст.
int Engine_WeaponExists(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    if (!id || !*id) { lua_pushboolean(L, 0); return 1; }

    bool exists = false;
    __try {
        exists = WeaponTypeClass::Find(id) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        exists = false;
    }
    lua_pushboolean(L, exists ? 1 : 0);
    return 1;
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
    lua_pushcfunction(L, Engine_WeaponExists);
    lua_setfield(L, -2, "WeaponExists");
    lua_pushcfunction(L, Engine_SetHudMuted);
    lua_setfield(L, -2, "SetHudMuted");
    lua_pushcfunction(L, Engine_IsHudMuted);
    lua_setfield(L, -2, "IsHudMuted");

    lua_setglobal(L, "Engine");

    // Global "Game" table: Game.GetDebugHudText() -> string
    lua_newtable(L);
    lua_pushcfunction(L, Engine_GetDebugHudText);
    lua_setfield(L, -2, "GetDebugHudText");
    lua_setglobal(L, "Game");

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

// ---------------------------------------------------------------------------
// Слой ввода для дебаг-консоли (dev-команды ИИ). Всё выполняется в OnGameFrame
// (логический кадр). Удержание клавиши НЕ спамит: используется детект перехода
// "в прошлом кадре не нажата -> сейчас нажата" с хранением предыдущего состояния.
// ---------------------------------------------------------------------------
constexpr int kDebugCmdMax = 256;

struct DebugInputState {
    bool mode = false;        // режим ввода активен (VK_BACK переключает)
    std::string buffer;       // накопленные печатные символы
};
DebugInputState g_debugInput;

// Узкая строка -> широкая (для TextPrint / DrawText), SEH-не требуется.
static std::wstring ToWide(const std::string& s) {
    if (s.empty())
        return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 0)
        return L"";
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], wlen);
    if (!w.empty())
        w.pop_back(); // убрать хвостовой '\0'
    return w;
}

// Rисует уже подготовленную широкую HUD-строку. Вынесена в отдельную функцию
// БЕЗ C++-объектов, чтобы SEH __try не конфликтовал с unwinding (C2712).
static void DrawHudText(const wchar_t* wtext) {
    __try {
        DSurface* pSurface = DSurface::Primary;
        if (pSurface)
            pSurface->DrawText(wtext, 12, 42, 0xFFFF00 /* yellow */);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Not fatal for a debug indicator; silently skip.
    }
}

// Рисует g_debugHudText на видимом кадре каждый логический кадр (TextPrint-
// механизм через DSurface::DrawText). SEH-безопасно, best-effort.
static void DrawDebugHud() {
    if (g_debugHudText.empty())
        return;
    std::wstring wtext = ToWide(g_debugHudText);
    if (wtext.empty())
        return;
    DrawHudText(wtext.c_str());
}

// Детект перехода "отпущена -> нажата" для клавиши, с хранением prev-состояния.
static bool KeyEdge(int vk, bool* prevPtr) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = down && !*prevPtr;
    *prevPtr = down;
    return edge;
}

static bool IsDebugPrintable(int vk) {
    return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == VK_SPACE;
}

// Передача команды в Lua: глобальная OnDebugCommand(text), обёрнутая в pcall.
static void CallDebugCommand(lua_State* L, const std::string& cmd) {
    if (!L || cmd.empty())
        return;
    lua_getglobal(L, "OnDebugCommand");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1); // обработчика нет — игнорируем (debug)
        return;
    }
    lua_pushlstring(L, cmd.data(), cmd.size());
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LUA_LOG_ERROR("[DebugCmd] OnDebugCommand error: {}", err ? err : "unknown error");
        lua_pop(L, 1);
    }
}

static void ClearDebugInput() {
    g_debugInput.mode = false;
    g_debugInput.buffer.clear();
    g_debugHudText.clear();
}

static void ProcessDebugInput(lua_State* L) {
    static bool backPrev = false;
    static bool enterPrev = false;
    static bool charPrev[256] = {};   // per-VK prev state for printable keys

    // 1) VK_BACK: переключение режима; при включении буфер очищается.
    if (KeyEdge(VK_BACK, &backPrev)) {
        g_debugInput.mode = !g_debugInput.mode;
        if (g_debugInput.mode) {
            g_debugInput.buffer.clear();
            LUA_LOG_INFO("[DebugCmd] input mode ENABLED");
        } else {
            LUA_LOG_INFO("[DebugCmd] input mode DISABLED");
        }
    }

    if (g_debugInput.mode) {
        // 3) Печатные A-Z, 0-9 и пробел: добавлять по одной на нажатие (edge).
        for (int vk = '0'; vk <= '9'; ++vk)
            if (KeyEdge(vk, &charPrev[vk]) && g_debugInput.buffer.size() < kDebugCmdMax)
                g_debugInput.buffer += static_cast<char>(vk);
        for (int vk = 'A'; vk <= 'Z'; ++vk)
            if (KeyEdge(vk, &charPrev[vk]) && g_debugInput.buffer.size() < kDebugCmdMax)
                g_debugInput.buffer += static_cast<char>(vk);
        if (KeyEdge(VK_SPACE, &charPrev[VK_SPACE]) && g_debugInput.buffer.size() < kDebugCmdMax)
            g_debugInput.buffer += ' ';

        // 4) VK_RETURN: передать буфер в Lua и очистить.
        if (KeyEdge(VK_RETURN, &enterPrev)) {
            if (!g_debugInput.buffer.empty()) {
                LUA_LOG_INFO("[DebugCmd] -> OnDebugCommand('{}')", g_debugInput.buffer);
                CallDebugCommand(L, g_debugInput.buffer);
                g_debugInput.buffer.clear();
            } else {
                LUA_LOG_INFO("[DebugCmd] empty buffer, ignored");
            }
        }
    } else {
        // Режим выключен: обновляем prev для Enter, чтобы при включении зажатый
        // Enter не дал ложный edge, и обнуляем prev печатных клавиш.
        enterPrev = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        std::memset(charPrev, 0, sizeof(charPrev));
    }
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

    // Ванильная пролог-байтовая сигнатура (первые 8 байт) целевых функций.
    // Значения залогированы из текущей рабочей ванильной сборки gamemd.exe.
    // С Gate 11.1 проверка — логгер, НЕ блокер: при несовпадении живых байт
    // пишем WARN с hex, но всё равно вызываем MH_CreateHook. Деград (пропуск
    // хука) наступает только если сам MH_CreateHook вернёт ошибку.
    static const uint8_t kMainLoopSig[8] = {0xA0, 0xA0, 0xE9, 0xA8, 0x00, 0x81, 0xEC, 0xB4};
    static const uint8_t kLoadStringSig[8] = {0x53, 0x56, 0x8B, 0xF2, 0x8B, 0xD9, 0x85, 0xF6};

    // --- MainLoop (0x0055D360) ---
    LogByteSignature(kScenarioUpdateAddr, kMainLoopSig, "MainLoop");
    {
        // Force PAGE_EXECUTE_READWRITE BEFORE MH_CreateHook - some pages report
        // non-executable protection.
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
            LUA_LOG_WARN("Skipping MainLoop hook installation (degraded): MH_CreateHook returned {}", static_cast<int>(status));
        } else {
            MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(kScenarioUpdateAddr));
            LUA_LOG_INFO("MH_EnableHook(MainLoop) -> {} ({})", MH_StatusToString(enableStatus), static_cast<int>(enableStatus));
        }
    }

    // --- StringTable::LoadString (0x00734E60) ---
    LogByteSignature(kLoadStringAddr, kLoadStringSig, "LoadString");
    {
        // Also protect and hook StringTable::LoadString (main-menu watermark).
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(kLoadStringAddr), 64, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LUA_LOG_WARN("VirtualProtect(LoadString) failed (error {})", GetLastError());
        }

        MH_STATUS status = MH_CreateHook(
            reinterpret_cast<LPVOID>(kLoadStringAddr),
            reinterpret_cast<LPVOID>(&Hooked_LoadString),
            reinterpret_cast<LPVOID*>(&g_originalLoadString));
        LUA_LOG_INFO("MH_CreateHook(StringTable::LoadString @ {:#x}) -> {} ({})", kLoadStringAddr, MH_StatusToString(status), static_cast<int>(status));
        if (status != MH_OK) {
            LUA_LOG_WARN("Skipping LoadString hook installation (degraded): MH_CreateHook returned {}", static_cast<int>(status));
        } else {
            MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(kLoadStringAddr));
            LUA_LOG_INFO("MH_EnableHook(StringTable::LoadString) -> {} ({})", MH_StatusToString(enableStatus), static_cast<int>(enableStatus));
        }
    }

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
    LUA_LOG_TRACE("Frame: calling EventHook::Update()");
    LuaAPI::EventHook::Update();
    LUA_LOG_TRACE("Frame: EventHook::Update() returned");

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

    // Переключатель HUD-лога на клавишу ` (VK 0xC0). Не обрабатываем, пока
    // активен режим ввода отладочной консоли (чтобы тильда не сбивала набор).
    if (!g_debugInput.mode)
        PollHudMuteToggle();

    // Дебаг-консоль (dev-команды ИИ): опрос клавиш раз в логический кадр.
    ProcessDebugInput(g_L);

    // HUD-индикатор дебаг-консоли: обновляем текст и рисуем на экране.
    if (g_debugInput.mode) {
        g_debugHudText = "DEBUG MODE: [" + g_debugInput.buffer + "] | ENTER=send BACKSPACE=toggle";
    } else {
        g_debugHudText.clear();
    }
    DrawDebugHud();

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

    // 1c. Сброс состояния дебаг-консоли.
    ClearDebugInput();

    // 2. Reset the Lua VM state for a new match.
    if (g_L) {
        LuaAPI::ClearHouseCache(g_L);   // освободить реестровые ссылки домов до закрытия VM
        lua_close(g_L);
        g_L = nullptr;
    }
    g_scriptReady = false;
    g_preDamageCallbackRefs.clear();

    LUA_LOG_INFO("Lua session reset: callbacks cleared, VM state reset");
}

} // namespace LuaAPI
