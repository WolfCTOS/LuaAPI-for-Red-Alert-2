#include "weapon_override.h"

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

#include <YRPP.h>

#include <MinHook.h>

#include <LuaAPI/logger.hpp>

#include <cstring>
#include <map>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace LuaAPI {
namespace WeaponOverride {

namespace {

// TechnoClass::GetPrimaryWeapon (alias of GetTurretWeapon). Verified JMP_THIS
// address in the vendored YRpp. Returns the WeaponStruct* the unit resolves for
// its primary weapon (already Primary vs ElitePrimary by veterancy).
constexpr uintptr_t kGetPrimaryWeaponAddr = 0x70E1A0;

// (type ID, veterancy level) -> weapon to force. Written from the Lua game
// thread and read from the weapon-resolution detour, which also runs on the
// game thread, so no locking is required.
std::map<std::pair<std::string, int>, WeaponTypeClass*> g_overrides;

// Rotation of scratch WeaponStructs handed back to the engine. Weapon resolution
// is synchronous, so a small ring is enough; the engine reads the struct
// immediately and never retains it across frames.
thread_local WeaponStruct g_scratch[8];
thread_local int g_scratchIdx = 0;

using GetPrimaryWeaponFn = WeaponStruct* (__fastcall*)(TechnoClass*);
GetPrimaryWeaponFn g_original = nullptr;

// Rookie / veteran / elite are the three veterancy levels the engine exposes.
constexpr const char* kVetNames[3] = { "rookie", "veteran", "elite" };

int VetLevelFromString(const char* s) {
    if (!s) return -1;
    for (int i = 0; i < 3; ++i) {
        if (_stricmp(s, kVetNames[i]) == 0) return i;
    }
    return -1;
}

// POD read-only snapshot of the weapon key. Kept in a POD struct so the SEH
// block below never crosses C++ object unwinding (the C2712 rule).
struct WeaponKey {
    char id[64];
    int vetLevel = 0;
    bool ok = false;
};

// SEH-guarded read of the firing techno's type ID + veterancy level. Never lets
// a dangling pointer crash the hook; returns ok=false on any fault.
static WeaponKey ReadWeaponKeySafe(TechnoClass* pThis) {
    WeaponKey k;
    if (!pThis) return k;

    __try {
        auto what = pThis->WhatAmI();
        if (what != AbstractType::Building &&
            what != AbstractType::Unit &&
            what != AbstractType::Infantry &&
            what != AbstractType::Aircraft) {
            return k;
        }
        if (!pThis->IsAlive || pThis->Health <= 0 || pThis->InLimbo)
            return k;

        auto* pType = pThis->GetType();
        if (!pType) return k;

        const char* id = pType->get_ID();
        if (!id || id[0] == '\0') return k;

        strncpy_s(k.id, sizeof(k.id), id, _TRUNCATE);

        const auto& v = pThis->Veterancy;
        if (v.IsElite())
            k.vetLevel = 2;
        else if (v.IsVeteran())
            k.vetLevel = 1;
        else
            k.vetLevel = 0;

        k.ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        k.ok = false;
    }

    return k;
}

// Returns a scratch WeaponStruct* with the overridden WeaponType, or nullptr
// when no override applies (caller then falls back to the original). The map
// lookup runs OUTSIDE __try: std::string/std::map is not SEH-visible here.
static WeaponStruct* ResolveOverride(TechnoClass* pThis, WeaponStruct* pOriginal) {
    if (!pOriginal) return nullptr;

    WeaponKey key = ReadWeaponKeySafe(pThis);
    if (!key.ok) return nullptr;

    auto it = g_overrides.find(std::make_pair(std::string(key.id), key.vetLevel));
    if (it == g_overrides.end() || !it->second)
        return nullptr;

    // Keep the engine's muzzle geometry, only swap the fired weapon.
    WeaponStruct* pScratch = &g_scratch[g_scratchIdx];
    g_scratchIdx = (g_scratchIdx + 1) & 7;

    pScratch->WeaponType     = it->second;
    pScratch->FLH            = pOriginal->FLH;
    pScratch->BarrelLength   = pOriginal->BarrelLength;
    pScratch->BarrelThickness = pOriginal->BarrelThickness;
    pScratch->TurretLocked   = pOriginal->TurretLocked;
    return pScratch;
}

// __fastcall detour for a __thiscall member function (this in ECX).
WeaponStruct* __fastcall Hooked_GetPrimaryWeapon(TechnoClass* pThis, void* /*edx*/) {
    // Run the vanilla resolver first so it is ALWAYS the source of truth.
    WeaponStruct* pOriginal = g_original ? g_original(pThis) : nullptr;
    if (!pOriginal || g_overrides.empty())
        return pOriginal;

    WeaponStruct* pOverride = ResolveOverride(pThis, pOriginal);
    return pOverride ? pOverride : pOriginal;
}

void RegisterBinding(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
}

// Tiny SEH helper: __try must live in a function with no C++ locals needing
// destructors (the C2712 rule). Returns nullptr on fault.
static WeaponTypeClass* FindWeaponSafe(const char* id) {
    WeaponTypeClass* pWeapon = nullptr;
    __try { pWeapon = WeaponTypeClass::Find(id); }
    __except (EXCEPTION_EXECUTE_HANDLER) { pWeapon = nullptr; }
    return pWeapon;
}

// Tiny SEH helper: read a weapon's rules ID. Returns nullptr on fault.
static const char* WeaponIdSafe(WeaponTypeClass* pWeapon) {
    const char* id = nullptr;
    __try { id = pWeapon ? pWeapon->get_ID() : nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { id = nullptr; }
    return id;
}

// WeaponOverride.Set(typeId, vetLevel, weaponId) -> bool
int WOverride_Set(lua_State* L) {
    const char* typeId = luaL_checkstring(L, 1);
    const char* vetStr = luaL_checkstring(L, 2);
    const char* weaponId = luaL_checkstring(L, 3);

    int level = VetLevelFromString(vetStr);
    if (!typeId || !typeId[0] || level < 0 || !weaponId || !weaponId[0]) {
        lua_pushboolean(L, 0);
        return 1;
    }

    WeaponTypeClass* pWeapon = FindWeaponSafe(weaponId);
    if (!pWeapon) {
        LUA_LOG_WARN("[WeaponOverride] '{}' [{}]: unknown weapon '{}'", typeId, vetStr, weaponId);
        lua_pushboolean(L, 0);
        return 1;
    }

    // No __try here: std::string/std::pair temporaries need C++ unwinding.
    g_overrides[std::make_pair(std::string(typeId), level)] = pWeapon;
    LUA_LOG_INFO("[WeaponOverride] '{}' [{}] -> weapon '{}'", typeId, vetStr, weaponId);
    lua_pushboolean(L, 1);
    return 1;
}

// WeaponOverride.Get(typeId, vetLevel) -> string | nil
int WOverride_Get(lua_State* L) {
    const char* typeId = luaL_checkstring(L, 1);
    const char* vetStr = luaL_checkstring(L, 2);

    int level = VetLevelFromString(vetStr);
    if (!typeId || !typeId[0] || level < 0) {
        lua_pushnil(L);
        return 1;
    }

    auto it = g_overrides.find(std::make_pair(std::string(typeId), level));
    if (it == g_overrides.end() || !it->second) {
        lua_pushnil(L);
        return 1;
    }

    const char* id = WeaponIdSafe(it->second);
    if (id) lua_pushstring(L, id);
    else    lua_pushnil(L);
    return 1;
}

// WeaponOverride.Clear([typeId [, vetLevel]]) -> nil
int WOverride_Clear(lua_State* L) {
    if (lua_gettop(L) == 0) {
        g_overrides.clear();
        LUA_LOG_INFO("[WeaponOverride] cleared all overrides");
        return 0;
    }

    const char* typeId = luaL_checkstring(L, 1);
    int n = lua_gettop(L);

    if (n < 2) {
        for (auto it = g_overrides.begin(); it != g_overrides.end();) {
            if (it->first.first == typeId)
                it = g_overrides.erase(it);
            else
                ++it;
        }
        return 0;
    }

    int level = VetLevelFromString(luaL_checkstring(L, 2));
    if (level >= 0)
        g_overrides.erase(std::make_pair(std::string(typeId), level));
    return 0;
}

} // namespace

bool Install() {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kGetPrimaryWeaponAddr), 64,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("[WeaponOverride] VirtualProtect(GetPrimaryWeapon 0x{:X}) failed (error {})",
                     kGetPrimaryWeaponAddr, GetLastError());
    }

