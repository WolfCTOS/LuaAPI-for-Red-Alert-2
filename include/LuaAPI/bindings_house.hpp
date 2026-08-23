#pragma once
struct lua_State;
class HouseClass;

namespace LuaAPI {

// Registers the global "House" namespace and the "LuaAPI.House" userdata
// metatable on the given lua_State.
void RegisterHouseBindings(lua_State* L);

// Pushes a userdata wrapping pHouse onto the stack (or nothing if null).
// Returns the number of values pushed (1 or 0).
int PushHouse(lua_State* L, HouseClass* pHouse);

} // namespace LuaAPI
