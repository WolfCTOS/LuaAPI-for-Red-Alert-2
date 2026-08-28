// RA2 Yuri's Revenge — LuaAPI Injector (modern dark Win32 GUI, no console)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// Глобальные функции, определённые ниже на уровне файла; видимы и из анонимного namespace.
void RecalcLayout();
void ToggleFullscreen();

namespace {

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
constexpr COLORREF kBg        = RGB(17, 20, 26);    // #11141A
constexpr COLORREF kSurface   = RGB(27, 32, 43);    // #1B202B
constexpr COLORREF kHover     = RGB(37, 44, 61);    // #252C3D
constexpr COLORREF kRed       = RGB(229, 62, 62);   // #E53E3E
constexpr COLORREF kBlue      = RGB(49, 130, 206);  // #3182CE
constexpr COLORREF kGreen     = RGB(47, 133, 90);   // #2F855A
constexpr COLORREF kGreenHover= RGB(56, 161, 105);
constexpr COLORREF kText      = RGB(247, 250, 252); // #F7FAFC
constexpr COLORREF kDim       = RGB(160, 174, 192); // #A0AEC0
constexpr COLORREF kBadge     = RGB(74, 85, 104);   // #4A5568
constexpr COLORREF kOrange    = RGB(237, 137, 54);
constexpr COLORREF kOk        = RGB(72, 187, 120);

constexpr const wchar_t* kWindowClass = L"LuaAPIInjectorWnd";
constexpr const wchar_t* kWindowTitle = L"RA2 Yuri's Revenge - LuaAPI Engine";

constexpr int kDefaultClientW = 580;
constexpr int kDefaultClientH = 640;
constexpr int kPad = 20;
constexpr int kCardH = 76;     // Высота карточки
constexpr int kCardGap = 8;    // Зазор между карточками
constexpr int kCardStep = 84;  // Полный шаг цикла (76 + 8)
constexpr int kScrollW = 8;    // Ширина скроллбара

constexpr const wchar_t* kGameProcess = L"gamemd.exe";

// ---------------------------------------------------------------------------
// Localization (RU / EN)
// ---------------------------------------------------------------------------
bool g_isRussian = true;

const wchar_t* L10N(const wchar_t* ru, const wchar_t* en) {
    return g_isRussian ? ru : en;
}

const wchar_t* Str_Subtitle() {
    return L10N(L"Yuri's Revenge v1.001 Modding Platform",
                L"Yuri's Revenge v1.001 Modding Platform");
}
const wchar_t* Str_StatusReady() {
    return L10N(L"Готов к запуску",
                L"Ready to Launch");
}
const wchar_t* Str_StatusInjected() { return L10N(
    L"Игра запущена & LuaAPI внедрена",
    L"Game Running & LuaAPI Injected"); }
const wchar_t* Str_Busy() { return L10N(
    L"Работает...",
    L"Working..."); }
const wchar_t* Str_LaunchBtn() { return L10N(
    L"Запустить игру",
    L"Launch Game"); }
const wchar_t* Str_InjectBtn() { return L10N(
    L"Внедрить",
    L"Inject"); }
const wchar_t* Str_ModsHeader() { return L10N(
    L"МОДЫ",
    L"INSTALLED MODS"); }
const wchar_t* Str_SaveBtn() { return L10N(
    L"Сохранить и применить",
    L"Save & Apply"); }
std::wstring Str_ActiveCount(int active, int total) {
    return g_isRussian
        ? L"\u0410\u043A\u0442\u0438\u0432\u043D\u043E: " + std::to_wstring(active) + L" \u0438\u0437 " + std::to_wstring(total)
        : L"Active: " + std::to_wstring(active) + L" of " + std::to_wstring(total);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class AppState { Ready, Busy, StatusError, Injected };

HWND g_hwnd = nullptr;
HFONT g_fontTitle = nullptr;   // 16pt bold
HFONT g_fontHeader = nullptr;  // card titles, 11pt bold
HFONT g_fontReg = nullptr;     // 12pt
HFONT g_fontSmall = nullptr;   // 10pt

DWORD g_gamePid = 0;
bool g_skipInjection = false;
bool g_injectCnCNet = false;
std::wstring g_gameName;
AppState g_appState = AppState::Ready;
bool g_dirty = false;
bool g_launching = false;
bool g_injecting = false;
constexpr UINT WM_APP_LAUNCH_DONE = WM_APP + 1;
constexpr UINT WM_APP_INJECT_DONE = WM_APP + 2;
constexpr UINT kToastTimerId = 1;

// Translatable status: store the KEY, localize at paint time so the
// RU/EN switch instantly re-translates even previously shown statuses.
enum class StatusKey { Ready, Injected, NotFound, DllMissing, InjectFail, GameNotFound, BusyLaunch, BusyInject, SaveFail, Custom };
StatusKey g_statusKey = StatusKey::Ready;
std::wstring g_statusCustom;
COLORREF g_statusColor = kOk;

int g_clientW = kDefaultClientW;
int g_clientH = kDefaultClientH;
int g_scroll = 0;

struct ModEntry {
    std::wstring dir;
    std::wstring id;
    std::wstring name = L"?";
    std::wstring version = L"1.0.0";
    std::wstring author = L"unknown";
    std::wstring description;
    std::vector<std::wstring> conflicts;
    bool hasManifest = false;
    bool enabled = false;
};

std::vector<ModEntry> g_mods;

RECT g_rcLaunch{};
RECT g_rcInject{};
RECT g_rcSave{};
RECT g_rcLang{};
RECT g_rcFs{};
bool g_hoverLaunch = false;
bool g_hoverInject = false;
bool g_hoverSave = false;
bool g_hoverLang = false;
bool g_hoverFs = false;
bool g_trackingMouse = false;
bool g_headless = false;
bool g_fullscreen = false;
RECT g_windowedRect{};  // исходная геометрия окна для возврата из полного экрана

// RecCalcLayout/ToggleFullscreen определены ниже на уровне файла (глобально).

// Drag-and-drop reorder state for the mod cards.
struct DragState {
    bool pendingClick = false;   // mouse-down on a card, not yet decided click vs drag
    bool dragging = false;       // drag in progress (moved > 6px)
    int pendingIndex = -1;       // card index where mouse-down happened
    int dragIndex = -1;          // current index of the dragged card
    POINT downPos{0, 0};         // client coords of the mouse-down
    int dragAnchorY = 0;         // client Y anchor for step-based swapping
};
DragState g_dragState;

// Worker-thread result for async injection, posted to the UI thread via WM_APP_INJECT_DONE.
struct InjectResult {
    bool ok = false;
    DWORD pid = 0;
    std::wstring error;
};

// Layout centralization - single source of truth
struct Layout {
    RECT launch, inject, lang, fs, save, list;
    int footerTop, footerBottom, bannerTop, bannerBottom;
};
Layout ComputeLayout(int w, int h) {
    Layout l{};
    int btnWidth = (w - kPad * 2 - 12) / 2;
    l.launch = RECT{ kPad, 96, kPad + btnWidth, 140 };
    l.inject = RECT{ kPad + btnWidth + 12, 96, w - kPad, 140 };
    l.lang   = RECT{ w - 110, 16, w - 20, 44 };
    l.fs     = RECT{ w - 176, 16, w - 120, 44 };   // кнопка полноэкранного режима (слева от RU/EN)
    // list area строго ПОД заголовком секции "МОДЫ" (g_rcLaunch.bottom+36), не перекрывает кнопки
    int sectionBottom = l.launch.bottom + 36; // y=176 при дефолте
    l.list   = RECT{ kPad, sectionBottom + 8, w - kPad, h - kPad * 3 - 80 };
    l.footerTop = h - 136; l.footerBottom = h - 92;
    l.bannerTop = h - 92; l.bannerBottom = h - 68;
    l.save = RECT{ w - kPad - 200, h - 55, w - kPad, h - 17 };
    return l;
}
RECT ListRect() {
    return ComputeLayout(g_clientW, g_clientH).list;
}
inline void ClampScroll() {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    int listHeight = l.list.bottom - l.list.top;
    int totalModHeight = static_cast<int>(g_mods.size()) * kCardStep;
    int maxScroll = std::max(0, totalModHeight - listHeight);
    g_scroll = std::max(0, std::min(g_scroll, maxScroll));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void SetStatusKey(StatusKey key) {
    g_statusKey = key;
    g_statusCustom.clear();
    if (g_hwnd)
        InvalidateRect(g_hwnd, nullptr, TRUE);
}

void SetStatusCustom(const std::wstring& text) {
    g_statusKey = StatusKey::Custom;
    g_statusCustom = text;
    if (g_hwnd)
        InvalidateRect(g_hwnd, nullptr, TRUE);
}

std::wstring CurrentStatusText() {
    switch (g_statusKey) {
    case StatusKey::Ready:        return Str_StatusReady();
    case StatusKey::Injected:     return Str_StatusInjected();
    case StatusKey::NotFound:     return L10N(L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D\u0430",
                                              L"Game not found");
    case StatusKey::DllMissing:   return std::wstring(L"LuaAPI.dll ") +
                                       L10N(L"\u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D", L"not found");
    case StatusKey::InjectFail:   return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C",
                                              L"Injection failed");
    case StatusKey::GameNotFound: return std::wstring(L"gamemd.exe ") +
                                       L10N(L"\u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D", L"not found");
    case StatusKey::BusyLaunch:   return L10N(L"\u0417\u0430\u043F\u0443\u0441\u043A gamemd.exe...",
                                              L"Launching gamemd.exe...");
    case StatusKey::BusyInject:   return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435...",
                                              L"Injecting...");
    case StatusKey::SaveFail:     return L10N(L"\u041E\u0448\u0438\u0431\u043A\u0430 \u0437\u0430\u043F\u0438\u0441\u0438 active_mods.txt",
                                              L"Failed to write active_mods.txt");
    default:                      return g_statusCustom;
    }
}

COLORREF CurrentStatusColor() {
    switch (g_statusKey) {
    case StatusKey::Ready:
    case StatusKey::Injected:   return kOk;
    case StatusKey::BusyLaunch:
    case StatusKey::BusyInject: return kOrange;
    default:                    return g_appState == AppState::StatusError ? kRed : kDim;
    }
}

void SetStatus(const std::wstring& text) { SetStatusCustom(text); }

void FillRoundRect(HDC dc, const RECT& r, COLORREF fill, int radius, COLORREF outline = 0, bool hasOutline = false) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = hasOutline ? CreatePen(PS_SOLID, 1, outline)
                          : reinterpret_cast<HPEN>(GetStockObject(NULL_PEN));
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    if (hasOutline)
        DeleteObject(pen);
}

