#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <iostream>

namespace {

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

std::wstring ResolveDllPath(int argc, wchar_t* argv[])
{
    // Explicit argument: resolve relative paths against the current directory.
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

    // Default: LuaAPI.dll next to injector.exe...
    std::wstring exeDir = GetExeDirectory();
    std::wstring candidate = exeDir + L"\\LuaAPI.dll";
    if (FileExists(candidate))
        return candidate;

    // ...then build\RelWithDebInfo\LuaAPI.dll (in-tree build layout).
    candidate = exeDir + L"\\build\\RelWithDebInfo\\LuaAPI.dll";
    if (FileExists(candidate))
        return candidate;

    return exeDir + L"\\LuaAPI.dll"; // reported as not found by caller
}

} // namespace

int wmain(int argc, wchar_t* argv[])
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
    if (pid == 0)
    {
        std::cerr << "Failed to find process: " << WideToNarrow(processName) << " (error " << GetLastError() << ")\n";
        return 1;
    }

    std::cout << "Found process " << WideToNarrow(processName) << " (PID " << pid << ")\n";

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (process == nullptr)
    {
        std::cerr << "OpenProcess failed (error " << GetLastError() << ")\n";
        return 1;
    }

    std::string dllPathNarrow = WideToNarrow(dllPath);
    size_t pathSize = (dllPathNarrow.size() + 1) * sizeof(char);

    void* remoteMemory = VirtualAllocEx(process, nullptr, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemory == nullptr)
    {
        std::cerr << "VirtualAllocEx failed (error " << GetLastError() << ")\n";
        CloseHandle(process);
        return 1;
    }

    BOOL written = WriteProcessMemory(process, remoteMemory, dllPathNarrow.c_str(), pathSize, nullptr);
    if (!written)
    {
        std::cerr << "WriteProcessMemory failed (error " << GetLastError() << ")\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr)
    {
        std::cerr << "GetModuleHandleW failed (error " << GetLastError() << ")\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    FARPROC loadLibraryA = GetProcAddress(kernel32, "LoadLibraryA");
    if (loadLibraryA == nullptr)
    {
        std::cerr << "GetProcAddress(LoadLibraryA) failed (error " << GetLastError() << ")\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HANDLE remoteThread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibraryA), remoteMemory, 0, nullptr);
    if (remoteThread == nullptr)
    {
        std::cerr << "CreateRemoteThread failed (error " << GetLastError() << ")\n";
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    DWORD waitResult = WaitForSingleObject(remoteThread, INFINITE);
    if (waitResult != WAIT_OBJECT_0)
    {
        std::cerr << "WaitForSingleObject failed (result " << waitResult << ", error " << GetLastError() << ")\n";
        CloseHandle(remoteThread);
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    DWORD exitCode = 0;
    GetExitCodeThread(remoteThread, &exitCode);

    VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(remoteThread);
    CloseHandle(process);

    if (exitCode == 0)
    {
        std::cerr << "DLL load failed in remote process (LoadLibraryA returned NULL)\n";
        return 1;
    }

    std::cout << "Successfully injected " << dllPathNarrow << " into " << WideToNarrow(processName) << " (PID " << pid << ")\n";
    return 0;
}
