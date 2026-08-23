#include <LuaAPI/bindings_techno.hpp>
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

#include <cmath>
#include <vector>

namespace LuaAPI {

// Defined below; pushes a "LuaAPI.Techno" userdata wrapping pTechno.
void PushTechno(lua_State* L, void* pTechno);

namespace {

constexpr const char* kMetaName = "LuaAPI.Techno";

// Timed-disable registry, processed every frame from OnGameFrame.
struct DisableEntry {
    TechnoClass* ptr;
    bool isBuilding;
    bool hadPower;        // BuildingClass::HasPower prior to the blackout
    unsigned int expiryFrame;
};
std::vector<DisableEntry> g_disabledEntries;

bool IsValid(TechnoClass* pTechno) {
    return pTechno != nullptr && pTechno->Health > 0;
}

TechnoClass* CheckTechno(lua_State* L, int idx) {
    void* ud = luaL_checkudata(L, idx, kMetaName);
    auto* pTechno = *static_cast<TechnoClass**>(ud);
    if (!pTechno) {
        luaL_error(L, "techno object is no longer valid");
        return nullptr;
    }
    return pTechno;
}

// --- instance methods ------------------------------------------------------

// obj:GetTypeName() -> string
int Techno_GetTypeName(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!IsValid(pTechno))
        return 0;
    lua_pushstring(L, pTechno->GetType()->get_ID());
    return 1;
}

// obj:GetHealth() -> int
int Techno_GetHealth(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    lua_pushinteger(L, pTechno->Health);
    return 1;
}

// obj:GetMaxHealth() -> int
int Techno_GetMaxHealth(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!IsValid(pTechno))
        return 0;
    lua_pushinteger(L, pTechno->GetType()->Strength);
    return 1;
}

// obj:GetOwner() -> house | nil
int Techno_GetOwner(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!IsValid(pTechno))
        return 0;
    return PushHouse(L, pTechno->Owner);
}

// obj:GetPosition() -> table {x, y, z} in map cells
int Techno_GetPosition(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!IsValid(pTechno))
        return 0;

    CoordStruct coords = pTechno->GetCoords();

    lua_createtable(L, 0, 3);
    lua_pushinteger(L, coords.X / 256);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, coords.Y / 256);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, coords.Z / 256);
    lua_setfield(L, -2, "z");
    return 1;
}

// obj:IsAlive() -> bool
int Techno_IsAlive(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    lua_pushboolean(L, IsValid(pTechno) && !pTechno->InLimbo ? 1 : 0);
    return 1;
}

// obj:GetDistanceTo(other_obj) -> number (in map cells)
int Techno_GetDistanceTo(lua_State* L) {
    auto* pSelf = CheckTechno(L, 1);

    void* ud = luaL_testudata(L, 2, kMetaName);
    if (!ud)
        return luaL_argerror(L, 2, "expected a techno object");

    auto* pOther = *static_cast<TechnoClass**>(ud);
    if (!IsValid(pSelf) || !IsValid(pOther)) {
        lua_pushnil(L);
        return 1;
    }

    CoordStruct a = pSelf->GetCoords();
    CoordStruct b = pOther->GetCoords();

    double dx = static_cast<double>(a.X - b.X) / 256.0;
    double dy = static_cast<double>(a.Y - b.Y) / 256.0;
    lua_pushnumber(L, std::sqrt(dx * dx + dy * dy));
    return 1;
}

// obj:TakeDamage(damage_amount) -> int remaining health
int Techno_TakeDamage(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!IsValid(pTechno)) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_Integer damage = luaL_checkinteger(L, 2);
    if (damage <= 0) {
        lua_pushinteger(L, pTechno->Health);
        return 1;
    }

    int remaining = pTechno->Health - static_cast<int>(damage);
    if (remaining < 0)
        remaining = 0;
    pTechno->Health = remaining;

    LUA_LOG_INFO("[Combat] {} took {} damage, HP remaining: {}", pTechno->GetType()->get_ID(), damage, remaining);
    lua_pushinteger(L, remaining);
    return 1;
}

// obj:Disable(duration_frames)
//
// Real EMP-style lock:
// - buildings: cut HasPower (drives IsPowerOnline(), so weapons stop firing)
//   AND call DisableStuff() (switched-off state);
// - feet: start the game's own ParalysisTimer (giant-squid mechanism)
//   AND set Deactivated.
// All state is restored automatically when the timer expires.
int Techno_Disable(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!IsValid(pTechno))
        return 0;

    lua_Integer frames = luaL_checkinteger(L, 2);
    if (frames <= 0)
        return 0;

    DisableEntry entry{};
    entry.ptr = pTechno;
    entry.expiryFrame = Unsorted::CurrentFrame + static_cast<unsigned int>(frames);

    if (pTechno->WhatAmI() == AbstractType::Building) {
        auto* pBuilding = static_cast<BuildingClass*>(pTechno);
        entry.isBuilding = true;
        entry.hadPower = pBuilding->HasPower;
        pBuilding->HasPower = false;      // IsPowerOnline() -> false: no firing
        pBuilding->DisableStuff();        // official switched-off state
        pTechno->Deactivated = true;
    } else {
        entry.isBuilding = false;
        entry.hadPower = true;
        // Units/infantry are always FootClass-derived.
        static_cast<FootClass*>(pTechno)->ParalysisTimer.Start(static_cast<int>(frames)); // native paralysis
        pTechno->Deactivated = true;
    }

    g_disabledEntries.push_back(entry);
    LUA_LOG_INFO("[Combat] EMP Lock applied to {} for {} frames", pTechno->GetType()->get_ID(), frames);
    return 0;
}

