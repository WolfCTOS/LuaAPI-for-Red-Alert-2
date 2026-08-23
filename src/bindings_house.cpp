#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/logger.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

// YRpp game classes
#include <YRPP.h>

namespace LuaAPI {

namespace {

constexpr const char* kMetaName = "LuaAPI.House";

HouseClass* CheckHouse(lua_State* L, int idx) {
    void* ud = luaL_checkudata(L, idx, kMetaName);
    auto* pHouse = *static_cast<HouseClass**>(ud);
    if (!pHouse) {
        luaL_error(L, "house object is no longer valid");
        return nullptr;
    }
    return pHouse;
}

HouseClass** NewHouse(lua_State* L, HouseClass* pHouse) {
    auto* ud = static_cast<HouseClass**>(lua_newuserdatauv(L, sizeof(HouseClass*), 0));
    *ud = pHouse;
    luaL_getmetatable(L, kMetaName);
    lua_setmetatable(L, -2);
    return ud;
}

} // anonymous namespace

int PushHouse(lua_State* L, HouseClass* pHouse) {
    if (!pHouse)
        return 0;
    NewHouse(L, pHouse);
    return 1;
}

namespace {

// House.GetPlayer() -> house | nil
int House_GetPlayer(lua_State* L) {
    HouseClass* pHouse = HouseClass::CurrentPlayer;
    if (!pHouse)
        return 0; // nil

    NewHouse(L, pHouse);
    return 1;
}

// House.GetCount() -> int
int House_GetCount(lua_State* L) {
    lua_pushinteger(L, HouseClass::Array.Count);
    return 1;
}

// House.GetByIndex(idx) -> house | nil
int House_GetByIndex(lua_State* L) {
    lua_Integer idx = luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= HouseClass::Array.Count) {
        LUA_LOG_WARN("House.GetByIndex({}) out of range (count={})", idx, HouseClass::Array.Count);
        return 0; // nil
    }

    HouseClass* pHouse = HouseClass::Array.GetItem(static_cast<int>(idx));
    if (!pHouse)
        return 0;

    NewHouse(L, pHouse);
    return 1;
}

// --- instance methods ------------------------------------------------------

// house:GetCredits() -> int
int House_GetCredits(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(pHouse->Available_Money()));
    return 1;
}

// house:SetCredits(amount)
int House_SetCredits(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_Integer target = luaL_checkinteger(L, 2);

    long current = pHouse->Available_Money();
    long delta = static_cast<long>(target) - current;
    if (delta != 0)
        pHouse->TransactMoney(delta);

    LUA_LOG_INFO("[House] {} credits set to {} (delta {:+})", pHouse->get_ID(), target, delta);
    return 0;
}

// house:AddCredits(delta)
int House_AddCredits(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_Integer delta = luaL_checkinteger(L, 2);

    if (delta != 0)
        pHouse->TransactMoney(static_cast<long>(delta));

    LUA_LOG_INFO("[House] {} credits adjusted ({:+})", pHouse->get_ID(), delta);
    return 0;
}

// house:GetPowerOutput() -> int
int House_GetPowerOutput(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_pushinteger(L, pHouse->PowerOutput);
    return 1;
}

// house:GetPowerDrain() -> int
int House_GetPowerDrain(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_pushinteger(L, pHouse->PowerDrain);
    return 1;
}

// house:GetName() -> string
int House_GetName(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_pushstring(L, pHouse->get_ID());
    return 1;
}

// house:IsHuman() -> bool
int House_IsHuman(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_pushboolean(L, pHouse->IsControlledByHuman() ? 1 : 0);
    return 1;
}

const luaL_Reg kHouseMethods[] = {
    { "GetCredits",     House_GetCredits     },
    { "SetCredits",     House_SetCredits     },
    { "AddCredits",     House_AddCredits     },
    { "GetPowerOutput", House_GetPowerOutput },
    { "GetPowerDrain",  House_GetPowerDrain  },
    { "GetName",        House_GetName        },
    { "IsHuman",        House_IsHuman        },
    { nullptr, nullptr }
};

} // namespace

void RegisterHouseBindings(lua_State* L) {
    // Userdata metatable
    luaL_newmetatable(L, kMetaName);

    // metatable.__index points to the methods table
    lua_newtable(L);
    luaL_setfuncs(L, kHouseMethods, 0);
    lua_setfield(L, -2, "__index");

    lua_pop(L, 1); // pop metatable

    // Global "House" namespace
    lua_newtable(L);
    lua_pushcfunction(L, House_GetPlayer);
    lua_setfield(L, -2, "GetPlayer");
    lua_pushcfunction(L, House_GetCount);
    lua_setfield(L, -2, "GetCount");
    lua_pushcfunction(L, House_GetByIndex);
    lua_setfield(L, -2, "GetByIndex");
    lua_setglobal(L, "House");
}

} // namespace LuaAPI
