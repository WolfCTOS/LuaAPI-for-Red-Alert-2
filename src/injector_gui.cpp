// RA2 Yuri's Revenge Р В Р вЂ Р В РІР‚С™Р Р†Р вЂљРЎСљ LuaAPI Injector (modern dark Win32 GUI, no console)
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
bool g_hoverLaunch = false;
bool g_hoverInject = false;
bool g_hoverSave = false;
bool g_hoverLang = false;
bool g_trackingMouse = false;
bool g_headless = false;

// Layout metrics recomputed in RecalcLayout.
// Maximum visible mod area before scrollbar appears
const int kMaxModArea = 400;

RECT ListRect() {
    return RECT{ kPad, kPad * 2, g_clientW - kPad, g_clientH - kPad * 3 - 80 };
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

    int pathBytes = WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrowPath(static_cast<size_t>(pathBytes), '\0');
    WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, &narrowPath[0], pathBytes, nullptr, nullptr);

    void* remoteBase = VirtualAllocEx(process, nullptr, narrowPath.size(),
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBase) {
        if (error) *error = L"VirtualAllocEx failed (error " + std::to_wstring(GetLastError()) + L")";
        CloseHandle(process);
        return false;
    }

    if (!WriteProcessMemory(process, remoteBase, narrowPath.c_str(), narrowPath.size(), nullptr)) {
        if (error) *error = L"WriteProcessMemory failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    auto loadLibraryA = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryA, remoteBase, 0, nullptr);
    if (!thread) {
        if (error) *error = L"CreateRemoteThread failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
    CloseHandle(process);

    if (exitCode == 0) {
        if (error) *error = L"LoadLibraryA returned NULL inside the target";
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
void DoLaunchGame() {
    std::wstring exeDir = GetExeDirectory();
    std::wstring gamePath = exeDir + L"\\gamemd.exe";
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    std::wstring stubPath = exeDir + L"\\RA2MD.EXE";

    if (!FileExists(dllPath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    // If the game is already running, just inject.
    DWORD existing = FindTargetProcess();
    if (existing != 0) {
        g_gamePid = existing;
        g_gameName = kGameProcess;

        std::wstring error;
        HANDLE process = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, existing);
        if (process == nullptr) {
            SetStatus(L"OpenProcess failed");
            return;
        }
        bool ok = InjectDllIntoProcess(existing, dllPath, &error);
        CloseHandle(process);
        if (ok) SetStatus(L"LuaAPI.dll \u0432\u043D\u0435\u0434\u0440\u0451\u043D \u0443\u0441\u043F\u0435\u0448\u043D\u043E!");
        else SetStatus(L"\u041E\u0448\u0438\u0431\u043A\u0430 \u0432\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u044F");
        return;
    }

    // Launch through the native stub loader - it prepares the environment
    // (CD-check bypass etc.) that a direct gamemd.exe start lacks.
    if (FileExists(stubPath)) {
        LogLine(L"Launching via RA2MD.EXE stub...");
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(stubPath.c_str(), nullptr, nullptr, nullptr, FALSE,
                           0, nullptr, exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            LogLine(L"Stub launch failed, falling back to direct spawn");
        }
    }

    // Wait for the real game process to appear, then inject immediately.
    SetStatus(L"\u0416\u0434\u0451\u043C \u0437\u0430\u043F\u0443\u0441\u043A\u0430 gamemd.exe...");
    for (int i = 0; i < 600; ++i) { // up to 120 seconds
        Sleep(200);
        DWORD pid = FindTargetProcess();
        if (pid == 0)
            continue;

        std::wstring error;
        HANDLE process = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, pid);
        if (!process)
            continue;

        bool ok = InjectDllIntoProcess(pid, dllPath, &error);
        CloseHandle(process);

        if (ok) {
            g_gamePid = pid;
            g_gameName = kGameProcess;
            SetStatus(L"LuaAPI \u0432\u043D\u0435\u0434\u0440\u0451\u043D \u0432 \u0438\u0433\u0440\u0443!");
            LogLine(L"Injected into freshly spawned gamemd.exe");
        }
        break;
    }
}

void DoInjectAttach() {
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

    g_gamePid = pid;
    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    std::wstring error;
    g_appState = AppState::Busy;
    SetStatusKey(StatusKey::BusyInject);
    if (InjectDllIntoProcess(pid, dllPath, &error)) {
        g_appState = AppState::Injected;
        SetStatusKey(StatusKey::Injected);
    } else {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::InjectFail);
        MessageBoxW(g_hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + error).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
    }
}

// ---------------------------------------------------------------------------
// Mods
// ---------------------------------------------------------------------------

std::vector<std::wstring> LoadActiveModIds(const std::wstring& exeDir) {
    std::vector<std::wstring> ids;
    std::ifstream file(exeDir + L"\\scripts\\active_mods.txt");
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        size_t start = line.find_first_not_of(' ');
        if (start == std::string::npos)
            continue;
        if (line[start] == '#')
            continue;
        ids.push_back(std::wstring(line.begin() + start, line.end()));
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

        for (const auto& id : LoadActiveModIds(exeDir)) {
            if (_wcsicmp(entry.id.c_str(), id.c_str()) == 0) {
                entry.enabled = true;
                break;
            }
        }

        g_mods.push_back(entry);
    } while (FindNextFileW(find, &fd));

    FindClose(find);
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

    g_appState = AppState::Ready;
    SetStatusKey(StatusKey::Ready);
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
        if (!g_mods[i].enabled)
            continue;
        for (size_t j = 0; j < g_mods.size(); ++j) {
            if (i == j || !g_mods[j].enabled)
                continue;
            for (const auto& c : g_mods[i].conflicts) {
                if (_wcsicmp(c.c_str(), g_mods[j].id.c_str()) == 0)
                    hits.emplace_back(i, j);
            }
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
              RECT{kPad, 14, w - 130, 42}, g_fontTitle, kText);

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

        COLORREF blueFill = g_hoverInject ? LerpColor(kBlue, kText, 0.15f) : kBlue;
        FillRoundRect(dc, g_rcInject, blueFill, 10);
        DrawTextR(dc, Str_InjectBtn(), g_rcInject, g_fontHeader, kText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ---- Section label ----
    DrawTextR(dc, Str_ModsHeader(), RECT{kPad, g_rcLaunch.bottom + 16, 260, g_rcLaunch.bottom + 36},
              g_fontSmall, kDim);

    // ---- Mod cards ----
    RECT list = ListRect();
    SaveDC(dc);
    IntersectClipRect(dc, list.left, list.top, list.right, list.bottom);

    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(g_hwnd, &cursor);

    // Compute usable mod area height, accounting for scroll
    int listHeight = ListRect().bottom - ListRect().top;
    int totalModHeight = static_cast<int>(g_mods.size()) * 70; // 62 + 8 padding
    int maxScroll = std::max(0, totalModHeight - listHeight);
    g_scroll = std::max(0, std::min(g_scroll, maxScroll));

    int yPos = ListRect().top + 4 - g_scroll;
    for (auto& m : g_mods) {
        // Stop drawing if we've moved past the visible area
        if (yPos > ListRect().bottom + 70) break;

        RECT card{ kPad, yPos, ListRect().right - kPad, yPos + 62 };
        // Clamp card to list rectangle
        if (card.right > ListRect().right) card.right = ListRect().right;
        if (card.bottom > ListRect().bottom) card.bottom = ListRect().bottom;
        bool hovered = PtInRect(&card, cursor);

        FillRoundRect(dc, card, hovered ? kHover : kSurface, 10,
                      m.enabled ? kGreen : kBadge, m.enabled);

        RECT box{ card.left + 12, card.top + 12, card.left + 30, card.top + 30 };
        FillRoundRect(dc, box, m.enabled ? kGreen : kBg, 4, m.enabled ? kGreen : kBadge, true);
        if (m.enabled) {
            HFONT old = static_cast<HFONT>(SelectObject(dc, g_fontReg));
            SetTextColor(dc, kText);
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, L"[x]", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }

        int tx = card.left + 42;
        DrawTextR(dc, m.name, RECT{tx, card.top + 8, tx + 200, card.top + 30}, g_fontHeader, kText);

        std::wstring badge = L"[v" + m.version + L"]";
        HFONT measureFont = static_cast<HFONT>(SelectObject(dc, g_fontSmall));
        SIZE sz{};
        GetTextExtentPoint32W(dc, badge.c_str(), static_cast<int>(badge.size()), &sz);
        SelectObject(dc, measureFont);
        RECT badgeRc{ tx + 200, card.top + 11, tx + 208 + sz.cx, card.top + 29 };
        FillRoundRect(dc, badgeRc, kBadge, 6);
        DrawTextR(dc, badge, badgeRc, g_fontSmall, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        DrawTextR(dc, L"by " + m.author, RECT{badgeRc.right + 8, card.top + 11, card.right - 10, card.top + 29},
                  g_fontSmall, kDim);

        DrawTextR(dc, m.description, RECT{tx, card.top + 34, card.right - 12, card.bottom - 6},
                  g_fontSmall, kDim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        yPos += 70;
    }
    RestoreDC(dc, -1);

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
        // Place banner just above the footer, with minimum spacing
        int bannerTop = g_clientH - 92;
        int bannerBottom = g_clientH - 68;
        // Ensure banner doesn't overlap mod list area
        int listBottom = ListRect().bottom;
        if (bannerTop > listBottom + 16) {
            bannerTop = listBottom + 8;
            bannerBottom = bannerTop + 24;
        }
        DrawTextR(dc, warning, RECT{kPad, bannerTop, w - kPad, bannerBottom}, g_fontSmall, kOrange);
    }

    // ---- Footer ----
    // Active count above conflict banner
    int footerTop = g_clientH - 136;
    int footerBottom = g_clientH - 92;
    DrawTextR(dc, Str_ActiveCount(EnabledModCount(), static_cast<int>(g_mods.size())),
              RECT{kPad, footerTop, 280, footerBottom}, g_fontReg, kDim);

    // Save button below footer (with minimum spacing from bottom)
    int saveTop = g_clientH - 55;
    int saveBottom = g_clientH - 17;
    // Ensure save button is below conflict banner / footer
    int bannerBottom = g_clientH - 68;
    if (saveTop < bannerBottom + 8) {
        saveTop = bannerBottom + 8;
        saveBottom = saveTop + 38;
    }
    COLORREF saveFill = g_hoverSave ? kGreenHover : kGreen;
    FillRoundRect(dc, g_rcSave, saveFill, 10);
    DrawTextR(dc, Str_SaveBtn(), g_rcSave, g_fontHeader, kText,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Hit testing / interaction
// ---------------------------------------------------------------------------

bool PointIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

POINT CursorInClient() {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(g_hwnd, &p);
    return p;
}

void OnLeftDown(POINT pt) {
    if (PointIn(g_rcLang, pt)) {
        g_isRussian = !g_isRussian;
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }
    if (PointIn(g_rcLaunch, pt)) { DoLaunchGame(); return; }
    if (PointIn(g_rcInject, pt)) { DoInjectAttach(); return; }
    if (PointIn(g_rcSave, pt)) { SaveMods(); return; }

    RECT list = ListRect();
    int yPos = list.top + 4 - g_scroll;
    for (auto& m : g_mods) {
        // Stop if past visible area
        if (yPos > ListRect().bottom + 70) break;

        RECT box{ kPad + 12, yPos + 12, kPad + 30, yPos + 30 };
        RECT card{ kPad, yPos, ListRect().right - kPad, yPos + 62 };
        if (PointIn(box, pt) || PointIn(card, pt)) {
            m.enabled = !m.enabled;
            InvalidateRect(g_hwnd, nullptr, TRUE);
            return;
        }
        yPos += 70;
    }
}

} // namespace


void RecalcLayout() {
    int w = g_clientW;
    int h = g_clientH;

    int btnWidth = (w - kPad * 2 - 12) / 2;
    g_rcLaunch = RECT{ kPad, 96, kPad + btnWidth, 140 };
    g_rcInject = RECT{ kPad + btnWidth + 12, 96, w - kPad, 140 };
    g_rcSave = RECT{ w - kPad - 200, h - 55, w - kPad, h - 17 };
    g_rcLang = RECT{ w - 110, 16, w - 20, 44 };
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        ScanMods();
        SetStatusKey(StatusKey::Ready);
        return 0;
    }
    case WM_SIZE:
        g_clientW = LOWORD(lParam);
        g_clientH = HIWORD(lParam);
        RecalcLayout();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool hL = PointIn(g_rcLaunch, pt);
        bool hI = PointIn(g_rcInject, pt);
        bool hS = PointIn(g_rcSave, pt);
        bool hG = PointIn(g_rcLang, pt);
        bool overList = pt.y > ListRect().top && pt.y < ListRect().bottom;
        if ((hL != g_hoverLaunch) || (hI != g_hoverInject) ||
            (hS != g_hoverSave) || (hG != g_hoverLang) || overList) {
            g_hoverLaunch = hL;
            g_hoverInject = hI;
            g_hoverSave = hS;
            g_hoverLang = hG;
            InvalidateRect(hwnd, nullptr, TRUE);
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
        g_hoverLaunch = g_hoverInject = g_hoverSave = g_hoverLang = false;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int listHeight = ListRect().bottom - ListRect().top;
        int totalModHeight = static_cast<int>(g_mods.size()) * 70;
        int maxScroll = std::max(0, totalModHeight - listHeight);
        g_scroll -= delta / WHEEL_DELTA * 40;
        g_scroll = std::max(0, std::min(g_scroll, maxScroll));
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        OnLeftDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        ReleaseCapture();
        return 0;
    }
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
    case WM_DESTROY:
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
            Sleep(25000); // keep process alive while the watcher logs game exit
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
