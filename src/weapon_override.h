#pragma once

#include <windows.h>

struct lua_State;

namespace LuaAPI {
namespace WeaponOverride {

// Installs the MinHook detour on TechnoClass::GetPrimaryWeapon (0x70E1A0).
// The vanilla function remains the source of truth: when no Lua override is
// registered for a (type, veterancy) pair, the original result is returned
// unchanged. Returns false (degraded, vanilla untouched) if the hook could
// not be installed.
bool Install();

// Removes every registered weapon override. Called on session reset so the
// override map never leaks across map/mission reloads.
void ClearAll();

// Registers the global "WeaponOverride" table:
//   Set(typeId, vetLevel, weaponId) -> bool
//   Get(typeId, vetLevel)          -> string | nil
//   Clear([typeId [, vetLevel]])   -> nil
void RegisterBindings(lua_State* L);

} // namespace WeaponOverride
} // namespace LuaAPI
