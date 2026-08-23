#pragma once
#include <windows.h>
#include <string>

namespace LuaAPI {

// Returns the directory containing the given module, without trailing slash.
std::wstring GetModuleDirectory(HMODULE hModule);

// Initializes the Lua state and executes <module_dir>/scripts/init.lua.
// Call only from a worker thread, never from DllMain.
void StartEngine(const std::wstring& moduleDir);

} // namespace LuaAPI
