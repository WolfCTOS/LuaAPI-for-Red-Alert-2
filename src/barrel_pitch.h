#pragma once

#include <windows.h>

struct lua_State;

namespace LuaAPI {
namespace BarrelPitch {

// M16 path B: draw-time barrel pitch injection.
//
// Installs a MinHook detour on UnitClass::DrawAsVXL (0x73B470, verified
// JMP_THIS address in the vendored YRpp). Around each draw call the detour
// temporarily swaps TechnoTypeClass::FireAngle for an overriden value derived
// from the per-unit Lua pitch (degrees), so the engine's own FireAngle math
// orients the barrel - no matrix reversal needed. The type field is restored
// immediately after the original draw returns, so every other unit of the
// same type keeps the vanilla value.
//
// Draw-only and client-local: it never touches simulation state, which keeps
// CnCNet multiplayer deterministic.
//
// Returns false (degraded, vanilla untouched) if the hook could not be
// installed.
bool Install();

// Removes every per-unit pitch override. Called on session reset so overrides
// never leak across map/mission reloads.
void ClearAll();

// AUTO mode for one unit: the pitch is computed natively per draw call from
// the live target distance (8..55 degrees across 4..14 cells, AutoPitchDegrees
// in barrel_pitch.cpp). While enabled it takes precedence over any manual
// override for that unit; with no live target nothing is drawn elevated.
// Gate 2B lesson: the earlier "send -1 as a flag" design silently stored -1
// as a manual override and starved the AUTO branch - never do that again.
void SetAuto(unsigned int unitId, bool enabled);

// Global AUTO mode (user request: "no hotkeys, the tank AI decides itself"):
// every unit that reaches the DrawAsVXL detour with a voxel turret gets the
// distance-based AUTO pitch - player units and AI units alike, no per-unit
// registration needed. Units without a live target still draw vanilla.
void SetAutoAll(bool enabled);

// PERSISTENT static test (Gate 2B follow-up): writes `degrees` into the
// armed unit's TechnoTypeClass::FireAngle ONCE and leaves it there, so every
// unit of that type renders with the field held at the test value across
// frames and facings - the same engine state a static rules/art edit would
// produce, but logged and reversible in-session. Clearing restores the value
// captured at write time. Distinguishes "consumption lives in a cache
// refresh that never sees a transient per-draw swap" from "the vehicle
// render path does not consume the field at all".
// Returns false if the request is malformed (id 0, degrees out of range).
void SetPersistent(unsigned int unitId, double degrees);
void ClearPersistent(unsigned int unitId);

// Bounty mark overlay (Gate 2A): draw-only rectangle + "BOUNTY" label for one
// or more UnitClass objects, keyed by UniqueID (never raw pointers).
// Diagnostic draw mode (crash isolation A-E): 0=off (registry live, no
// pixels), 1=rect only, 2=text only, 3=full (default). Checked in the
// detour; switched live via Engine.SetBountyDrawMode without rebuilds.
void SetBountyDrawMode(int mode);
// Drawn inside the existing DrawAsVXL detour AFTER the original draw call;
// touches no simulation state (no health/mission/money/target writes).
// Marks die by expiry frame, explicit clear, or ClearAll() on session reset.
void MarkBounty(unsigned int unitId, unsigned int color, unsigned int durationFrames);
void ClearBountyMark(unsigned int unitId);
void ClearBountyMarks();

// Extends the global "Engine" table (must already exist):
//   Engine.SetBarrelPitchOverride(unitId, pitchDegrees)  -> bool
//   Engine.GetBarrelPitchOverride(unitId)                -> number | nil
//   Engine.SetBarrelPitchAuto(unitId, enabled)           -> bool
//   Engine.GetBarrelPitchAuto(unitId)                    -> enabled, degrees | nil
//   Engine.SetBarrelPitchAutoAll(enabled)                -> bool  (global AUTO)
//   Engine.SetPersistentBarrelPitch(unitId, degrees)     -> bool
//   Engine.ClearPersistentBarrelPitch(unitId)            -> bool (restored)
//   Engine.ClearBarrelPitchOverride(unitId)              -> nil (clears all)
//   Engine.ClearAllBarrelPitchOverrides()                -> nil (clears all)
//   Engine.ClearBountyMarks()                             -> nil
//   Engine.SetBountyDrawMode(mode)                        -> bool (0..3)
void RegisterBindings(lua_State* L);

} // namespace BarrelPitch
} // namespace LuaAPI
