// LuaAPI crash-evidence dumper (diagnostic only, 2026-09-21).
//
// Problem: AV storms (e.g. 2026-09-21: 2202x 0xC0000005 in ~3 s, no dump,
// LuaAPI.log + syringe.log overwritten on next launch) leave no stack.
// Syringe is attached as debugger, so WER LocalDumps never fires; an
// external ProcDump cannot attach alongside it either.
//
// Answer: an in-process vectored exception handler (VEH runs before SEH
// frames, works under any debugger) that writes ONE MiniDumpNormal dump
// (stacks + threads + loaded modules, no full heap: small and fast) and
// then returns EXCEPTION_CONTINUE_SEARCH so normal handling proceeds.
//
// Storm-proofing (2026-09-21 p.m.): fire on the FIRST AV the handler sees.
// Rationale: under Syringe the debugger may swallow first-chance exceptions
// (DBG_CONTINUE) so in-process VEH can miss most of a storm — observed
// 2026-09-21 20:13: 3 AVs total, instant death, no dump at threshold 3.
// A stray benign handled AV costs one small MiniDumpNormal file; a real
// death spiral always trips the first one the handler observes.
//
// C2712 discipline: the handler and writer use POD locals only; DbgHelp is
// loaded dynamically (no link dependency); no logger calls inside the
// handler (the logger mutex may be held by the faulting thread).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <strsafe.h>
#include <LuaAPI/crash_dump.hpp>

namespace LuaAPI {

namespace {

volatile LONG s_avCount = 0;
volatile LONG s_dumped = 0;
wchar_t s_dir[MAX_PATH] = L"";

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(
    HANDLE hProcess, DWORD ProcessId, HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

// POD-only body; caller wraps in __try/__except.
void WriteDump(EXCEPTION_POINTERS* pExc) {
    if (!pExc || s_dir[0] == L'\0')
        return;

    wchar_t evDir[MAX_PATH];
    if (FAILED(StringCchPrintfW(evDir, MAX_PATH, L"%s\\crash_evidence", s_dir)))
        return;
    CreateDirectoryW(evDir, nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    if (FAILED(StringCchPrintfW(path, MAX_PATH,
            L"%s\\av_%04d%02d%02d_%02d%02d%02d.dmp",
            evDir, st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond)))
        return;

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    HMODULE hDbg = LoadLibraryW(L"DbgHelp.dll");
    if (hDbg) {
        MiniDumpWriteDumpFn fn = reinterpret_cast<MiniDumpWriteDumpFn>(
            GetProcAddress(hDbg, "MiniDumpWriteDump"));
        if (fn) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = pExc;
            mei.ClientPointers = FALSE;
            fn(GetCurrentProcess(), GetCurrentProcessId(), hFile,
               MiniDumpNormal, &mei, nullptr, nullptr);
        }
        FreeLibrary(hDbg);
    }
    CloseHandle(hFile);
}

LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* pExc) {
    if (!pExc || !pExc->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    if (pExc->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&s_avCount);
    if (InterlockedCompareExchange(&s_dumped, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    __try {
        WriteDump(pExc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void InstallCrashDumper(const std::wstring& gameDir) {
    size_t n = gameDir.size();
    if (n == 0 || n >= MAX_PATH)
        return;
    for (size_t i = 0; i <= n; ++i)
        s_dir[i] = gameDir[i];
    AddVectoredExceptionHandler(1, VectoredHandler);
}

} // namespace LuaAPI