void DrawCircle(HDC dc, int cx, int cy, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = reinterpret_cast<HPEN>(GetStockObject(NULL_PEN));
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
}

void DrawTextR(HDC dc, const std::wstring& text, RECT rc, HFONT font, COLORREF color,
               UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rc, flags | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetExeDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true) {
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0)
            return L".";
        if (len < path.size() - 1 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            break;
        path.resize(path.size() * 2);
    }
    path.resize(len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring GetPrefsPath() { return GetExeDirectory() + L"\\injector.ini"; }
void LoadPrefs() {
    wchar_t buf[16]={0};
    GetPrivateProfileStringW(L"UI", L"lang", L"RU", buf, 16, GetPrefsPath().c_str());
    g_isRussian = (_wcsicmp(buf, L"EN") != 0);
}
void SavePrefs() {
    WritePrivateProfileStringW(L"UI", L"lang", g_isRussian ? L"RU" : L"EN", GetPrefsPath().c_str());
}
void ShowToast(const std::wstring& msg) {
    SetStatusCustom(msg);
    if (g_hwnd) {
        KillTimer(g_hwnd, kToastTimerId);
        SetTimer(g_hwnd, kToastTimerId, 2000, nullptr);
    }
}

void LogLine(const std::wstring& text) {
    std::wofstream log(GetExeDirectory() + L"\\injector_log.txt",
                       std::ios::app);
    SYSTEMTIME st;
    GetLocalTime(&st);
    log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"."
        << st.wMilliseconds << L"] " << text << L"\n";
}
DWORD FindTargetProcess() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kGameProcess) != 0)
                continue;
            HANDLE moduleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, entry.th32ProcessID);
            if (moduleSnap != INVALID_HANDLE_VALUE) {
                MODULEENTRY32W mod{};
                mod.dwSize = sizeof(mod);
                if (Module32FirstW(moduleSnap, &mod) &&
                    _wcsicmp(mod.szModule, kGameProcess) == 0) {
                    pid = entry.th32ProcessID;
                }
                CloseHandle(moduleSnap);
            }
            if (pid)
                break;
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

