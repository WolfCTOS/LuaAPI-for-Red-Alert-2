#include <LuaAPI/bindings_production.hpp>
#include <LuaAPI/logger.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

using byte = unsigned char;

#include <YRPP.h>

namespace LuaAPI {

namespace {

constexpr const char* kHouseMetaName = "LuaAPI.House";

HouseClass* CheckHouse(lua_State* L, int idx)
{
    void* ud = luaL_checkudata(L, idx, kHouseMetaName);

    auto* pHouse = *static_cast<HouseClass**>(ud);

    if (!pHouse) {
        luaL_error(L, "house object is no longer valid");
        return nullptr;
    }

    return pHouse;
}

// AI.QueueUnit(house, "DRED") -> boolean
int AI_QueueUnit(lua_State* L)
{
    HouseClass* pHouse = CheckHouse(L, 1);
    const char* typeName = luaL_checkstring(L, 2);

    if (!pHouse) {
        lua_pushboolean(L, 0);
        return 1;
    }

    TechnoTypeClass* pType = TechnoTypeClass::Find(typeName);

    if (!pType) {
        return luaL_error(
            L,
            "AI.QueueUnit: unknown techno type '%s'",
            typeName
        );
    }

    bool produced = false;

    for (auto* pFactory : FactoryClass::Array) {

        if (!pFactory)
            continue;

        if (pFactory->Owner != pHouse)
            continue;

        if (pFactory->DemandProduction(pType, pHouse, true)) {
            produced = true;
            break;
        }
    }

    LUA_LOG_INFO(
        "[Production] House={} request={} result={}",
        pHouse->get_ID(),
        typeName,
        produced ? "accepted" : "rejected"
    );

    lua_pushboolean(L, produced ? 1 : 0);
    return 1;
}


// AI.CountUnit(house, "DRED") -> integer
int AI_CountUnit(lua_State* L)
{
    HouseClass* pHouse = CheckHouse(L, 1);
    const char* typeName = luaL_checkstring(L, 2);

    if (!pHouse) {
        lua_pushinteger(L, 0);
        return 1;
    }

    TechnoTypeClass* pType = TechnoTypeClass::Find(typeName);

    if (!pType) {
        return luaL_error(
            L,
            "AI.CountUnit: unknown techno type '%s'",
            typeName
        );
    }

    int count = 0;

    for (auto* pFactory : FactoryClass::Array) {

        if (!pFactory)
            continue;

        if (pFactory->Owner != pHouse)
            continue;

        count += pFactory->CountTotal(pType);
    }

    lua_pushinteger(L, count);
    return 1;
}

const luaL_Reg kProductionFunctions[] = {
    { "QueueUnit", AI_QueueUnit },
    { "CountUnit", AI_CountUnit },
    { nullptr, nullptr }
};

} // anonymous namespace


void RegisterProductionBindings(lua_State* L)
{
    // Get/create global AI table.
    lua_getglobal(L, "AI");

    if (!lua_istable(L, -1)) {

        lua_pop(L, 1);

        lua_newtable(L);
        lua_setglobal(L, "AI");

        lua_getglobal(L, "AI");
    }

    luaL_setfuncs(L, kProductionFunctions, 0);

    lua_pop(L, 1);

    LUA_LOG_INFO("[Production] bindings registered");
}

} // namespace LuaAPI