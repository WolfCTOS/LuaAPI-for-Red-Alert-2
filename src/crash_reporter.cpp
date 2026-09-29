#include <LuaAPI/crash_reporter.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// UEF crash reporter. Everything the handler touches is either a static
// buffer or init-time snapshots: no heap allocation, no locks, no CRT
// formatting (hand-rolled hex), so it is safe to run on the faulting thread
// in an unknown state. It chains to any previously installed filter.

namespace LuaAPI {
namespace CrashReporter {

namespace {

HANDLE g_log = INVALID_HANDLE_VALUE;
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

// Module ranges snapshotted at init (no loader-lock calls in the handler).
uintptr_t g_ourBase = 0;
uintptr_t g_ourEnd = 0;
uintptr_t g_gameBase = 0;
uintptr_t g_gameEnd = 0;

// Recent engine-contact ring. Written on the game thread only, read by the
// handler. Single-word stores; torn reads would only garble one line.
struct RingEntry {
    const char* tag;
    DWORD tick;
};
constexpr int kRingMask = 31; // 32 entries, power of two
RingEntry g_ring[kRingMask + 1] = {};
volatile LONG g_ringPos = 0;

char g_buf[4096];

uintptr_t ModuleRangeEnd(HMODULE h) {
    if (!h)
        return 0;
    __try {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(h);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<char*>(h) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        uintptr_t size = nt->OptionalHeader.SizeOfImage;
        if (size == 0 || size > 0x10000000u)
            return 0;
        return reinterpret_cast<uintptr_t>(h) + size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void PutHex(char*& p, const char* end, uintptr_t v, int digits) {
    static const char* kHex = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0 && p < end; --i)
        *p++ = kHex[(v >> (i * 4)) & 0xFu];
}

void PutStr(char*& p, const char* end, const char* s, int maxLen) {
    int n = 0;
    while (p < end && s && *s && n < maxLen) {
        *p++ = *s++;
        ++n;
    }
}

void PutU32(char*& p, const char* end, unsigned long v) {
    char tmp[12];
    int n = 0;
    if (v == 0) {
        if (p < end)
            *p++ = '0';
        return;
    }
    while (v > 0 && n < 12) {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    while (n > 0 && p < end)
        *p++ = tmp[--n];
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    char* p = g_buf;
    const char* end = g_buf + sizeof(g_buf) - 1;

    PutStr(p, end, "CRASH code=", 11);
    PutHex(p, end, info ? static_cast<uintptr_t>(info->ExceptionRecord->ExceptionCode) : 0, 8);
    PutStr(p, end, " addr=", 6);
    uintptr_t addr = 0;
    if (info && info->ExceptionRecord->NumberParameters >= 1)
        addr = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
    PutHex(p, end, addr, 8);
    PutStr(p, end, " in=", 4);
    if (addr >= g_ourBase && addr < g_ourEnd)
        PutStr(p, end, "LuaAPI.dll+", 11);
    else if (addr >= g_gameBase && addr < g_gameEnd)
        PutStr(p, end, "gamemd+", 7);
    else
        PutStr(p, end, "other+", 6);
    if ((addr >= g_ourBase && addr < g_ourEnd) || (addr >= g_gameBase && addr < g_gameEnd)) {
        uintptr_t base = (addr >= g_ourBase && addr < g_ourEnd) ? g_ourBase : g_gameBase;
        PutStr(p, end, "0x", 2);
        PutHex(p, end, addr - base, 8);
    }
    PutStr(p, end, "\r\nrecent:", 9);
    LONG pos = g_ringPos;
    for (int k = 0; k <= kRingMask; ++k) {
        int idx = static_cast<int>((pos - 1 - k) & kRingMask);
        const char* tag = g_ring[idx].tag;
        if (!tag)
            break;
        PutStr(p, end, "\r\n  [", 4);
        PutU32(p, end, g_ring[idx].tick);
        PutStr(p, end, "] ", 2);
        PutStr(p, end, tag, 48);
    }
    PutStr(p, end, "\r\n", 2);

    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_log, g_buf, static_cast<DWORD>(p - g_buf), &written, nullptr);
        FlushFileBuffers(g_log);
    }

    if (g_prevFilter)
        return g_prevFilter(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void Init() {
    static bool done = false;
    if (done)
        return;
    done = true;

    HMODULE ours = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCSTR>(&Init), &ours);
    if (ours) {
        g_ourBase = reinterpret_cast<uintptr_t>(ours);
        g_ourEnd = ModuleRangeEnd(ours);
    }
    HMODULE game = GetModuleHandleA(nullptr);
    if (game) {
        g_gameBase = reinterpret_cast<uintptr_t>(game);
        g_gameEnd = ModuleRangeEnd(game);
    }

    g_log = CreateFileA("LuaAPI.crash.log", FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    for (int i = 0; i <= kRingMask; ++i) {
        g_ring[i].tag = nullptr;
        g_ring[i].tick = 0;
    }

    g_prevFilter = SetUnhandledExceptionFilter(&CrashFilter);
}

void Note(const char* tag) {
    LONG i = _InterlockedIncrement(&g_ringPos) & kRingMask;
    g_ring[i].tag = tag;
    g_ring[i].tick = GetTickCount();
}

} // namespace CrashReporter
} // namespace LuaAPI
