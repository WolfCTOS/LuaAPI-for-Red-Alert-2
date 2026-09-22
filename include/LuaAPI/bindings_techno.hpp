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

// Drops every pending timed-disable entry without touching the engine.
// Call on session reset: entries hold raw TechnoClass* from the previous
// match plus stale expiry frames (Gate 1.3).
void ClearDisabledObjects();

// Resets Input.WasKeyPressed edge-detect state (Gate 1.3).
void ClearKeyPrevState();

// M16 Gate 1: one-shot diagnostic scan of every UnitTypeClass turret voxel.
// Logs TypeID -> HVA FrameCount (and FireAngle) to LuaAPI.log once per session,
// guarded so it runs on the first logic frame after rules are loaded.
void LogTurretHvaFrameCounts();

} // namespace LuaAPI