    MH_STATUS st = MH_CreateHook(
        reinterpret_cast<LPVOID>(kGetPrimaryWeaponAddr),
        reinterpret_cast<LPVOID>(&Hooked_GetPrimaryWeapon),
        reinterpret_cast<LPVOID*>(&g_original));
    LUA_LOG_INFO("[WeaponOverride] MH_CreateHook(GetPrimaryWeapon @ 0x{:X}) -> {} ({})",
                 kGetPrimaryWeaponAddr, MH_StatusToString(st), static_cast<int>(st));

    if (st != MH_OK) {
        LUA_LOG_WARN("[WeaponOverride] GetPrimaryWeapon hook NOT installed (degraded: vanilla only)");
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kGetPrimaryWeaponAddr));
    LUA_LOG_INFO("[WeaponOverride] MH_EnableHook(GetPrimaryWeapon) -> {} ({})",
                 MH_StatusToString(st), static_cast<int>(st));

    if (st != MH_OK) {
        LUA_LOG_WARN("[WeaponOverride] GetPrimaryWeapon hook could not be enabled (degraded: vanilla only)");
        return false;
    }

    LUA_LOG_INFO("[WeaponOverride] GetPrimaryWeapon hook installed successfully");
    return true;
}

void ClearAll() {
    g_overrides.clear();
}

void RegisterBindings(lua_State* L) {
    lua_newtable(L);
    RegisterBinding(L, "Set",   WOverride_Set);
    RegisterBinding(L, "Get",   WOverride_Get);
    RegisterBinding(L, "Clear", WOverride_Clear);
    lua_setglobal(L, "WeaponOverride");
}

} // namespace WeaponOverride
} // namespace LuaAPI