bool InjectDllIntoProcess(DWORD pid, const std::wstring& dllPath, std::wstring* error) {
    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!process) {
        if (error) *error = L"OpenProcess failed (error " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    // Unicode: use LoadLibraryW + wchar_t buffer to support Cyrillic install paths
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteBase = VirtualAllocEx(process, nullptr, bytes,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBase) {
        if (error) *error = L"VirtualAllocEx failed (error " + std::to_wstring(GetLastError()) + L")";
        CloseHandle(process);
        return false;
    }

    if (!WriteProcessMemory(process, remoteBase, dllPath.c_str(), bytes, nullptr)) {
        if (error) *error = L"WriteProcessMemory failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remoteBase, 0, nullptr);
    if (!thread) {
        if (error) *error = L"CreateRemoteThread failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(thread, 5000);
    if (waitResult == WAIT_TIMEOUT) {
        // Не считаем это успехом и не блокируем окно: target не ответил на LoadLibraryW.
        if (error) *error = L10N(
            L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u0437\u0430\u0432\u0438\u0441\u043B\u043E: \u0446\u0435\u043B\u0435\u0432\u043E\u0439 \u043F\u0440\u043E\u0446\u0435\u0441\u0441 \u043D\u0435 \u0437\u0430\u0433\u0440\u0443\u0437\u0438\u043B DLL \u0437\u0430 5000 \u043C\u0441",
            L"Injection timed out: target did not load DLL within 5000 ms (WAIT_TIMEOUT)");
        CloseHandle(thread);
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
    CloseHandle(process);

    if (exitCode == 0) {
        if (error) *error = L"LoadLibraryW returned NULL inside the target";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------


void DoFindGame() {
    g_gamePid = FindTargetProcess();
    g_gameName = g_gamePid ? kGameProcess : L"";
    if (g_gamePid == 0) {
        SetStatusKey(StatusKey::NotFound);
        MessageBoxW(g_hwnd,
                    L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430!\n\n"
                    L"\u0421\u043D\u0430\u0447\u0430\u043B\u0430 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 Yuri's Revenge (gamemd.exe).",
                    L"\u041F\u043E\u0438\u0441\u043A \u043F\u0440\u043E\u0446\u0435\u0441\u0441\u0430", MB_ICONWARNING | MB_OK);
    } else {
        SetStatusCustom(std::wstring(L"Target: gamemd.exe (PID: ") + std::to_wstring(g_gamePid) + L")");
    }
}

void DoInject() {
    if (g_gamePid == 0)
        DoFindGame();

    if (g_gamePid == 0)
        return;

    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    std::wstring error;
    if (InjectDllIntoProcess(g_gamePid, dllPath, &error)) {
        SetStatusKey(StatusKey::Injected);
        MessageBoxW(g_hwnd,
                    L"LuaAPI.dll \u0443\u0441\u043F\u0435\u0448\u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0435\u043D \u0432 \u0438\u0433\u0440\u0443!",
                    L"\u0423\u0441\u043F\u0435\u0445", MB_ICONINFORMATION | MB_OK);
    } else {
        SetStatusKey(StatusKey::InjectFail);
        MessageBoxW(g_hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + error).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
    }
}
void DoLaunchGameAsync(HWND hwnd);

void DoLaunchGame() {
    if (g_launching) return;
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    DWORD existing = FindTargetProcess();
    if (existing != 0) {
        g_gamePid = existing;
        g_gameName = kGameProcess;
        std::wstring error;
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, existing);
        if (!process) { SetStatus(L"OpenProcess failed"); return; }
        bool ok = InjectDllIntoProcess(existing, dllPath, &error);
        CloseHandle(process);
        if (ok) { g_appState = AppState::Injected; SetStatusKey(StatusKey::Injected); }
        else { g_appState = AppState::StatusError; SetStatusKey(StatusKey::InjectFail); }
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }
    // Async path: spawn thread, disable UI
    g_launching = true;
    g_appState = AppState::Busy;
    SetStatusKey(StatusKey::BusyLaunch);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    HWND hwnd = g_hwnd;
    std::thread([hwnd]() { DoLaunchGameAsync(hwnd); }).detach();
}

void DoLaunchGameAsync(HWND hwnd) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    std::wstring stubPath = exeDir + L"\\RA2MD.EXE";
    if (FileExists(stubPath)) {
        LogLine(L"Launching via RA2MD.EXE stub...");
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(stubPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else {
            LogLine(L"Stub launch failed, falling back to direct spawn");
        }
    }
    bool injected = false;
    std::wstring err;
    DWORD foundPid = 0;
    for (int i = 0; i < 600; ++i) {
        Sleep(200);
        DWORD pid = FindTargetProcess();
        if (!pid) continue;
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process) continue;
        bool ok = InjectDllIntoProcess(pid, dllPath, &err);
        CloseHandle(process);
        if (ok) { foundPid = pid; injected = true; LogLine(L"Injected into freshly spawned gamemd.exe"); }
        else { foundPid = pid; }
        break;
    }
    PostMessageW(hwnd, WM_APP_LAUNCH_DONE, (WPARAM)injected, (LPARAM)foundPid);
}

void DoInjectAttachAsync(HWND hwnd, DWORD pid, const std::wstring& dllPath) {
    std::wstring error;
    bool ok = InjectDllIntoProcess(pid, dllPath, &error);
    PostMessageW(hwnd, WM_APP_INJECT_DONE, 0,
                 reinterpret_cast<LPARAM>(new InjectResult{ ok, pid, error }));
}

void DoInjectAttach() {
    if (g_injecting) return;

    DWORD pid = FindTargetProcess();
    if (pid == 0) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::NotFound);
        MessageBoxW(g_hwnd,
                    L"gamemd.exe \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D.\n\n"
                    L"\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0438\u0433\u0440\u0443 \u043A\u043D\u043E\u043F\u043A\u043E\u0439 \u00AB\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C \u0438\u0433\u0440\u0443\u00BB.",
                    L"\u041F\u043E\u0438\u0441\u043A \u043F\u0440\u043E\u0446\u0435\u0441\u0441\u0430", MB_ICONWARNING | MB_OK);
        return;
    }

    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    g_gamePid = pid;
    g_injecting = true;
    g_appState = AppState::Busy;
    SetStatusKey(StatusKey::BusyInject);
    InvalidateRect(g_hwnd, nullptr, TRUE);

    // Async: работа внедрения уходит в отдельный поток, результат возвращается через
    // WM_APP_INJECT_DONE, чтобы окно не замерзало (InjectDllIntoProcess внутри имеет таймаут).
    HWND hwnd = g_hwnd;
    std::thread([hwnd, pid, dllPath]() { DoInjectAttachAsync(hwnd, pid, dllPath); }).detach();
}

// ---------------------------------------------------------------------------
// Mods
// ---------------------------------------------------------------------------

std::vector<std::wstring> LoadActiveModIds(const std::wstring& exeDir) {
    std::vector<std::wstring> ids;
    std::ifstream file(exeDir + L"\\scripts\\active_mods.txt");
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 && (unsigned char)line[0]==0xEF && (unsigned char)line[1]==0xBB && (unsigned char)line[2]==0xBF)
                line.erase(0,3);
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        if (line[start] == '#')
            continue;
        // trim end already done, extract id
        std::string id = line.substr(start);
        // trim trailing spaces inside id
        size_t end = id.find_last_not_of(" \t");
        if (end != std::string::npos) id = id.substr(0, end+1);
        ids.push_back(std::wstring(id.begin(), id.end()));
    }
    return ids;
}

