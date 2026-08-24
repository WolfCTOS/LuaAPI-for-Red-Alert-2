// RA2 Yuri's Revenge вЂ” LuaAPI Injector (modern dark Win32 GUI, no console)
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
constexpr const wchar_t* kWindowTitle = L"RA2 Yuri's Revenge \u2014 LuaAPI Engine";

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
    return L10N(L"\u041F\u043B\u0430\u0442\u0444\u043E\u0440\u043C\u0430 \u043C\u043E\u0434\u043E\u0432 \u0434\u043B\u044F Yuri's Revenge v1.001",
                L"Yuri's Revenge v1.001 Modding Platform");
}
const wchar_t* Str_StatusReady() {
    return L10N(L"\U0001F7E2 \u0413\u043E\u0442\u043E\u0432 \u043A \u0437\u0430\u043F\u0443\u0441\u043A\u0443",
                L"\U0001F7E2 Ready to Launch");
}
const wchar_t* Str_StatusInjected() { return L10N(
    L"\U0001F7E2 \u0418\u0433\u0440\u0430 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430 \u0438 LuaAPI \u0432\u043D\u0435\u0434\u0440\u0435\u043D",
    L"\U0001F7E2 Game Running & LuaAPI Injected"); }
const wchar_t* Str_Busy() { return L10N(
    L"\u23F3 \u041E\u0431\u0440\u0430\u0431\u043E\u0442\u043A\u0430...",
    L"\u23F3 Working..."); }
const wchar_t* Str_LaunchBtn() { return L10N(
    L"\U0001F680 \u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C \u0438\u0433\u0440\u0443",
    L"\U0001F680 Launch Game"); }
const wchar_t* Str_InjectBtn() { return L10N(
    L"\u26A1 \u0412\u043D\u0435\u0434\u0440\u0438\u0442\u044C",
    L"\u26A1 Inject"); }
const wchar_t* Str_ModsHeader() { return L10N(
    L"\u0423\u0421\u0422\u0410\u043D\u041E\u0412\u041B\u0415\u041D\u041D\u042B\u0415 \u041C\u041E\u0414\u042B",
    L"INSTALLED MODS"); }
const wchar_t* Str_SaveBtn() { return L10N(
    L"\U0001F4BE \u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u0438 \u043F\u0440\u0438\u043C\u0435\u043D\u0438\u0442\u044C",
    L"\U0001F4BE Save & Apply"); }
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

// Layout metrics recomputed in RecalcLayout.
RECT ListRect() {
    return RECT{ 0, 178, g_clientW, g_clientH - 70 };
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
    // If the game is already running, just inject instead of double-spawning.
    DWORD runningPid = FindTargetProcess();
    if (runningPid != 0) {
        g_gamePid = runningPid;
        g_gameName = kGameProcess;
        DoInject();
        return;
    }

    std::wstring exeDir = GetExeDirectory();
    std::wstring gamePath = exeDir + L"\\gamemd.exe";
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";

    if (!FileExists(gamePath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::GameNotFound);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + gamePath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    if (!FileExists(dllPath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    g_appState = AppState::Busy;
    SetStatusKey(StatusKey::BusyLaunch);
    if (!CreateProcessW(gamePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, exeDir.c_str(), &si, &pi)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::Custom); g_statusColor = kRed; g_statusCustom = L"CreateProcessW failed";
        MessageBoxW(g_hwnd, (L"CreateProcessW failed (error " + std::to_wstring(GetLastError()) + L")").c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    std::wstring error;
    if (!InjectDllIntoProcess(pi.dwProcessId, dllPath, &error)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::InjectFail);
        MessageBoxW(g_hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + error).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    g_gamePid = pi.dwProcessId;
    g_appState = AppState::Injected;
    SetStatusKey(StatusKey::Injected);
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
    DrawTextR(dc, L"\u26A1 RED ALERT 2 \u2014 LUA ENGINE",
              RECT{kPad, 14, w - 130, 42}, g_fontTitle, kText);

    // Language toggle (top right)
    FillRoundRect(dc, g_rcLang, g_hoverLang ? kHover : kSurface, 14);
    DrawTextR(dc, L"\U0001F310 RU / EN", g_rcLang, g_fontSmall,
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

    int yPos = list.top + 4 - g_scroll;
    for (auto& m : g_mods) {
        RECT card{ kPad, yPos, w - kPad, yPos + 62 };
        bool hovered = PtInRect(&card, cursor);

        FillRoundRect(dc, card, hovered ? kHover : kSurface, 10,
                      m.enabled ? kGreen : kBadge, m.enabled);

        RECT box{ card.left + 12, card.top + 12, card.left + 30, card.top + 30 };
        FillRoundRect(dc, box, m.enabled ? kGreen : kBg, 4, m.enabled ? kGreen : kBadge, true);
        if (m.enabled) {
            HFONT old = static_cast<HFONT>(SelectObject(dc, g_fontReg));
            SetTextColor(dc, kText);
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, L"\u2713", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
        }

        int tx = card.left + 42;
        DrawTextR(dc, m.name, RECT{tx, card.top + 8, tx + 220, card.top + 30}, g_fontHeader, kText);

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

        yPos += 62 + 8;
    }
    RestoreDC(dc, -1);

    // ---- Conflict banner ----
    auto conflicts = DetectConflicts();
    if (!conflicts.empty()) {
        std::wstring warning;
        for (size_t k = 0; k < conflicts.size(); ++k) {
            warning += L"\u26A0 " + g_mods[conflicts[k].first].name +
                       L" \u226E " + g_mods[conflicts[k].second].name;
            if (k + 1 < conflicts.size())
                warning += L";  ";
        }
        DrawTextR(dc, warning, RECT{kPad, g_clientH - 92, w - kPad, g_clientH - 68}, g_fontSmall, kOrange);
    }

    // ---- Footer ----
    DrawTextR(dc, Str_ActiveCount(EnabledModCount(), static_cast<int>(g_mods.size())),
              RECT{kPad, g_clientH - 48, 280, g_clientH - 24}, g_fontReg, kDim);

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
        RECT box{ kPad + 12, yPos + 12, kPad + 30, yPos + 30 };
        RECT card{ kPad, yPos, g_clientW - kPad, yPos + 62 };
        if (PointIn(box, pt) || PointIn(card, pt)) {
            m.enabled = !m.enabled;
            InvalidateRect(g_hwnd, nullptr, TRUE);
            return;
        }
        yPos += 62 + 8;
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
        int maxScroll = static_cast<int>(g_mods.size()) * 70 - (ListRect().bottom - ListRect().top);
        if (maxScroll < 0) maxScroll = 0;
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
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
