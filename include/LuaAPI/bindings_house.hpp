#pragma once
struct lua_State;

namespace LuaAPI {

// Registers the global "House" namespace and the "LuaAPI.House" userdata
// metatable on the given lua_State.
void RegisterHouseBindings(lua_State* L);

} // namespace LuaAPI
