// RA2 Yuri's Revenge — LuaAPI Injector (native Win32 GUI, no console)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr const wchar_t* kWindowClass = L"LuaAPIInjectorWnd";
constexpr const wchar_t* kWindowTitle = L"RA2 Yuri's Revenge \u2014 LuaAPI Injector";

constexpr int kWndWidth = 420;
constexpr int kWndHeight = 280;

constexpr int kMarginX = 30;
constexpr int kBtnWidth = 360;
constexpr int kBtnHeight = 44;

constexpr int IDC_BTN_FIND = 1001;
constexpr int IDC_BTN_INJECT = 1002;
constexpr int IDC_BTN_LAUNCH = 1003;
constexpr int IDC_STATUS = 1004;

constexpr const wchar_t* kGameProcesses[] = {
    L"gamemd.exe",
    L"ra2md.exe",
    L"game.exe",
};

HWND g_hwnd = nullptr;
HWND g_status = nullptr;
HFONT g_font = nullptr;
DWORD g_gamePid = 0;
std::wstring g_gameName;

void SetStatus(const std::wstring& text) {
    if (g_status)
        SetWindowTextW(g_status, text.c_str());
}

// Returns PID and (optionally) the process name that matched.
DWORD FindGameProcess(std::wstring* outName = nullptr) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            for (const wchar_t* name : kGameProcesses) {
                if (_wcsicmp(entry.szExeFile, name) == 0) {
                    pid = entry.th32ProcessID;
                    if (outName)
                        *outName = name;
                    break;
                }
            }
        } while (pid == 0 && Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
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

// Classic remote-thread injection: write the DLL path into the target,
// then call LoadLibraryA on it from a remote thread.
bool InjectDll(HANDLE hProcess, const std::wstring& dllPath, std::wstring* error) {
    int pathBytes = WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (pathBytes <= 0) {
        if (error) *error = L"Path conversion failed";
        return false;
    }
    std::string narrowPath(static_cast<size_t>(pathBytes), '\0');
    WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, &narrowPath[0], pathBytes, nullptr, nullptr);

    void* remoteBase = VirtualAllocEx(hProcess, nullptr, narrowPath.size(),
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBase) {
        if (error) *error = L"VirtualAllocEx failed (error " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteBase, narrowPath.c_str(), narrowPath.size(), nullptr)) {
        if (error) *error = L"WriteProcessMemory failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    auto loadLibraryA = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA"));
    if (!loadLibraryA) {
        if (error) *error = L"GetProcAddress(LoadLibraryA) failed";
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(hProcess, nullptr, 0, loadLibraryA, remoteBase, 0, nullptr);
    if (!thread) {
        if (error) *error = L"CreateRemoteThread failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);

    if (exitCode == 0) {
        if (error) *error = L"LoadLibraryA returned NULL inside the target";
        return false;
    }
    return true;
}

void DoFindGame() {
    g_gamePid = FindGameProcess(&g_gameName);
    if (g_gamePid == 0) {
        SetStatus(L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D\u0430");
        MessageBoxW(g_hwnd,
                    L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430!\n\n"
                    L"\u0421\u043D\u0430\u0447\u0430\u043B\u0430 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 Yuri's Revenge (gamemd.exe).",
                    L"\u041F\u043E\u0438\u0441\u043A \u043F\u0440\u043E\u0446\u0435\u0441\u0441\u0430", MB_ICONWARNING | MB_OK);
    } else {
        SetStatus(L"\u041D\u0430\u0439\u0434\u0435\u043D\u0430 \u0438\u0433\u0440\u0430: " + g_gameName +
                  L" (PID: " + std::to_wstring(g_gamePid) + L")");
    }
}

void DoInject() {
    if (g_gamePid == 0)
        DoFindGame();

    if (g_gamePid == 0)
        return;

    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        SetStatus(L"LuaAPI.dll \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D");
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, g_gamePid);
    if (process == nullptr) {
        SetStatus(L"OpenProcess failed");
        MessageBoxW(g_hwnd, (L"OpenProcess failed (error " + std::to_wstring(GetLastError()) + L")").c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    std::wstring error;
    if (InjectDll(process, dllPath, &error)) {
        SetStatus(L"LuaAPI.dll \u0432\u043D\u0435\u0434\u0440\u0451\u043D \u0443\u0441\u043F\u0435\u0448\u043D\u043E!");
        MessageBoxW(g_hwnd,
                    L"LuaAPI.dll \u0443\u0441\u043F\u0435\u0448\u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0435\u043D \u0432 \u0438\u0433\u0440\u0443!",
                    L"\u0423\u0441\u043F\u0435\u0445", MB_ICONINFORMATION | MB_OK);
    } else {
        SetStatus(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C");
        MessageBoxW(g_hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + error).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
    }

    CloseHandle(process);
}

void DoLaunchGame() {
    HINSTANCE result = ShellExecuteW(g_hwnd, L"open", L"gamemd.exe", nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetStatus(L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C gamemd.exe");
        MessageBoxW(g_hwnd,
                    L"gamemd.exe \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D \u0432 \u043F\u0430\u043F\u043A\u0435 \u0438\u043D\u0436\u0435\u043A\u0442\u043E\u0440\u0430.",
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    SetStatus(L"gamemd.exe \u0437\u0430\u043F\u0443\u0441\u043A\u0430\u0435\u0442\u0441\u044F...");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_FIND)
            DoFindGame();
        else if (id == IDC_BTN_INJECT)
            DoInject();
        else if (id == IDC_BTN_LAUNCH)
            DoLaunchGame();
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);

    // Center on screen, non-resizable.
    int x = (GetSystemMetrics(SM_CXSCREEN) - kWndWidth) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - kWndHeight) / 2;

    g_hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, kWindowClass, kWindowTitle,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             x, y, kWndWidth, kWndHeight,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd)
        return 1;

    // Clean proportional UI font instead of the raw default pixel font.
    g_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    auto makeControl = [&](const wchar_t* text, int id, int yPos) {
        HWND ctrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"BUTTON", text,
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    kMarginX, yPos, kBtnWidth, kBtnHeight, g_hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    hInstance, nullptr);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        return ctrl;
    };

    makeControl(L"\U0001F50D \u041D\u0430\u0439\u0442\u0438 \u0438\u0433\u0440\u0443 (Find Game)", IDC_BTN_FIND, 22);
    makeControl(L"\u26A1 \u0412\u043D\u0435\u0434\u0440\u0438\u0442\u044C LuaAPI (Inject)", IDC_BTN_INJECT, 76);
    makeControl(L"\U0001F680 \u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C \u0438\u0433\u0440\u0443 (Launch Game)", IDC_BTN_LAUNCH, 130);

    g_status = CreateWindowExW(0, L"STATIC",
                               L"\u0413\u043E\u0442\u043E\u0432\u043E. \u0412\u044B\u0431\u0435\u0440\u0438\u0442\u0435 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0435.",
                               WS_CHILD | WS_VISIBLE | SS_CENTER,
                               kMarginX, 186, kBtnWidth, 34, g_hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
                               hInstance, nullptr);
    SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_font)
        DeleteObject(g_font);
    return static_cast<int>(msg.wParam);
}
