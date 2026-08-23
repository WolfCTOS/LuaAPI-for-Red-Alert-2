#pragma once
#include <windows.h>

struct lua_State;

namespace LuaAPI {

// Registers the global "World" namespace and the "LuaAPI.Techno" userdata
// metatable on the given lua_State.
void RegisterTechnoBindings(lua_State* L);

// Pushes a userdata wrapping a TechnoClass-derived pointer (BuildingClass,
// UnitClass, ...) onto the stack. Always pushes one value.
void PushTechno(lua_State* L, void* pTechno);

// Expires timed disables; call once per game frame from the main thread.
void ProcessDisabledObjects(unsigned int currentFrame);

} // namespace LuaAPI
