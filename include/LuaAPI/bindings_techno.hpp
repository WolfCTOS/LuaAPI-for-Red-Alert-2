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


// Resets Input.WasKeyPressed edge-detect state (Gate 1.3).
void ClearKeyPrevState();

// M16 Gate 1: one-shot diagnostic scan of every UnitTypeClass turret voxel.
// Logs TypeID -> HVA FrameCount (and FireAngle) to LuaAPI.log once per session,
// guarded so it runs on the first logic frame after rules are loaded.
void LogTurretHvaFrameCounts();

} // namespace LuaAPI