std::wstring JsonGetString(const std::wstring& json, const wchar_t* key) {
    std::wstring pattern = std::wstring(L"\"") + key + L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos)
        return L"";
    size_t colon = json.find(L':', keyPos + pattern.size());
    size_t openQuote = json.find(L'"', colon);
    size_t closeQuote = json.find(L'"', openQuote + 1);
    if (colon == std::wstring::npos || openQuote == std::wstring::npos || closeQuote == std::wstring::npos)
        return L"";
    return json.substr(openQuote + 1, closeQuote - openQuote - 1);
}

std::vector<std::wstring> JsonGetStringArray(const std::wstring& json, const wchar_t* key) {
    std::vector<std::wstring> out;
    std::wstring pattern = std::wstring(L"\"") + key + L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos)
        return out;
    size_t openBracket = json.find(L'[', keyPos);
    size_t closeBracket = json.find(L']', openBracket == std::wstring::npos ? 0 : openBracket);
    if (openBracket == std::wstring::npos || closeBracket == std::wstring::npos || closeBracket <= openBracket)
        return out;

    std::wstring body = json.substr(openBracket + 1, closeBracket - openBracket - 1);
    size_t pos = 0;
    while ((pos = body.find(L'"', pos)) != std::wstring::npos) {
        size_t end = body.find(L'"', pos + 1);
        if (end == std::wstring::npos)
            break;
        out.push_back(body.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return out;
}

void ScanMods() {
    g_mods.clear();
    g_scroll = 0;
    std::wstring exeDir = GetExeDirectory();
    auto activeIds = LoadActiveModIds(exeDir);

    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((exeDir + L"\\scripts\\mods\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        ModEntry entry{};
        entry.dir = fd.cFileName;
        entry.id = fd.cFileName;

        std::wstring manifest = exeDir + L"\\scripts\\mods\\" + entry.dir + L"\\mod.json";
        if (FileExists(manifest)) {
            std::ifstream f(manifest);
            std::stringstream ss;
            ss << f.rdbuf();
            int size = MultiByteToWideChar(CP_UTF8, 0, ss.str().c_str(), -1, nullptr, 0);
            if (size > 0) {
                std::wstring wide(static_cast<size_t>(size), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, ss.str().c_str(), -1, &wide[0], size);
                wide.resize(size - 1);

                entry.id = JsonGetString(wide, L"id");
                entry.name = JsonGetString(wide, L"name");
                entry.version = JsonGetString(wide, L"version");
                entry.author = JsonGetString(wide, L"author");
                entry.description = JsonGetString(wide, L"description");
                entry.conflicts = JsonGetStringArray(wide, L"conflicts");
                entry.hasManifest = true;
                if (entry.id.empty())
                    entry.id = entry.dir;
                if (entry.name.empty())
                    entry.name = entry.id;
            }
        } else if (!FileExists(exeDir + L"\\scripts\\mods\\" + entry.dir + L"\\main.lua")) {
            continue;
        }

        if (entry.name.empty())
            entry.name = entry.id;
        if (entry.author.empty())
            entry.author = L"unknown";

        for (const auto& id : activeIds) {
            if (_wcsicmp(entry.id.c_str(), id.c_str()) == 0) {
                entry.enabled = true;
                break;
            }
        }

        g_mods.push_back(entry);
    } while (FindNextFileW(find, &fd));

    FindClose(find);

    // Уважаем пользовательский порядок, сохранённый в active_mods.txt: порядок строк файла
    // = порядок включённых модов после реордера. Моды из файла идут первыми — в их порядке,
    // прочие (новые / выключенные) — после, в файловом (алфавитном) порядке, как раньше.
    if (!activeIds.empty()) {
        std::vector<ModEntry> ordered;
        std::vector<bool> used(g_mods.size(), false);
        for (const auto& id : activeIds) {
            for (size_t i = 0; i < g_mods.size(); ++i) {
                if (!used[i] && _wcsicmp(g_mods[i].id.c_str(), id.c_str()) == 0) {
                    ordered.push_back(std::move(g_mods[i]));
                    used[i] = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < g_mods.size(); ++i) {
            if (!used[i]) ordered.push_back(std::move(g_mods[i]));
        }
        g_mods = std::move(ordered);
    }
}

void SaveMods() {
    std::wstring exeDir = GetExeDirectory();
    std::wofstream out(exeDir + L"\\scripts\\active_mods.txt", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::SaveFail);
        MessageBoxW(g_hwnd, L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0437\u0430\u043F\u0438\u0441\u0430\u0442\u044C active_mods.txt",
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    out << L"# LuaAPI active mods - one mod ID per line\n";
    int saved = 0;
    for (const auto& m : g_mods) {
        if (m.enabled) {
            out << m.id << L"\n";
            ++saved;
        }
    }
    out.flush();
    out.close();
    g_dirty = false;
    g_appState = AppState::Ready;
    ShowToast(L10N(L"\u2713 \u0421\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u043E", L"\u2713 Saved"));
    // toast timer will revert to Ready after 2s, just invalidate now
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

int EnabledModCount() {
    int n = 0;
    for (const auto& m : g_mods)
        if (m.enabled) ++n;
    return n;
}

std::vector<std::pair<size_t, size_t>> DetectConflicts() {
    std::vector<std::pair<size_t, size_t>> hits;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!g_mods[i].enabled) continue;
        for (size_t j = i + 1; j < g_mods.size(); ++j) {
            if (!g_mods[j].enabled) continue;
            bool conflict = false;
            for (const auto& c : g_mods[i].conflicts) if (_wcsicmp(c.c_str(), g_mods[j].id.c_str())==0) { conflict = true; break; }
            if (!conflict) for (const auto& c : g_mods[j].conflicts) if (_wcsicmp(c.c_str(), g_mods[i].id.c_str())==0) { conflict = true; break; }
            if (conflict) hits.emplace_back(i, j);
        }
    }
    return hits;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------


COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(GetRValue(a) + static_cast<int>((GetRValue(b) - GetRValue(a)) * t),
               GetGValue(a) + static_cast<int>((GetGValue(b) - GetGValue(a)) * t),
               GetBValue(a) + static_cast<int>((GetBValue(b) - GetBValue(a)) * t));
}

void PaintAll(HDC dc) {
    RECT full{ 0, 0, g_clientW, g_clientH };
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &full, bg);
    DeleteObject(bg);

    int w = g_clientW;

    // ---- Header ----
    DrawTextR(dc, L"RED ALERT 2 - LUA ENGINE",
              RECT{kPad, 14, w - 186, 42}, g_fontTitle, kText);

    // Fullscreen toggle (top right, слева от RU/EN)
    FillRoundRect(dc, g_rcFs, g_hoverFs ? kHover : kSurface, 14);
    DrawTextR(dc, L"\u26F6", g_rcFs, g_fontSmall,
              g_hoverFs ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Language toggle (top right)
    FillRoundRect(dc, g_rcLang, g_hoverLang ? kHover : kSurface, 14);
    DrawTextR(dc, L"RU / EN", g_rcLang, g_fontSmall,
              g_hoverLang ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawTextR(dc, Str_Subtitle(), RECT{kPad, 44, w - kPad, 64}, g_fontSmall, kDim);

    // ---- Status row ----
    {
        int dotX = kPad + 6;
        int cy = 82;
        DrawCircle(dc, dotX, cy, 5, CurrentStatusColor());
        DrawTextR(dc, CurrentStatusText(), RECT{kPad + 18, cy - 12, w - kPad, cy + 12}, g_fontReg, kText);
    }

    // ---- Action buttons ----
    {
        bool launchEnabled = FileExists(GetExeDirectory() + L"\\gamemd.exe");
        COLORREF fill = !launchEnabled ? kBadge
                      : (g_hoverLaunch ? LerpColor(kRed, kText, 0.15f) : kRed);
        FillRoundRect(dc, g_rcLaunch, fill, 10);
        DrawTextR(dc, Str_LaunchBtn(), g_rcLaunch, g_fontHeader, kText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        COLORREF blueFill = g_injecting ? kBadge : (g_hoverInject ? LerpColor(kBlue, kText, 0.15f) : kBlue);
        FillRoundRect(dc, g_rcInject, blueFill, 10);
        DrawTextR(dc, Str_InjectBtn(), g_rcInject, g_fontHeader, g_injecting ? kDim : kText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ---- Section label ----
    DrawTextR(dc, Str_ModsHeader(), RECT{kPad, g_rcLaunch.bottom + 16, 260, g_rcLaunch.bottom + 36},
              g_fontSmall, kDim);

    // ---- Mod cards ---- (СТРОГО внутри маски списка)
    Layout l = ComputeLayout(g_clientW, g_clientH);
    // Сохраняем контекст и ставим жесткую маску отсечения по границам списка
    int savedDC = SaveDC(dc);
    IntersectClipRect(dc, l.list.left, l.list.top, l.list.right, l.list.bottom);

    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(g_hwnd, &cursor);

    // Empty state UX: show hint when no mods detected (внутри маски)
    if (g_mods.empty()) {
        DrawTextR(dc, L10N(L"Моды не найдены — поместите папки в scripts/mods/", L"No mods found — place folders in scripts/mods/"),
                  RECT{kPad, l.list.top + 20, g_clientW - kPad, l.list.top + 44}, g_fontSmall, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Отрисовка карточек модов с учетом скролла (kCardH=76, kCardGap=8, шаг 84, отступ под скроллбар)
    int listH = l.list.bottom - l.list.top;
    int totalH = static_cast<int>(g_mods.size()) * kCardStep;
    int cardW = (l.list.right - l.list.left) - (totalH > listH ? (kScrollW + 8) : 0);
    int yPos = l.list.top + 4 - g_scroll;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        auto& m = g_mods[i];
        // Рисуем только если карточка попадает в видимую область списка
        if (yPos + kCardH >= l.list.top && yPos <= l.list.bottom) {
            RECT rcCard = { l.list.left, yPos, l.list.left + cardW, yPos + kCardH };
            RECT card = rcCard; // для hover/клика
            bool hovered = PtInRect(&card, cursor);
            bool isDrag = g_dragState.dragging && g_dragState.dragIndex == static_cast<int>(i);

            FillRoundRect(dc, card, isDrag ? kBlue : (hovered ? kHover : kSurface), 10,
                          m.enabled ? kGreen : kBadge, m.enabled);

            RECT box{ rcCard.left + 12, yPos + 16, rcCard.left + 32, yPos + 36 };
            FillRoundRect(dc, box, m.enabled ? kGreen : kBg, 4, m.enabled ? kGreen : kBadge, true);
            if (m.enabled) {
                HFONT old = static_cast<HFONT>(SelectObject(dc, g_fontReg));
                SetTextColor(dc, kText);
                SetBkMode(dc, TRANSPARENT);
                if (m.enabled) {
                    HPEN pen = CreatePen(PS_SOLID, 2, kText);
                    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
                    MoveToEx(dc, box.left + 4,  box.top + 10, nullptr);
                    LineTo(dc,   box.left + 8,  box.top + 14);
                    LineTo(dc,   box.left + 16, box.top + 5);
                    SelectObject(dc, old);
                    DeleteObject(pen);
                }
                SelectObject(dc, old);
            }

            int tx = rcCard.left + 42;
            DrawTextR(dc, m.name, RECT{tx, yPos + 10, tx + 200, yPos + 30}, g_fontHeader, kText);

            std::wstring badge = L"[v" + m.version + L"]";
            HFONT measureFont = static_cast<HFONT>(SelectObject(dc, g_fontSmall));
            SIZE sz{};
            GetTextExtentPoint32W(dc, badge.c_str(), static_cast<int>(badge.size()), &sz);
            SelectObject(dc, measureFont);
            RECT badgeRc{ tx + 200, yPos + 10, tx + 208 + sz.cx, yPos + 30 };
            FillRoundRect(dc, badgeRc, kBadge, 6);
            DrawTextR(dc, badge, badgeRc, g_fontSmall, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            DrawTextR(dc, L"by " + m.author, RECT{badgeRc.right + 8, yPos + 10, rcCard.right - 10, yPos + 30},
                      g_fontSmall, kDim);

            DrawTextR(dc, m.description, RECT{rcCard.left + 40, yPos + 36, rcCard.right - 12, yPos + 68},
                      g_fontSmall, kDim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        yPos += kCardStep;
    }

    // Восстанавливаем контекст (снимаем маску отсечения)
    RestoreDC(dc, savedDC);

// ---- Conflict banner ----
    auto conflicts = DetectConflicts();
    if (!conflicts.empty()) {
        std::wstring warning;
        for (size_t k = 0; k < conflicts.size(); ++k) {
            warning += L"! " + g_mods[conflicts[k].first].name +
                       L" vs " + g_mods[conflicts[k].second].name;
            if (k + 1 < conflicts.size())
                warning += L";  ";
        }
        Layout bl = ComputeLayout(g_clientW, g_clientH);
        int bannerTop = bl.bannerTop;
        int bannerBottom = bl.bannerBottom;
        int listBottom = bl.list.bottom;
        if (bannerTop < listBottom + 8) { // ensure gap from list
            bannerTop = listBottom + 8;
            bannerBottom = bannerTop + 24;
        }
        int modSectionTop = bl.launch.bottom + 16;
        if (bannerTop < modSectionTop) {
            bannerTop = modSectionTop;
            bannerBottom = bannerTop + 24;
        }
        // clamp banner inside footer area
        if (bannerBottom > g_clientH - kPad - 38) {
            bannerBottom = g_clientH - kPad - 38;
            bannerTop = bannerBottom - 24;
        }
        DrawTextR(dc, warning, RECT{kPad, bannerTop, w - kPad, bannerBottom}, g_fontSmall, kOrange);
    }

    // ---- Footer ----
    Layout fl = ComputeLayout(g_clientW, g_clientH);
    DrawTextR(dc, Str_ActiveCount(EnabledModCount(), static_cast<int>(g_mods.size())),
              RECT{kPad, fl.footerTop, 280, fl.footerBottom}, g_fontReg, kDim);

    // Save button is already computed in ComputeLayout and synced in RecalcLayout/Clamp
    // Apply same clamping as layout to handle banner overlap, then sync global
    {
        Layout sl = ComputeLayout(g_clientW, g_clientH);
        int saveTop = sl.save.top, saveBottom = sl.save.bottom;
        int bannerBottom = sl.bannerBottom;
        if (saveTop < bannerBottom + 8) {
            saveTop = bannerBottom + 8;
            saveBottom = saveTop + 38;
        }
        if (saveBottom > g_clientH - kPad) {
            saveBottom = g_clientH - kPad;
            saveTop = saveBottom - 38;
        }
        g_rcSave = RECT{ g_clientW - kPad - 200, saveTop, g_clientW - kPad, saveBottom };
    }
    // Dim save button when launching
    COLORREF saveFill = g_launching ? kBadge : (g_hoverSave ? kGreenHover : kGreen);
    if (g_launching) {
        // still draw but with disabled look
    }
    FillRoundRect(dc, g_rcSave, saveFill, 10);
    DrawTextR(dc, Str_SaveBtn(), g_rcSave, g_fontHeader, g_launching ? kDim : kText,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ---- Minimal scrollbar (kScrollW=8, dark theme) - строго в диапазоне l.list.top..bottom
    {
        Layout l = ComputeLayout(g_clientW, g_clientH);
        int listHeight = l.list.bottom - l.list.top;
        int totalModHeight = static_cast<int>(g_mods.size()) * kCardStep;
        if (totalModHeight > listHeight) {
            int maxScroll = totalModHeight - listHeight;
            int trackH = listHeight - 8;
            int trackX = l.list.right - kScrollW;
            int trackY = l.list.top + 4;
            // track (ширина kScrollW)
            FillRoundRect(dc, RECT{trackX, trackY, trackX + kScrollW, trackY+trackH}, kBadge, 3);
            // thumb
            int thumbH = std::max(20, trackH * listHeight / totalModHeight);
            int thumbY = trackY + (maxScroll ? (g_scroll * (trackH - thumbH) / maxScroll) : 0);
            FillRoundRect(dc, RECT{trackX, thumbY, trackX + kScrollW, thumbY+thumbH}, kDim, 3);
        }
    }
}

// ---------------------------------------------------------------------------
// Hit testing / interaction
// ---------------------------------------------------------------------------

bool PointIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

// Возвращает индекс карточки мода под курсором (с учётом скролла) или -1.
int CardIndexAt(POINT pt) {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    if (pt.y < l.list.top || pt.y > l.list.bottom) return -1;
    int listH = l.list.bottom - l.list.top;
    int totalH = static_cast<int>(g_mods.size()) * kCardStep;
    int cardW = (l.list.right - l.list.left) - (totalH > listH ? (kScrollW + 8) : 0);
    int yPos = l.list.top + 4 - g_scroll;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        RECT rcCard = { l.list.left, yPos, l.list.left + cardW, yPos + kCardH };
        if (PointIn(rcCard, pt)) return static_cast<int>(i);
        yPos += kCardStep;
    }
    return -1;
}

POINT CursorInClient() {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(g_hwnd, &p);
    return p;
}

void OnLeftDown(POINT pt) {
    if (PointIn(g_rcLang, pt)) {
        g_isRussian = !g_isRussian;
        SavePrefs();
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }
    if (PointIn(g_rcFs, pt)) {
        ToggleFullscreen();
        return;
    }
    if (g_launching || g_injecting) return; // disable clicks while busy
    if (PointIn(g_rcLaunch, pt)) { DoLaunchGame(); return; }
    if (PointIn(g_rcInject, pt)) { DoInjectAttach(); return; }
    if (PointIn(g_rcSave, pt)) { SaveMods(); return; }

    // Клик по карточке: держим захват мыши, чтобы отличить обычный клик (тоггл чекбокса)
    // от перетаскивания (сдвиг > 6px) в WM_MOUSEMOVE.
    int idx = CardIndexAt(pt);
    if (idx >= 0) {
        g_dragState.pendingClick = true;
        g_dragState.pendingIndex = idx;
        g_dragState.downPos = pt;
        g_dragState.dragging = false;
        g_dragState.dragIndex = -1;
        SetCapture(g_hwnd);
    }
}

} // namespace


void RecalcLayout() {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    g_rcLaunch = l.launch;
    g_rcInject = l.inject;
    g_rcLang = l.lang;
    g_rcFs = l.fs;
    g_rcSave = l.save;
    // keep clamped
    ClampScroll();
}

// Переключение полноэкранного режима (borderless). Сохраняет окно-геометрию при входе
// и восстанавливает её при выходе. Клиентская область обновляется через WM_SIZE.
void ToggleFullscreen() {
    if (!g_hwnd) return;

    if (!g_fullscreen) {
        GetWindowRect(g_hwnd, &g_windowedRect);
        // Borderless: только WS_POPUP, без WS_EX_TOPMOST (он накрывал бы окна других
        // приложений, даже без фокуса). Разворачиваемся в рабочую область монитора под окном
        // (rcWork — без таскбара), а не в весь экран, чтобы таскбар остался видимой.
        LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
        SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);

        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            const RECT& w = mi.rcWork;
            SetWindowPos(g_hwnd, HWND_TOP, w.left, w.top,
                         w.right - w.left, w.bottom - w.top,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            // Fallback на весь экран, если не удалось получить информацию о мониторе.
            SetWindowPos(g_hwnd, HWND_TOP, 0, 0,
                         GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        g_fullscreen = true;
    } else {
        LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
        SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) |
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME);
        SetWindowPos(g_hwnd, HWND_TOP,
                     g_windowedRect.left, g_windowedRect.top,
                     g_windowedRect.right - g_windowedRect.left,
                     g_windowedRect.bottom - g_windowedRect.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
        g_fullscreen = false;
    }
    RecalcLayout();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        LoadPrefs();
        RECT rc; GetClientRect(hwnd, &rc);
        g_clientW = rc.right - rc.left; g_clientH = rc.bottom - rc.top;
        if (g_clientW == 0) g_clientW = kDefaultClientW;
        if (g_clientH == 0) g_clientH = kDefaultClientH;
        RecalcLayout();
        ScanMods();
        SetStatusKey(StatusKey::Ready);
        return 0;
    }
    case WM_SIZE:
        g_clientW = LOWORD(lParam);
        g_clientH = HIWORD(lParam);
        RecalcLayout();
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F11) {
            ToggleFullscreen();
            return 0;
        }
        if (wParam == VK_ESCAPE && g_fullscreen) {
            ToggleFullscreen();
            return 0;
        }
        break; // не обработанные клавиши — в DefWindowProc, а не молча глотать
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        // ---- Drag-and-drop reorder of mod cards ----
        if (g_dragState.pendingClick || g_dragState.dragging) {
            if (g_dragState.pendingClick) {
                long adx = pt.x - g_dragState.downPos.x; adx = adx < 0 ? -adx : adx;
                long ady = pt.y - g_dragState.downPos.y; ady = ady < 0 ? -ady : ady;
                if (adx > 6 || ady > 6) {
                    g_dragState.dragging = true;
                    g_dragState.pendingClick = false;
                    g_dragState.dragIndex = g_dragState.pendingIndex;
                    g_dragState.dragAnchorY = pt.y;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            if (g_dragState.dragging) {
                int dy = pt.y - g_dragState.dragAnchorY;
                if (dy >= kCardStep / 2 && g_dragState.dragIndex + 1 < static_cast<int>(g_mods.size())) {
                    std::swap(g_mods[g_dragState.dragIndex], g_mods[g_dragState.dragIndex + 1]);
                    g_dragState.dragIndex += 1;
                    g_dragState.dragAnchorY += kCardStep;
                    InvalidateRect(hwnd, nullptr, TRUE);
                } else if (dy <= -kCardStep / 2 && g_dragState.dragIndex - 1 >= 0) {
                    std::swap(g_mods[g_dragState.dragIndex], g_mods[g_dragState.dragIndex - 1]);
                    g_dragState.dragIndex -= 1;
                    g_dragState.dragAnchorY -= kCardStep;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        bool hL = (!g_launching && !g_injecting) && PointIn(g_rcLaunch, pt);
        bool hI = (!g_launching && !g_injecting) && PointIn(g_rcInject, pt);
        bool hS = (!g_launching && !g_injecting) && PointIn(g_rcSave, pt);
        bool hG = PointIn(g_rcLang, pt);
        bool hF = PointIn(g_rcFs, pt);
        bool overList = false;
        // Only invalidate overList if it changes hover state of cards - throttle
        if ((hL != g_hoverLaunch) || (hI != g_hoverInject) ||
            (hS != g_hoverSave) || (hG != g_hoverLang) || (hF != g_hoverFs)) {
            g_hoverLaunch = hL;
            g_hoverInject = hI;
            g_hoverSave = hS;
            g_hoverLang = hG;
            g_hoverFs = hF;
            InvalidateRect(hwnd, nullptr, TRUE);
        } else {
            // check card hover without invalidating every move: only if card under cursor changed
            static int lastHoverIdx = -1;
            int idx = -1;
            Layout hl = ComputeLayout(g_clientW, g_clientH);
            int hListH = hl.list.bottom - hl.list.top;
            int hTotalH = (int)g_mods.size() * kCardStep;
            int hCardW = (hl.list.right - hl.list.left) - (hTotalH > hListH ? (kScrollW + 8) : 0);
            int yPos2 = hl.list.top + 4 - g_scroll;
            for (size_t i=0;i<g_mods.size();++i){ RECT card{ hl.list.left, yPos2, hl.list.left + hCardW, yPos2 + kCardH }; if (PointIn(card, pt)) { idx=(int)i; break; } yPos2+=kCardStep; }
            if (idx != lastHoverIdx) { lastHoverIdx = idx; InvalidateRect(hwnd, nullptr, TRUE); }
        }
        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_trackingMouse = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        g_hoverLaunch = g_hoverInject = g_hoverSave = g_hoverLang = g_hoverFs = false;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        // high-res wheel: use delta directly, 40px per notch
        g_scroll -= delta * 40 / WHEEL_DELTA;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        OnLeftDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;
    }
    case WM_LBUTTONUP: {
        bool pending = g_dragState.pendingClick;
        bool dragging = g_dragState.dragging;
        if (pending && !dragging) {
            int idx = g_dragState.pendingIndex;
            if (idx >= 0 && idx < static_cast<int>(g_mods.size())) {
                g_mods[idx].enabled = !g_mods[idx].enabled;
                g_dirty = true;
            }
        }
        if (dragging) {
            g_dirty = true; // произошёл реордер — помечаем к сохранению
        }
        g_dragState = DragState{};
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) {
            g_dragState = DragState{};
        }
        return 0;
    case WM_ERASEBKGND:
        return 1; // all painting is double-buffered; kill resize flicker
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, g_clientW, g_clientH);
        auto oldBmp = SelectObject(mem, bmp);

        PaintAll(mem);

        BitBlt(hdc, 0, 0, g_clientW, g_clientH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_TIMER:
        if (wParam == kToastTimerId) {
            KillTimer(hwnd, kToastTimerId);
            SetStatusKey(StatusKey::Ready);
        }
        return 0;
    case WM_APP_LAUNCH_DONE: {
        g_launching = false;
        bool ok = (bool)wParam;
        DWORD pid = (DWORD)lParam;
        if (ok && pid) {
            g_gamePid = pid; g_gameName = kGameProcess;
            g_appState = AppState::Injected; SetStatusKey(StatusKey::Injected);
        } else if (pid) {
            g_gamePid = pid; g_appState = AppState::StatusError; SetStatusKey(StatusKey::InjectFail);
        } else {
            g_appState = AppState::StatusError; SetStatusKey(StatusKey::NotFound);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_APP_INJECT_DONE: {
        g_injecting = false;
        auto* res = reinterpret_cast<InjectResult*>(lParam);
        bool ok = res ? res->ok : false;
        DWORD pid = res ? res->pid : 0;
        std::wstring err = res ? res->error : L"";
        delete res;
        if (ok && pid) {
            g_gamePid = pid; g_gameName = kGameProcess;
            g_appState = AppState::Injected; SetStatusKey(StatusKey::Injected);
            MessageBoxW(hwnd,
                        L"LuaAPI.dll \u0443\u0441\u043F\u0435\u0448\u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0435\u043D \u0432 \u0438\u0433\u0440\u0443!",
                        L"\u0423\u0441\u043F\u0435\u0445", MB_ICONINFORMATION | MB_OK);
        } else {
            g_appState = AppState::StatusError; SetStatusKey(StatusKey::InjectFail);
            MessageBoxW(hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + err).c_str(),
                        L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE: {
        if (g_dirty) {
            // silent auto-save per spec (no MessageBox)
            SaveMods();
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DPICHANGED: {
        // wParam loword = new DPI x, hiword = y
        RECT* const prc = reinterpret_cast<RECT*>(lParam);
        // Recreate fonts scaled to new DPI
        int dpi = HIWORD(wParam);
        if (dpi==0) dpi=96;
        DeleteObject(g_fontTitle); DeleteObject(g_fontHeader); DeleteObject(g_fontReg); DeleteObject(g_fontSmall);
        g_fontTitle = CreateFontW(-MulDiv(16, dpi, 72), 0,0,0,FW_BOLD, FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"Segoe UI");
        g_fontHeader= CreateFontW(-MulDiv(11, dpi, 72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"Segoe UI");
        g_fontReg   = CreateFontW(-MulDiv(12, dpi, 72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"Segoe UI");
        g_fontSmall = CreateFontW(-MulDiv(10, dpi, 72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"Segoe UI");
        g_clientW = prc->right - prc->left; g_clientH = prc->bottom - prc->top;
        RecalcLayout(); ClampScroll();
        SetWindowPos(hwnd, nullptr, prc->left, prc->top, g_clientW, g_clientH, SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kToastTimerId);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

namespace {


} // namespace

// Headless diagnostic launch: injector.exe --noinject
// Spawns gamemd.exe (no LuaAPI.dll), waits for exit, logs to injector_log.txt.
int RunNoInjectDiagnostic() {
    LogLine(L"--- Diagnostic (--noinject): pure vanilla launch ---");
    std::wstring exeDir = GetExeDirectory();
    std::wstring gamePath = exeDir + L"\\gamemd.exe";
    if (!FileExists(gamePath)) {
        LogLine(L"Diagnostics: gamemd.exe not found");
        MessageBoxW(nullptr, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + gamePath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return 1;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(gamePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, exeDir.c_str(), &si, &pi)) {
        LogLine(L"Diagnostics: CreateProcessW FAILED");
        return 1;
    }
    LogLine(L"Diagnostics: spawned suspended, resuming...");
    ResumeThread(pi.hThread);

    DWORD startTick = GetTickCount64();
    WaitForSingleObject(pi.hProcess, 30000);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    DWORD elapsed = GetTickCount64() - startTick;
    wchar_t b[16];
    swprintf(b, 16, L"%08X", code);
    LogLine(std::wstring(L"Diagnostics: exited code=0x") + b +
            L" after " + std::to_wstring(elapsed) + L" ms");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // High-DPI awareness: Per-Monitor DPI Aware V2 (fallback to system DPI aware)
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *SetDPIAwarenessContext)(HANDLE);
        SetDPIAwarenessContext setDPI = (SetDPIAwarenessContext)GetProcAddress(shcore, "SetProcessDpiAwarenessContext");
        if (setDPI) {
            setDPI(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            // Fallback: SetProcessDPIAware
            SetProcessDPIAware();
        }
        FreeLibrary(shcore);
    } else {
        SetProcessDPIAware();
    }

    // Headless diagnostic / compatibility modes
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--noinject") == 0) {
                g_skipInjection = true;
                g_headless = true;
            }
            if (_wcsicmp(argv[i], L"--withcncnet") == 0) {
                g_injectCnCNet = true;
                g_headless = true;
            }
        }
        if (argv) LocalFree(argv);

        if (g_headless) {
            LogLine(L"--- Headless launch started ---");
            DoLaunchGame();
            // Wait for the game process to exit, then cleanly exit the injector.
            // Use SYNCHRONIZE so we can wait without needing PROCESS_TERMINATE rights.
            if (g_gamePid != 0) {
                HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, g_gamePid);
                if (!hProcess) hProcess = OpenProcess(SYNCHRONIZE, FALSE, g_gamePid);
                if (hProcess) {
                    LogLine(L"Headless: waiting for gamemd.exe (PID " + std::to_wstring(g_gamePid) + L") to exit...");
                    WaitForSingleObject(hProcess, INFINITE);
                    CloseHandle(hProcess);
                    LogLine(L"Headless: game exited, injector terminating");
                } else {
                    LogLine(L"Headless: OpenProcess failed, cannot wait for game exit");
                }
            } else {
                LogLine(L"Headless: g_gamePid == 0, game never appeared");
            }
            // Give a small grace period after game exit for log flush
            Sleep(500);
            return 0;
        }
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);

    // Compute window size from desired client area (580 x 640).
    RECT rc{ 0, 0, kDefaultClientW, kDefaultClientH };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    int wndW = rc.right - rc.left;
    int wndH = rc.bottom - rc.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - wndW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - wndH) / 2;

    g_hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
                             x, y, wndW, wndH,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd)
        return 1;

    g_fontTitle = CreateFontW(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontHeader = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontReg = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_fontTitle);
    DeleteObject(g_fontHeader);
    DeleteObject(g_fontReg);
    DeleteObject(g_fontSmall);
    return static_cast<int>(msg.wParam);
}
