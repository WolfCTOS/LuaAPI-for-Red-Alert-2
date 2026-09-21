#pragma once
#include <string>

namespace LuaAPI {

// Installs a first-chance vectored handler that writes ONE MiniDumpNormal
// minidump (stacks + threads + modules, no heap) into crash_evidence/ on an
// access-violation storm, then lets normal handling continue. Diagnostic only:
// no gameplay behavior changes. Safe under Syringe (in-process, needs no
// external debugger) where WER LocalDumps cannot fire.
void InstallCrashDumper(const std::wstring& gameDir);

} // namespace LuaAPI
