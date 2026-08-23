#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <iostream>

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

DWORD FindProcessId(const wchar_t* processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, processName) == 0)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

std::string WideToNarrow(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int size = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &narrow[0], size, nullptr, nullptr);
    return narrow;
}

bool FileExists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetExeDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true)
    {
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

// Classic remote-thread LoadLibraryA injection.
bool InjectDll(HANDLE hProcess, const std::wstring& dllPath)
{
    if (!FileExists(dllPath))
    {
        std::wcerr << L"[injector] DLL not found, skipping: " << dllPath << L"\n";
        return false;
    }

    std::string narrowPath = WideToNarrow(dllPath);

    void* remoteMemory = VirtualAllocEx(hProcess, nullptr, narrowPath.size() + 1,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemory == nullptr)
    {
        std::wcerr << L"[injector] VirtualAllocEx failed (error " << GetLastError() << L")\n";
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMemory, narrowPath.c_str(), narrowPath.size() + 1, nullptr))
    {
        std::wcerr << L"[injector] WriteProcessMemory failed (error " << GetLastError() << L")\n";
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    auto loadLibraryA = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA"));
    if (loadLibraryA == nullptr)
    {
        std::wcerr << L"[injector] GetProcAddress(LoadLibraryA) failed\n";
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HANDLE remoteThread = CreateRemoteThread(hProcess, nullptr, 0, loadLibraryA, remoteMemory, 0, nullptr);
    if (remoteThread == nullptr)
    {
        std::wcerr << L"[injector] CreateRemoteThread failed (error " << GetLastError() << L")\n";
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(remoteThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(remoteThread, &exitCode);

    CloseHandle(remoteThread);
    VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);

    if (exitCode == 0)
    {
        std::wcerr << L"[injector] LoadLibraryA failed inside target for: " << dllPath << L"\n";
        return false;
    }

    std::wcout << L"[injector] Injected: " << dllPath << L"\n";
    return true;
}

// ---------------------------------------------------------------------------
// Mode A: Spawn mode - launched with arguments (e.g. by the CnCNet client):
//   injector.exe "gamemd.exe" -SPAWN -LOG -CD
// Creates the game process suspended, injects DLLs, then resumes it and
// waits for exit, forwarding the exit code.
// ---------------------------------------------------------------------------

int SpawnMode(int argc, wchar_t* argv[])
{
    // Reconstruct the target command line: everything except argv[0].
    std::wstring cmdline;
    for (int i = 1; i < argc; ++i)
    {
        if (!cmdline.empty())
            cmdline += L' ';

        std::wstring part = argv[i];
        if (part.find(L' ') != std::wstring::npos && part.front() != L'"')
            cmdline += L'"' + part + L'"';
        else
            cmdline += part;
    }

    // Target executable is the first argument.
    std::wstring targetName = argv[1];
    // Strip quotes and any path, keep file name for error messages.
    size_t slash = targetName.find_last_of(L"\\/");
    std::wstring targetFile = slash == std::wstring::npos ? targetName : targetName.substr(slash + 1);
    if (!targetFile.empty() && targetFile.front() == L'"') targetFile = targetFile.substr(1);
    if (!targetFile.empty() && targetFile.back() == L'"') targetFile.pop_back();

    // Resolve the executable path: current dir first, then injector dir.
    std::wstring exeDir = GetExeDirectory();
    std::wstring targetPath = targetFile;
    if (!FileExists(targetPath))
    {
        std::wstring candidate = exeDir + L"\\" + targetFile;
        if (FileExists(candidate))
            targetPath = candidate;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wcout << L"[injector] Spawning: " << cmdline << L"\n";

    // CreateProcessW may modify the command line buffer.
    std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = CreateProcessW(
        targetPath.c_str(),
        mutableCmd.data(),
        nullptr, nullptr, FALSE,
        CREATE_SUSPENDED,
        nullptr, nullptr,
        &si, &pi);

    if (!ok)
    {
        std::wcerr << L"[injector] CreateProcessW failed (error " << GetLastError() << L")\n";
        return 1;
    }

    std::wcout << L"[injector] Created PID " << pi.dwProcessId << L" (suspended)\n";

    // Inject engine DLLs while the process is suspended.
    InjectDll(pi.hProcess, exeDir + L"\\cncnet5.dll");   // optional: spawner/hooks
    InjectDll(pi.hProcess, exeDir + L"\\LuaAPI.dll");    // required: LuaAPI engine

    ResumeThread(pi.hThread);
    std::wcout << L"[injector] Main thread resumed, waiting for exit...\n";

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::wcout << L"[injector] Game exited with code " << exitCode << L"\n";
    return static_cast<int>(exitCode);
}

// ---------------------------------------------------------------------------
// Mode B: Attach mode - no arguments. Finds an already running gamemd.exe and
// injects LuaAPI.dll into it.
// ---------------------------------------------------------------------------

std::wstring ResolveDllPath(int argc, wchar_t* argv[]);

int AttachMode(int argc, wchar_t* argv[])
{
    const wchar_t* processName = L"gamemd.exe";

    std::wstring dllPath = ResolveDllPath(argc, argv);
    if (!FileExists(dllPath))
    {
        std::wcerr << L"DLL not found: " << dllPath << L"\n"
                   << L"Place LuaAPI.dll next to injector.exe or pass an explicit path.\n";
        return 1;
    }

    std::wcout << L"Using DLL: " << dllPath << L"\n";

    DWORD pid = FindProcessId(processName);

    // 1-Click Launch: game not running -> spawn it suspended, inject, resume.
    if (pid == 0)
    {
        std::wstring exeDir = GetExeDirectory();
        std::wstring gamePath = exeDir + L"\\" + processName;
        if (!FileExists(gamePath))
        {
            std::wcerr << L"[injector] gamemd.exe not found next to injector.exe (" << gamePath << L")\n";
            return 1;
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::wcout << L"[injector] Game not running - launching " << gamePath << L"\n";

        if (!CreateProcessW(gamePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, exeDir.c_str(), &si, &pi))
        {
            std::wcerr << L"[injector] CreateProcessW failed (error " << GetLastError() << L")\n";
            return 1;
        }

        std::wcout << L"[injector] Created PID " << pi.dwProcessId << L" (suspended)\n";
        InjectDll(pi.hProcess, dllPath);

        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        std::wcout << L"[injector] Game launched and engine injected. Exiting.\n";
        return 0; // exit immediately - no lingering console
    }

    std::cout << "Found process " << WideToNarrow(processName) << " (PID " << pid << ")\n";

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (process == nullptr)
    {
        std::cerr << "OpenProcess failed (error " << GetLastError() << ")\n";
        return 1;
    }

    if (!InjectDll(process, dllPath))
    {
        CloseHandle(process);
        return 1;
    }

    CloseHandle(process);
    std::cout << "Successfully injected " << WideToNarrow(dllPath) << " into " << WideToNarrow(processName) << "\n";
    return 0;
}

// Default DLL resolution used by attach mode:
// explicit arg -> next to injector.exe -> build\RelWithDebInfo\LuaAPI.dll.
std::wstring ResolveDllPath(int argc, wchar_t* argv[])
{
    if (argc > 1 && argv[1][0] != L'\0')
    {
        std::wstring arg = argv[1];
        if (FileExists(arg))
        {
            std::wstring full(MAX_PATH, L'\0');
            DWORD len = GetFullPathNameW(arg.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
            if (len > 0 && len < full.size())
                return full.substr(0, len);
        }
        return arg;
    }

    std::wstring exeDir = GetExeDirectory();
    std::wstring candidate = exeDir + L"\\LuaAPI.dll";
    if (FileExists(candidate))
        return candidate;

    candidate = exeDir + L"\\build\\RelWithDebInfo\\LuaAPI.dll";
    if (FileExists(candidate))
        return candidate;

    return exeDir + L"\\LuaAPI.dll";
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc > 1)
        return SpawnMode(argc, argv);
    return AttachMode(argc, argv);
}
