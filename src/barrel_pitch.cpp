#include "barrel_pitch.h"

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

#include <YRPP.h>

#include <MinHook.h>

#include <LuaAPI/logger.hpp>

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace LuaAPI {
namespace BarrelPitch {

namespace {

// UnitClass::DrawAsVXL - main voxel draw entry for ground/naval vehicles.
// Verified JMP_THIS address in the vendored YRpp (UnitClass.h).
constexpr uintptr_t kDrawAsVXLAddr = 0x73B470;

// Per-unit pitch overrides in degrees (positive = barrel up).
// Written from the Lua game thread; read from the draw detour, which also
// runs on the game thread, so no locking is required.
std::unordered_map<unsigned int, float> g_overrides;

// Units in AUTO mode: the pitch is computed per draw call from the live
// target distance (AutoPitchDegrees below), not stored as a number.
// Gate 2B lesson: the first probe encoded AUTO as a -1 manual override,
// which silently won the manual branch in DecidePitch and starved the
// distance math - the user saw "barrel moves, AUTO never reacts". Keep the
// flag and the value in separate structures.
std::unordered_set<unsigned int> g_autoUnits;

// Global AUTO switch: when true, EVERY unit with a voxel turret that reaches
// the draw detour gets the distance-based AUTO pitch - player units and AI
// units alike, no per-unit registration. This is the "no hotkeys, the tank
// decides itself" mode.
bool g_autoAll = false;

// Last computed AUTO pitch per unit (degrees), updated by the detour each
// draw while a live target exists; erased when the target drops. Purely
// diagnostic: lets Lua read what AUTO is currently doing.
std::unordered_map<unsigned int, float> g_lastAutoPitch;

// Bounty marks (Gate 2A): UniqueID -> {draw color, expiry logical frame}.
// Keyed by ID, never by raw pointer: a destroyed unit simply stops drawing,
// so a stale entry can never cause invalid memory access. durationFrames == 0
// means "until explicitly cleared". Written from the Lua game thread, read
// from the draw detour on the same thread - no locking.
struct BountyMark {
    unsigned int color = 0x00FF00;
    unsigned int untilFrame = 0xFFFFFFFFu;
};
std::unordered_map<unsigned int, BountyMark> g_bountyMarks;

// Diagnostic draw mode (crash isolation A-E): 0=off, 1=rect only,
// 2=text only, 3=full. Read in the detour on the game thread, written
// from the Lua game thread (same thread) or the debug console - no lock.
int g_bountyDrawMode = 3;

// Persistent static test (Gate 2B follow-up): one entry per touched type,
// with the FireAngle captured before the first test write. TechnoTypeClass
// objects are static game data (allocated at rules load, never freed
// mid-session), so raw pointers stay valid; every write/restore still goes
// through the SEH-guarded helpers.
struct PersistentSave {
    TechnoTypeClass* pType;
    int originalFireAngle;
};
std::vector<PersistentSave> g_persistentSaves;

using DrawAsVXLFn = void(__fastcall*)(UnitClass*, void*, Point2D, RectangleStruct, int, int);
DrawAsVXLFn g_original = nullptr;

// ---------------------------------------------------------------------------
// SEH helpers: every engine dereference inside the detour goes through one of
// these tiny functions (C2712: __try must not share a frame with C++ objects).
// ---------------------------------------------------------------------------

struct UnitInfo {
    bool ok = false;
    unsigned int id = 0;
    int fireAngle = 0;          // Type->FireAngle (0..64 scale, may be negative)
    bool hasTurret = false;
    CoordStruct coords;
    AbstractClass* pTarget = nullptr; // raw; validated before use
    unsigned int targetId = 0;
    bool targetAlive = false;
    bool targetIsCell = false;  // ground force-fire target (CellClass)
    CoordStruct targetCoords;
    int barrelFacingRaw = 0;    // BarrelFacing.Current().Raw (BAM, yaw only)
};

static UnitInfo ReadUnitInfoSafe(UnitClass* pUnit) {
    UnitInfo info;
    if (!pUnit) return info;

    __try {
        auto what = pUnit->WhatAmI();
        if (what != AbstractType::Unit)
            return info;
        if (!pUnit->IsAlive || pUnit->Health <= 0 || pUnit->InLimbo)
            return info;

        auto* pType = pUnit->Type;
        if (!pType) return info;

        info.id = pUnit->UniqueID;
        info.fireAngle = pType->FireAngle;
        info.hasTurret = pType->Turret;
        info.coords = pUnit->GetCoords();

        info.barrelFacingRaw = pUnit->BarrelFacing.Current().Raw;

        AbstractClass* pTarget = pUnit->Target;
        if (pTarget) {
            auto tWhat = pTarget->WhatAmI();
            if (tWhat == AbstractType::Building || tWhat == AbstractType::Unit ||
                tWhat == AbstractType::Infantry || tWhat == AbstractType::Aircraft) {
                auto* pTargetTechno = static_cast<TechnoClass*>(pTarget);
                if (pTargetTechno->Health > 0 && !pTargetTechno->InLimbo) {
                    info.pTarget = pTarget;
                    info.targetId = pTargetTechno->UniqueID;
                    info.targetAlive = true;
                    info.targetCoords = pTargetTechno->GetCoords();
                }
            } else if (tWhat == AbstractType::Cell) {
                // Force-fire at ground keeps a CellClass in Target (session
                // 12:30: every AUTO heartbeat showed target=no while the
                // player force-fired at terrain). A cell is a legitimate
                // AUTO pitch input - read its coords the same way.
                // Draw-only: this never makes the unit shoot.
                auto* pCell = static_cast<CellClass*>(pTarget);
                info.targetCoords = pCell->GetCoords();
                info.targetAlive = true;
                info.targetIsCell = true;
            }
        }

        info.ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        info.ok = false;
    }

    return info;
}

// type->FireAngle write with restore guarantee is done by the caller; this
// helper only performs the guarded store.
static bool WriteFireAngleSafe(TechnoTypeClass* pType, int value) {
    bool ok = false;
    __try {
        pType->FireAngle = value;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static bool ReadFireAngleSafe(TechnoTypeClass* pType, int* pOut) {
    bool ok = false;
    __try {
        *pOut = pType->FireAngle;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static TechnoTypeClass* GetTypeSafe(UnitClass* pUnit) {
    TechnoTypeClass* pType = nullptr;
    __try { pType = pUnit->Type; }
    __except (EXCEPTION_EXECUTE_HANDLER) { pType = nullptr; }
    return pType;
}

// ---------------------------------------------------------------------------
// Bounty mark overlay (Gate 2A, draw-only)
// ---------------------------------------------------------------------------

// Reads the current logical frame without C++ objects (C2712-safe caller).
static unsigned int CurrentFrameSafe() {
    unsigned int f = 0;
    __try { f = Unsorted::CurrentFrame; }
    __except (EXCEPTION_EXECUTE_HANDLER) { f = 0; }
    return f;
}

// SEH-guarded presence probe over UnitClass::Array (tiny helper: no map ops
// in this frame, so C2712 cannot trigger). On SEH returns true (keep mark;
// expiry/explicit clear still applies).
static bool BountyIdPresentSafe(unsigned int unitId) {
    bool found = false;
    __try {
        for (int i = 0; i < UnitClass::Array.Count; ++i) {
            UnitClass* pObj = UnitClass::Array.GetItem(i);
            if (pObj && pObj->UniqueID == unitId) { found = true; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        found = true;
    }
    return found;
}

// Pure draw work: POD params only, everything engine-touching inside __try,
// no map/C++-object operations here (C2712).
//
// Gate 3A RCA: BoundingRect is the redraw CLIP rectangle (view-sized), NOT
// the unit's screen box — drawing it produced a screen-sized outline. The
// marker is therefore constructed around Coords, the unit's screen draw
// anchor (the voxel is blitted at Coords, so it tracks the unit by
// construction — the same convention the engine's own health-bar/pip drawing
// uses: position from pLocation, fixed offsets). Half-extents are a chosen
// marker size (~Rhino footprint), NOT derived from BoundingRect.
//
// Gate 3A flicker probe: the scene may be composed on Composite and blitted
// to Primary (or vice versa), so a Primary-only overlay can be erased by the
// compose pass. Until the surface topology is proven by the frame probe
// below, the mark is painted on BOTH Primary and Composite (whichever exist
// and differ) — negligible cost for a single target, strictly temporary
// belt-and-braces to be removed once the probe log identifies the live path.
// Probe verdict 2026-09-21: Composite pointer ALTERNATES every frame while
// Primary/Hidden/Alternate stay stable (live log) — the compose path is
// live, so Composite painting stays; Primary painting is kept as the
// harmless supplement (confined to the tactical view by the clip below).

// Gate 3A ghost RCA: the raw DrawRect/DrawText calls are UNCLIPPED, while
// the engine clips all its own drawing to the tactical viewport. When the
// bounty unit sits at the view edge, marker pixels land on the static HUD
// (sidebar/command bar), which is never repainted on camera moves — stale
// ghost pixels. Fix: intersect the marker with DSurface::ViewBounds (the
// engine's own convention, cf. DrawDashed) and skip paint/text outside it.
static void DrawBountyOverlaySafe(unsigned int unitId, unsigned int color,
                                  Point2D Coords, RectangleStruct BoundingRect,
                                  int mode, unsigned* pPaintedMask) {
    (void)unitId;
    (void)BoundingRect;
    constexpr int kHalfW = 36;
    constexpr int kHalfH = 28;
    // Color RCA (Gate 3A regression): DrawText honors COLORREF, but DrawRect
    // consumes a RAW 16-bit 5-6-5 value in the low word — proven by two live
    // observations sharing low word 0xFF00 and the same yellow-orange:
    // 0xFFFF00 (old) and 0x00FF00 (green COLORREF, displayed yellow). 565
    // 0xFF00 = R31/G56/B0 = yellow-orange. Layout per vendored
    // Color16Struct {B:5, G:6, R:5} (BasicStructures.h). So the COLORREF is
    // converted for the rect (green 0x00FF00 -> 0x07E0) and passed as-is to
    // the text. No channel guessing: the conversion is the documented
    // 565 packing of the Caller-supplied COLORREF.
    const unsigned int r5 = (GetRValue(color) >> 3) & 0x1Fu;
    const unsigned int g6 = (GetGValue(color) >> 2) & 0x3Fu;
    const unsigned int b5 = (GetBValue(color) >> 3) & 0x1Fu;
    const DWORD rectColor = static_cast<DWORD>((r5 << 11) | (g6 << 5) | b5);
    __try {
        const RectangleStruct v = DSurface::ViewBounds;
        int x1 = Coords.X - kHalfW;
        int y1 = Coords.Y - kHalfH;
        int x2 = Coords.X + kHalfW;
        int y2 = Coords.Y + kHalfH;
        if (x1 < v.X) x1 = v.X;
        if (y1 < v.Y) y1 = v.Y;
        if (x2 > v.X + v.Width) x2 = v.X + v.Width;
        if (y2 > v.Y + v.Height) y2 = v.Y + v.Height;
        if (x2 <= x1 || y2 <= y1)
            return; // marker fully outside the tactical view: paint nothing
        RectangleStruct r;
        r.X = x1;
        r.Y = y1;
        r.Width = x2 - x1;
        r.Height = y2 - y1;
        int tx = Coords.X - 30;
        int ty = Coords.Y - kHalfH - 16;
        const bool textInView = (tx >= v.X && tx < v.X + v.Width
            && ty >= v.Y && ty < v.Y + v.Height);
        DSurface* pP = DSurface::Primary;
        DSurface* pC = DSurface::Composite;
        const bool doRect = (mode != 2);
        const bool doText = (mode != 1);
        unsigned painted = 0;
        if (pP) {
            if (doRect)
                pP->DrawRect(&r, rectColor);
            if (doText && textInView)
                pP->DrawText(L"BOUNTY", tx, ty, static_cast<COLORREF>(color));
            painted |= 1;
        }
        if (pC && pC != pP) {
            if (doRect)
                pC->DrawRect(&r, rectColor);
            if (doText && textInView)
                pC->DrawText(L"BOUNTY", tx, ty, static_cast<COLORREF>(color));
            painted |= 2;
        }
        if (pPaintedMask)
            *pPaintedMask = painted;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Overlay is cosmetic; a draw failure must never break the unit draw.
    }
}

// TEMPORARY flicker diagnostics (Gate 3A): one line per NEW logical frame
// while a mark is live. Consecutive frame numbers prove the overlay is
// invoked every frame (=> flicker is post-draw erasure, not skipped calls);
// alternating surface pointers prove flipping surfaces. Removed once the
// RCA is confirmed by a live log.
static void LogBountyFrameProbe(unsigned int unitId, bool pitched,
                                Point2D Coords, RectangleStruct BoundingRect,
                                size_t regSize, unsigned painted) {
    const unsigned int cur = CurrentFrameSafe();
    static unsigned int s_lastLoggedFrame = 0xFFFFFFFFu;
    if (cur == s_lastLoggedFrame || cur == 0)
        return;
    s_lastLoggedFrame = cur;
    uintptr_t pP = 0, pC = 0, pH = 0, pA = 0;
    __try {
        if (DSurface::Primary)
            pP = reinterpret_cast<uintptr_t>(DSurface::Primary);
        if (DSurface::Composite)
            pC = reinterpret_cast<uintptr_t>(DSurface::Composite);
        if (DSurface::Hidden)
            pH = reinterpret_cast<uintptr_t>(DSurface::Hidden);
        if (DSurface::Alternate)
            pA = reinterpret_cast<uintptr_t>(DSurface::Alternate);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    // Throttle (2026-09-21): the per-frame flood hid the signal in the crash
    // session (hundreds of identical lines/sec). Log only transitions
    // (tracked unit / painted mask / registry size) plus a 300-frame
    // heartbeat. Surface pointers are DELIBERATELY excluded: Primary/
    // Composite alternate every frame by design (double buffering), so
    // comparing them would log every frame anyway. Flicker RCA keeps full
    // value: every paint transition is still recorded with its frame number.
    // (2026-09-21 p.m.: first throttle revision compared pP/pC and was a
    // no-op in live sessions for exactly this reason.)
    static unsigned int s_unit = 0, s_painted = 0xFFFFFFFFu,
                        s_reg = 0xFFFFFFFFu, s_heartbeat = 0;
    const bool changed = (unitId != s_unit) || (painted != s_painted) ||
        (static_cast<unsigned>(regSize) != s_reg);
    const bool heartbeat = (cur - s_heartbeat >= 300);
    if (!changed && !heartbeat)
        return;
    s_unit = unitId;
    s_painted = painted;
    s_reg = static_cast<unsigned>(regSize);
    if (heartbeat)
        s_heartbeat = cur;
    LUA_LOG_INFO("[Bounty] frame={} id={} path={} coords=({},{}) clip=({},{},{},{}) surfP={:#x} surfC={:#x} surfH={:#x} surfA={:#x} reg={} painted={}",
                 cur, unitId, pitched ? "pitched" : "vanilla",
                 Coords.X, Coords.Y,
                 BoundingRect.X, BoundingRect.Y,
                 BoundingRect.Width, BoundingRect.Height,
                 pP, pC, pH, pA, regSize, painted);
}

// Registry lookup + expiry (map ops OUTSIDE __try), then probe + safe draw.
// Draw mode 0 suppresses pixels but the probe still logs, so Test B shows
// a live registry with zero draw operations.
static void DrawBountyIfMarked(unsigned int unitId, bool pitched,
                               Point2D Coords, RectangleStruct BoundingRect) {
    if (unitId == 0)
        return;
    auto it = g_bountyMarks.find(unitId);
    if (it == g_bountyMarks.end())
        return;
    const unsigned int color = it->second.color;
    const unsigned int until = it->second.untilFrame;
    if (CurrentFrameSafe() >= until) {
        g_bountyMarks.erase(it); // lazy expiry, no Lua round-trip needed
        return;
    }
    const size_t regSize = g_bountyMarks.size();
    unsigned painted = 0;
    if (g_bountyDrawMode != 0) {
        DrawBountyOverlaySafe(unitId, color, Coords, BoundingRect,
                              g_bountyDrawMode, &painted);
    }
    LogBountyFrameProbe(unitId, pitched, Coords, BoundingRect, regSize, painted);
}

// ---------------------------------------------------------------------------
// Pitch derivation
// ---------------------------------------------------------------------------

// The FireAngle scale used by art/rules is 0 (horizontal) .. 64 (vertical),
// signed, and the engine converts it internally. Our override is specified in
// degrees (0 = horizontal, 90 = straight up) and mapped onto that scale.
constexpr double kDegToFireAngle = 64.0 / 90.0;

// Distance-based default curve for AUTO mode: low pitch at close range,
// higher pitch at long range. Deliberately simple and documented as a
// starting point for tuning, not as a ballistic solver.
constexpr double kAutoMinCells = 4.0;   // <= this distance -> min pitch
constexpr double kAutoMaxCells = 14.0;  // >= this distance -> max pitch
constexpr double kAutoMinPitchDeg = 8.0;
constexpr double kAutoMaxPitchDeg = 55.0;

static double AutoPitchDegrees(double distCells) {
    if (distCells <= kAutoMinCells) return kAutoMinPitchDeg;
    if (distCells >= kAutoMaxCells) return kAutoMaxPitchDeg;

    const double t = (distCells - kAutoMinCells) / (kAutoMaxCells - kAutoMinCells);
    return kAutoMinPitchDeg + t * (kAutoMaxPitchDeg - kAutoMinPitchDeg);
}

enum class PitchSource { None, Manual, Auto };

struct PitchDecision {
    PitchSource source = PitchSource::None;
    float degrees = 0.0f;
};

// Shared AUTO branch: converts the live target distance into a pitch decision
// (used by both the global and the per-unit AUTO path). Without a live target
// the decision stays None -> the unit draws vanilla.
static void ApplyAutoPitch(const UnitInfo& info, PitchDecision& d) {
    if (!info.targetAlive)
        return;

    const double dx = static_cast<double>(info.targetCoords.X - info.coords.X);
    const double dy = static_cast<double>(info.targetCoords.Y - info.coords.Y);
    // Lepton squared distances overflow int32 on big maps (AGENTS.md);
    // compute in doubles and convert to cells (256 leptons per cell).
    const double distCells = std::sqrt(dx * dx + dy * dy) / 256.0;
    d.source = PitchSource::Auto;
    d.degrees = static_cast<float>(AutoPitchDegrees(distCells));
}

// Decides the effective pitch for this unit this frame. Map lookups and
// std:: math run OUTSIDE __try (C2712); engine derefs stay inside helpers.
static PitchDecision DecidePitch(const UnitInfo& info) {
    PitchDecision d;

    if (!info.ok || !info.hasTurret)
        return d;

    // 1. Global AUTO mode: every voxel-turret unit gets the distance-based
    //    pitch - player and AI alike, no per-unit registration. This is the
    //    "no hotkeys, the tank decides itself" mode.
    if (g_autoAll) {
        ApplyAutoPitch(info, d);
        return d;
    }

    // 2. Per-unit AUTO mode: pitch computed per draw call from the live
    //    target distance. Enabled is independent of target presence; without
    //    a live target the unit draws vanilla (no stale last-angle).
    if (g_autoUnits.count(info.id)) {
        ApplyAutoPitch(info, d);
        return d;
    }

    // 3. Explicit Lua override for this unit id.
    auto it = g_overrides.find(info.id);
    if (it != g_overrides.end()) {
        d.source = PitchSource::Manual;
        d.degrees = it->second;
    }

    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Detour
// ---------------------------------------------------------------------------
//
// __fastcall detour for a __thiscall member (this in ECX, edx placeholder).
// SINGLE-DRAW variant (switched after Gate 2B sessions 12:46/12:29: the
// double-draw "elevated ghost on top of vanilla" probe proved the mechanism
// but is visually ambiguous at moderate angles - two nearly-coinciding
// silhouettes read as "nothing changed"). Sequence per call now:
//   1. decide the pitch BEFORE drawing (safe readers only, no engine writes);
//   2. if a pitch applies, swap Type->FireAngle to the override;
//   3. call the original DrawAsVXL exactly once - the barrel itself renders
//      with the swapped orientation, native engine math;
//   4. restore the type field unconditionally, so no other unit of the same
//      type is affected and no state leaks.
void __fastcall Hooked_DrawAsVXL(UnitClass* pThis, void* /*edx*/,
                                 Point2D Coords, RectangleStruct BoundingRect,
                                 int Brightness, int Tint) {
    if (!g_original) {
        return;
    }

    UnitInfo info = ReadUnitInfoSafe(pThis);
    PitchDecision d; // source None unless info.ok and a branch applies
    if (info.ok)
        d = DecidePitch(info);

    if (d.source == PitchSource::None) {
        if (info.ok)
            g_lastAutoPitch.erase(info.id); // AUTO unit lost its target
        __try {
            g_original(pThis, nullptr, Coords, BoundingRect, Brightness, Tint);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LUA_LOG_WARN("[BarrelPitch] SEH during vanilla draw (unit {})",
                         info.id);
        }
        DrawBountyIfMarked(info.id, false, Coords, BoundingRect);
        return;
    }

    if (d.source == PitchSource::Auto)
        g_lastAutoPitch[info.id] = d.degrees;

    TechnoTypeClass* pType = GetTypeSafe(pThis);
    int originalFireAngle = 0;
    bool swapped = false;

    if (pType && ReadFireAngleSafe(pType, &originalFireAngle)) {
        // Degrees -> engine FireAngle scale. Negative Lua values are allowed
        // and map to negative FireAngle (barrel below horizontal).
        int fireAngleOverride = static_cast<int>(std::lround(
            static_cast<double>(d.degrees) * kDegToFireAngle));
        fireAngleOverride = std::min(64, std::max(-64, fireAngleOverride));

        if (fireAngleOverride != originalFireAngle)
            swapped = WriteFireAngleSafe(pType, fireAngleOverride);
    }

    __try {
        g_original(pThis, nullptr, Coords, BoundingRect, Brightness, Tint);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LUA_LOG_WARN("[BarrelPitch] SEH during pitched draw (unit {})",
                     info.id);
    }

    // Restore the type field no matter what happened above, so the next unit
    // of the same type draws with the vanilla value and no state leaks.
    if (swapped)
        WriteFireAngleSafe(pType, originalFireAngle);

    DrawBountyIfMarked(info.id, true, Coords, BoundingRect);

    // Rate-limited diagnostic: proves per draw call that the swap actually
    // happened for this unit (the user-facing "is it drawing at all" signal
    // Gate 2B lacked when the double-draw ghost confused the visual verdict).
    if (swapped) {
        static DWORD s_lastLogTick = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastLogTick >= 1000) {
            s_lastLogTick = now;
            LUA_LOG_INFO("[BarrelPitch] draw id={} src={} pitch={:.1f}deg fireAngle={} (vanilla {})",
                         info.id,
                         d.source == PitchSource::Manual ? "manual" : "auto",
                         d.degrees,
                         std::min(64, std::max(-64, static_cast<int>(std::lround(
                             static_cast<double>(d.degrees) * kDegToFireAngle)))),
                         originalFireAngle);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool Install() {
    if (g_original)
        return true;

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kDrawAsVXLAddr), 64,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("[BarrelPitch] VirtualProtect(DrawAsVXL 0x{:X}) failed (error {})",
                     kDrawAsVXLAddr, GetLastError());
    }

    MH_STATUS st = MH_CreateHook(
        reinterpret_cast<LPVOID>(kDrawAsVXLAddr),
        reinterpret_cast<LPVOID>(&Hooked_DrawAsVXL),
        reinterpret_cast<LPVOID*>(&g_original));
    if (st != MH_OK) {
        LUA_LOG_WARN("[BarrelPitch] MH_CreateHook(DrawAsVXL 0x{:X}) -> {} ({})",
                     kDrawAsVXLAddr, MH_StatusToString(st), static_cast<int>(st));
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kDrawAsVXLAddr));
    if (st != MH_OK) {
        LUA_LOG_WARN("[BarrelPitch] MH_EnableHook(DrawAsVXL) -> {} ({})",
                     MH_StatusToString(st), static_cast<int>(st));
        return false;
    }

    LUA_LOG_INFO("[BarrelPitch] DrawAsVXL hook installed @ 0x{:X} (path B probe)",
                 kDrawAsVXLAddr);
    return true;
}

void ClearAll() {
    g_overrides.clear();
    g_autoUnits.clear();
    g_lastAutoPitch.clear();
    g_autoAll = false;
    g_bountyMarks.clear(); // marks never leak across a session

    // Restore every persistent type-field write so the type list never leaks
    // a test FireAngle across a session.
    for (const auto& save : g_persistentSaves) {
        WriteFireAngleSafe(save.pType, save.originalFireAngle);
    }
    g_persistentSaves.clear();
}

void SetAuto(unsigned int unitId, bool enabled) {
    if (enabled) {
        g_autoUnits.insert(unitId);
    } else {
        g_autoUnits.erase(unitId);
    }
}

void SetAutoAll(bool enabled) {
    if (g_autoAll == enabled)
        return; // idempotent: Lua may re-assert the mode every heartbeat
    g_autoAll = enabled;
    LUA_LOG_INFO("[BarrelPitch] AUTO-ALL {} - every voxel-turret unit pitches by live target distance",
                 enabled ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// Bounty marks (Gate 2A): UniqueID-keyed overlay registry
// ---------------------------------------------------------------------------

void MarkBounty(unsigned int unitId, unsigned int color, unsigned int durationFrames) {
    if (unitId == 0)
        return;    BountyMark m;
    m.color = color;
    m.untilFrame = (durationFrames == 0)
        ? 0xFFFFFFFFu
        : CurrentFrameSafe() + durationFrames;
    g_bountyMarks[unitId] = m;

    // Opportunistic purge (rare call, tiny map): drop marks whose unit already
    // left UnitClass::Array, so a kill-then-remark cycle never accumulates.
    for (auto it = g_bountyMarks.begin(); it != g_bountyMarks.end();) {
        if (it->first == unitId || BountyIdPresentSafe(it->first)) { ++it; continue; }
        it = g_bountyMarks.erase(it);
    }
    LUA_LOG_INFO("[Bounty] mark id={} color={:#x} dur={} reg={}",
                 unitId, color, durationFrames, g_bountyMarks.size());
}

void ClearBountyMark(unsigned int unitId) {
    g_bountyMarks.erase(unitId);
    LUA_LOG_INFO("[Bounty] clear id={} reg={}", unitId, g_bountyMarks.size());
}

void ClearBountyMarks() {
    const size_t n = g_bountyMarks.size();
    g_bountyMarks.clear();
    if (n > 0)
        LUA_LOG_INFO("[Bounty] clear-all n={}", n);
}

void SetBountyDrawMode(int mode) {
    if (mode < 0 || mode > 3)
        return;
    g_bountyDrawMode = mode;
    LUA_LOG_INFO("[Bounty] draw-mode={} (0=off,1=rect,2=text,3=full)", mode);
}

// ---------------------------------------------------------------------------
// Persistent type-field write (static FireAngle test)
// ---------------------------------------------------------------------------

// (State lives in g_persistentSaves, declared above ClearAll: one entry per
// touched type with the FireAngle captured before the first test write.)

void SetPersistent(unsigned int unitId, double degrees) {
    // Resolve the unit to its TechnoTypeClass once, on the Lua/game thread,
    // using the same SEH-guarded array idiom as the M16 HVA scan
    // (bindings_techno.cpp).
    TechnoTypeClass* pType = nullptr;
    __try {
        for (int i = 0; i < UnitClass::Array.Count; ++i) {
            UnitClass* pObj = UnitClass::Array.GetItem(i);
            if (pObj && pObj->UniqueID == unitId) {
                if (pObj->WhatAmI() == AbstractType::Unit)
                    pType = pObj->Type;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LUA_LOG_WARN("[BarrelPitch] PERSISTENT: SEH resolving unit {}", unitId);
        return;
    }
    if (!pType) {
        LUA_LOG_WARN("[BarrelPitch] PERSISTENT: unit {} not found (arm it first)",
                     unitId);
        return;
    }

    // Only one save entry per type: a repeated Set must NOT snapshot the
    // already-patched value as the "original".
    bool tracked = false;
    for (const auto& save : g_persistentSaves) {
        if (save.pType == pType) {
            tracked = true;
            break;
        }
    }

    int original = 0;
    if (!ReadFireAngleSafe(pType, &original))
        return;

    const int fireAngle = std::min(64, std::max(-64,
        static_cast<int>(std::lround(degrees * kDegToFireAngle))));

    if (!WriteFireAngleSafe(pType, fireAngle))
        return;

    if (!tracked)
        g_persistentSaves.push_back({ pType, original });

    LUA_LOG_INFO("[BarrelPitch] PERSISTENT FireAngle write: unit {} -> {} (was {}; {})",
                 unitId, fireAngle, original,
                 tracked ? "original kept from first write"
                         : "original saved");
}

void ClearPersistent(unsigned int unitId) {
    (void)unitId; // restore is global: every touched type
    const size_t n = g_persistentSaves.size();
    for (const auto& save : g_persistentSaves) {
        WriteFireAngleSafe(save.pType, save.originalFireAngle);
    }
    g_persistentSaves.clear();
    LUA_LOG_INFO("[BarrelPitch] PERSISTENT FireAngle restored on {} type(s)", n);
}

// --- Lua bindings ----------------------------------------------------------

// Engine.SetBarrelPitchOverride(unitId, pitchDegrees) -> bool
// pitchDegrees: 0 horizontal, 90 straight up, negative allowed (downward).
int BP_Set(lua_State* L) {
    const lua_Integer unitId = luaL_checkinteger(L, 1);
    const lua_Number degrees = luaL_checknumber(L, 2);

    if (unitId <= 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Reject non-finite values early; clamp the rest to the meaningful range.
    if (degrees != degrees || degrees > 1e9 || degrees < -1e9) { // NaN check
        lua_pushboolean(L, 0);
        return 1;
    }

    float deg = static_cast<float>(degrees);
    if (deg > 90.0f) deg = 90.0f;
    if (deg < -90.0f) deg = -90.0f;

    g_overrides[static_cast<unsigned int>(unitId)] = deg;
    lua_pushboolean(L, 1);
    return 1;
}

// Engine.GetBarrelPitchOverride(unitId) -> degrees | nil
int BP_Get(lua_State* L) {
    const lua_Integer unitId = luaL_checkinteger(L, 1);

    auto it = g_overrides.find(static_cast<unsigned int>(unitId));
    if (it == g_overrides.end()) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, static_cast<lua_Number>(it->second));
    return 1;
}

// Engine.SetBarrelPitchAuto(unitId, enabled) -> bool
// AUTO pitch: computed natively per draw call from the live target distance.
int BP_SetAuto(lua_State* L) {
    const lua_Integer unitId = luaL_checkinteger(L, 1);
    const bool enabled = lua_toboolean(L, 2) != 0;

    if (unitId <= 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (enabled) {
        g_autoUnits.insert(static_cast<unsigned int>(unitId));
    } else {
        g_autoUnits.erase(static_cast<unsigned int>(unitId));
    }

    lua_pushboolean(L, 1);
    return 1;
}

// Engine.SetBarrelPitchAutoAll(enabled) -> bool
// Global AUTO: every voxel-turret unit pitches by its live target distance,
// no per-unit registration ("no hotkeys, the tank AI decides itself").
int BP_SetAutoAll(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1) != 0;
    SetAutoAll(enabled);
    lua_pushboolean(L, 1);
    return 1;
}

// Engine.GetBarrelPitchAutoCount() -> int
// Number of units that drew with an AUTO-computed pitch on the latest frames
// (global or per-unit AUTO with a live target). Diagnostic for the Lua mod.
int BP_GetAutoCount(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(g_lastAutoPitch.size()));
    return 1;
}

// Engine.GetBarrelPitchAuto(unitId) -> enabled, degrees|nil | nil
int BP_GetAuto(lua_State* L) {
    const lua_Integer unitId = luaL_checkinteger(L, 1);

    if (g_autoUnits.find(static_cast<unsigned int>(unitId)) == g_autoUnits.end()) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushboolean(L, 1);

    // Report the live AUTO-computed angle when present (target in range),
    // otherwise the stored manual override, otherwise nil.
    auto autoIt = g_lastAutoPitch.find(static_cast<unsigned int>(unitId));
    if (autoIt != g_lastAutoPitch.end()) {
        lua_pushnumber(L, static_cast<lua_Number>(autoIt->second));
        return 2;
    }

    auto it = g_overrides.find(static_cast<unsigned int>(unitId));
    if (it != g_overrides.end()) {
        lua_pushnumber(L, static_cast<lua_Number>(it->second));
    } else {
        lua_pushnil(L);
    }
    return 2;
}

// Engine.SetPersistentBarrelPitch(unitId, degrees) -> bool
// Static-test path: writes the type field once and leaves it there (all
// units of that type), value NOT re-applied per draw. Restored by
// ClearPersistentBarrelPitch / ClearAllBarrelPitchOverrides.
int BP_SetPersistent(lua_State* L) {
    const lua_Integer unitId = luaL_checkinteger(L, 1);
    const lua_Number degrees = luaL_checknumber(L, 2);

    if (unitId <= 0 || degrees != degrees || degrees > 1e9 || degrees < -1e9) {
        lua_pushboolean(L, 0);
        return 1;
    }

    SetPersistent(static_cast<unsigned int>(unitId), static_cast<double>(degrees));

    lua_pushboolean(L, 1);
    return 1;
}

// Engine.ClearPersistentBarrelPitch() -> bool (true if anything restored)
int BP_ClearPersistent(lua_State* L) {
    const bool had = !g_persistentSaves.empty();
    ClearPersistent(0);
    lua_pushboolean(L, had);
    return 1;
}

// Engine.ClearBarrelPitchOverride(unitId) -> nil (clears manual AND auto)
int BP_Clear(lua_State* L) {
    const lua_Integer unitId = luaL_checkinteger(L, 1);
    g_overrides.erase(static_cast<unsigned int>(unitId));
    g_autoUnits.erase(static_cast<unsigned int>(unitId));
    return 0;
}

// Engine.ClearAllBarrelPitchOverrides() -> nil (clears manual AND auto)
int BP_ClearAll(lua_State* L) {
    const size_t n = g_overrides.size() + g_autoUnits.size();
    g_overrides.clear();
    g_autoUnits.clear();
    if (n > 0) {
        LUA_LOG_INFO("[BarrelPitch] cleared {} overrides", n);
    }
    return 0;
}

// Engine.ClearBountyMarks() -> nil (global bounty overlay reset)
int BP_ClearBountyMarks(lua_State* L) {
    (void)L;
    ClearBountyMarks();
    return 0;
}

// Engine.SetBountyDrawMode(mode) -> bool (crash-isolation A-E switch)
int BP_SetBountyDrawMode(lua_State* L) {
    const lua_Integer mode = luaL_checkinteger(L, 1);
    if (mode < 0 || mode > 3) {
        lua_pushboolean(L, 0);
        return 1;
    }
    SetBountyDrawMode(static_cast<int>(mode));
    lua_pushboolean(L, 1);
    return 1;
}

void RegisterBindings(lua_State* L) {
    lua_getglobal(L, "Engine");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }

    lua_pushcfunction(L, BP_Set);
    lua_setfield(L, -2, "SetBarrelPitchOverride");

    lua_pushcfunction(L, BP_Get);
    lua_setfield(L, -2, "GetBarrelPitchOverride");

    lua_pushcfunction(L, BP_SetAuto);
    lua_setfield(L, -2, "SetBarrelPitchAuto");

    lua_pushcfunction(L, BP_SetAutoAll);
    lua_setfield(L, -2, "SetBarrelPitchAutoAll");

    lua_pushcfunction(L, BP_GetAutoCount);
    lua_setfield(L, -2, "GetBarrelPitchAutoCount");

    lua_pushcfunction(L, BP_GetAuto);
    lua_setfield(L, -2, "GetBarrelPitchAuto");

    lua_pushcfunction(L, BP_Clear);
    lua_setfield(L, -2, "ClearBarrelPitchOverride");

    lua_pushcfunction(L, BP_ClearAll);
    lua_setfield(L, -2, "ClearAllBarrelPitchOverrides");

    lua_pushcfunction(L, BP_SetPersistent);
    lua_setfield(L, -2, "SetPersistentBarrelPitch");

    lua_pushcfunction(L, BP_ClearPersistent);
    lua_setfield(L, -2, "ClearPersistentBarrelPitch");

    lua_pushcfunction(L, BP_ClearBountyMarks);
    lua_setfield(L, -2, "ClearBountyMarks");

    lua_pushcfunction(L, BP_SetBountyDrawMode);
    lua_setfield(L, -2, "SetBountyDrawMode");

    lua_setglobal(L, "Engine");
}

} // namespace BarrelPitch
} // namespace LuaAPI