const luaL_Reg kTechnoMethods[] = {
    { "GetTypeName",   Techno_GetTypeName   },
    { "GetHealth",     Techno_GetHealth     },
    { "GetMaxHealth",  Techno_GetMaxHealth  },
    { "GetOwner",      Techno_GetOwner      },
    { "GetPosition",   Techno_GetPosition   },
    { "IsAlive",       Techno_IsAlive       },
    { "GetDistanceTo", Techno_GetDistanceTo },
    { "TakeDamage",    Techno_TakeDamage    },
    { "Disable",       Techno_Disable       },
    { nullptr, nullptr }
};

// --- World namespace -------------------------------------------------------

template <typename T>
int CollectArray(lua_State* L, DynamicVectorClass<T*>& array) {
    lua_createtable(L, static_cast<int>(array.Count), 0);
    int n = 0;
    for (int i = 0; i < array.Count; ++i) {
        T* pItem = array.GetItem(i);
        if (!pItem)
            continue;
        PushTechno(L, pItem);
        lua_seti(L, -2, ++n);
    }
    return 1;
}

// World.GetBuildings() -> table of techno objects
int World_GetBuildings(lua_State* L) {
    return CollectArray(L, BuildingClass::Array);
}

// World.GetUnits() -> table of techno objects
int World_GetUnits(lua_State* L) {
    return CollectArray(L, UnitClass::Array);
}

} // anonymous namespace

// Checks whether the pointer is still present in the engine's active object
// arrays. Only compares addresses - never dereferences ptr.
bool StillExists(TechnoClass* ptr) {
    if (!ptr)
        return false;

    for (int i = 0; i < BuildingClass::Array.Count; ++i)
        if (BuildingClass::Array.GetItem(i) == ptr) return true;
    for (int i = 0; i < UnitClass::Array.Count; ++i)
        if (UnitClass::Array.GetItem(i) == ptr) return true;
    for (int i = 0; i < InfantryClass::Array.Count; ++i)
        if (InfantryClass::Array.GetItem(i) == ptr) return true;
    for (int i = 0; i < AircraftClass::Array.Count; ++i)
        if (AircraftClass::Array.GetItem(i) == ptr) return true;

    return false;
}

void ProcessDisabledObjects(unsigned int currentFrame) {
    for (auto it = g_disabledEntries.begin(); it != g_disabledEntries.end();) {
        // Validate BEFORE any dereference: objects destroyed by damage/victory
        // are freed by the engine and must never be touched again.
        bool alive = StillExists(it->ptr) && it->ptr->Health > 0;
        if (!alive) {
            it = g_disabledEntries.erase(it); // dangling or dead: drop silently
            continue;
        }

        if (currentFrame >= it->expiryFrame) {
            if (it->isBuilding) {
                auto* pBuilding = static_cast<BuildingClass*>(it->ptr);
                pBuilding->EnableStuff();
                pBuilding->HasPower = it->hadPower; // restore pre-blackout state
                if (pBuilding->Deactivated)
                    pBuilding->Deactivated = false;
            } else if (it->ptr->Deactivated) {
                it->ptr->Deactivated = false; // ParalysisTimer expires on its own
            }
            LUA_LOG_INFO("[Combat] EMP Lock removed from {}", it->ptr->GetType()->get_ID());
            it = g_disabledEntries.erase(it);
        } else {
            ++it;
        }
    }
}

void PushTechno(lua_State* L, void* pTechno) {
    auto* ud = static_cast<void**>(lua_newuserdatauv(L, sizeof(void*), 0));
    *ud = pTechno;
    luaL_getmetatable(L, kMetaName);
    lua_setmetatable(L, -2);
}

void RegisterTechnoBindings(lua_State* L) {
    // Userdata metatable
    luaL_newmetatable(L, kMetaName);

    lua_newtable(L);
    luaL_setfuncs(L, kTechnoMethods, 0);
    lua_setfield(L, -2, "__index");

    lua_pop(L, 1); // pop metatable

    // Global "World" namespace
    lua_newtable(L);
    lua_pushcfunction(L, World_GetBuildings);
    lua_setfield(L, -2, "GetBuildings");
    lua_pushcfunction(L, World_GetUnits);
    lua_setfield(L, -2, "GetUnits");
    lua_setglobal(L, "World");
}

} // namespace LuaAPI
