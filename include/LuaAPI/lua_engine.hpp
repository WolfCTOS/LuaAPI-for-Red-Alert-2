#pragma once
#include <windows.h>
#include <string>

struct lua_State;

namespace LuaAPI {

// Returns the directory containing the given module, without trailing slash.
std::wstring GetModuleDirectory(HMODULE hModule);

// Stores the DLL's own directory (for locating scripts). Call once from bootstrap.
void InitPaths(HMODULE hModule);

// Called every game frame from the Syringe MainLoop hook (main thread).
// Lazily initializes the Lua state, then dispatches OnTick(frame).
void OnGameFrame();

} // namespace LuaAPI
