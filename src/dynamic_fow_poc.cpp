#include "dynamic_fow_poc.h"

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

#include <YRPP.h>

#include <MinHook.h>

#include <LuaAPI/logger.hpp>

#include <cstdlib>
#include <vector>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// TEMPORARY DIAGNOSTIC PROOF OF CONCEPT.
//
// Two detours, no engine writes:
//   0x4801F0 DrawFog  -- captures ECX (CellClass*) into thread-local storage and
//                        forwards to the vanilla body untouched.
//   0x69E7E0 resolver-- rewrites ONLY the stack argument "frame" for the PoC
//                        cell class, then calls the vanilla resolver.
//
// Nothing here writes CellClass+0x120 / +0x121, MapClass, or any shroud grid.
// Nothing here calls GetOcclusion, Reshroud or GrowShroud.

namespace LuaAPI {
namespace DynamicFowPoc {

namespace {

// ---------------------------------------------------------------------------
// Addresses (validated instruction starts, gamemd.exe 1.001)
// ---------------------------------------------------------------------------

// CellClass::DrawFog. __thiscall, this=ECX, ret 8 (two stack args, both
// unrelated to the cell identity). Proven frame computation lives inside this
// function, which is exactly why it is NOT the place the frame is changed.
constexpr uintptr_t kDrawFogAddr = 0x4801F0;

// Shape-frame resolver reached from the fog blitters. __thiscall:
//   ECX      = SHP shape object
//   [esp+4]  = arg0, out-rect pointer (the return value)
//   [esp+8]  = arg1, the frame index
//   ret 8
// Confirmed by the call site in the fog blitter:
//   0x47F041  push eax          ; arg1 = frame
//   0x47F042  push ecx          ; arg0 = &rect
//   0x47F043  mov ecx,[ebp-4]   ; this = shape object
//   0x47F046  call 0x69E7E0
constexpr uintptr_t kFrameResolverAddr = 0x69E7E0;

// Pixel-source resolver reached from the same blitters. ABI verified against
// the disassembly, not assumed:
//   0x69E740  sub esp,0x6C
//   0x69E743  push esi
//   0x69E744  mov esi,ecx            ; this = shape
//   0x69E7A7  pop esi                ; ESP back to entry-108
//   0x69E7AE  mov ecx,[esp+0x70]     ; = entry+4 = the frame
//   0x69E7B9  lea ecx,[eax+ecx*8+8]  ; the SAME record 0x69E7E0 uses
//   0x69E7C1  mov ecx,[ecx+0x14]     ; record+0x14 = pixel-data pointer
//   0x69E7C8  add eax,ecx            ; returns it
//   0x69E7CD  ret 4
// So: __thiscall, ECX = shape, one stack arg (frame), ret 4. Failure paths
// fall to 0x69E7D0 "xor eax,eax" and return null.
// Only 13 callers (vs 54 for 0x69E7E0), so the TLS gate matters just as much.
constexpr uintptr_t kPixelSourceAddr = 0x69E740;

// CellClass::MapCoords. First data member of CellClass; the RE shows DrawFog
// building &this->+0x24 and handing it to GetOcclusion as the coordinate
// struct. CellStruct is Vector2D<short> (GeneralStructures.h:12), so this is
// two 16-bit signed values.
constexpr uintptr_t kCellMapCoordsOffset = 0x24;

// ---------------------------------------------------------------------------
// PoC parameters
// ---------------------------------------------------------------------------

// Visual-regression guard. The override frame is NOT chosen here on purpose:
// SHP frame semantics are still unproven (frame 0 was shown to produce a
// rhomboid patch, not a clear cell, and the blitter draws from EITHER
// FOG.SHP or SHROUD.SHP depending on a global bit, so one index means different
// things on the two sheets). A negative value means "inert: never override".
constexpr int kOverrideFrameInert = -1;
int g_overrideFrame = kOverrideFrameInert;

// Diagnostic run control, read from the environment at install time so no
// rebuild is needed:
//   LUAAPI_DYNAMIC_FOW_POC=1   -> enable mutation (default is DISABLED, so the
//                                  shipped DLL is a clean passthrough baseline)
//   LUAAPI_DYNAMIC_FOW_FRAME=N -> which frame index to substitute; unset means
//                                  "mutation enabled but inert"
// The two are independent: POC alone still overrides nothing, which is the
// baseline that Test A/B need.
bool g_pocEnabled = false;

// Camera-test mode, set from LUAAPI_DYNAMIC_FOW_CAMERA_TEST at install time.
// When set, the Lua mod asks for the current viewport centre cell instead of
// using the fixed diagnostic coordinate.
bool g_cameraTest = false;
bool g_passthroughMode = false;   // diagnostic: never substitute, only observe

// The diagnostic override is limited to ONE cell, not a stride pattern. A stride
// pattern produced hundreds of overrides at once, which is exactly the kind of
// accumulation that makes it impossible to attribute a visual artifact to a
// single substitution.
constexpr int kTargetCellX = 64;
constexpr int kTargetCellY = 64;

// Deterministic, map-independent diagnostic cell class. Low density so the
// substitution is provably selective rather than "everything changed".


// Log budget: a few verbatim events, then a periodic summary. Never per frame.
// The rect and pixel channels get INDEPENDENT budgets so one channel can never
// starve the other's logging, and neither budget gates any counter or any
// override.
constexpr int kMaxVerboseEvents = 8;
constexpr int kMaxRectOverrideEvents = 8;
constexpr int kMaxPixelOverrideEvents = 8;

// How many pixel-override lines to emit for the single latched PoC cell, so
// repeated draws of the SAME cell can be compared. Diagnostics only.
constexpr int kPocCellLogEvents = 4;

constexpr DWORD kSummaryPeriodMs = 1000;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// The cell currently being fogged. 0x69E7E0 has many callers outside the fog
// path, so a non-null value here is the ONLY thing that authorises touching the
// frame argument. Set by the DrawFog detour, restored on the way out, so a
// nested DrawFog cannot confuse the outer one.
thread_local CellClass* tls_activeCell = nullptr;

using DrawFogFn = void(__fastcall*)(CellClass*, void*, void*, DWORD);
DrawFogFn g_originalDrawFog = nullptr;

using ResolveFrameFn = void*(__fastcall*)(void*, void*, void*, int);
ResolveFrameFn g_originalResolveFrame = nullptr;

using ResolvePixelFn = void*(__fastcall*)(void*, void*, int);
ResolvePixelFn g_originalResolvePixel = nullptr;

bool g_hooksInstalled = false;


// Live counters. All touched on the game thread only.
struct PocCounters {
    unsigned long long drawFogCalls = 0;
    unsigned long long drawFogWithCell = 0;
    unsigned long long resolverCallsWithTls = 0;
    unsigned long long resolverCallsWithoutTls = 0;
    unsigned long long overridesApplied = 0;
    unsigned long long overridesSkippedSameFrame = 0;
    // Pixel-source channel (0x69E740), counted separately from the rect
    // channel (0x69E7E0) so the two can be correlated per cell.
    unsigned long long pixelResolverCallsWithTls = 0;
    unsigned long long pixelResolverCallsWithoutTls = 0;
    unsigned long long pixelOverridesApplied = 0;
    unsigned long long pixelOverridesSkippedSameFrame = 0;
    unsigned long long pixelPtrDiffered = 0;
    unsigned long long rectPtrDiffered = 0;
    // Deliberately named "lastLogged...": these describe the last event that was
    // actually written to the log, NOT the most recent override. With finite
    // log budgets the two are different things and conflating them is what made
    // the previous summary misleading.
    int lastLoggedRectX = -1;
    int lastLoggedRectY = -1;
    int lastLoggedRectOrigFrame = -1;
    int lastLoggedRectOvrFrame = -1;
    int lastLoggedRectOrig[4] = { 0, 0, 0, 0 };
    int lastLoggedRectOvr[4] = { 0, 0, 0, 0 };
    int lastLoggedPixelX = -1;
    int lastLoggedPixelY = -1;
    int lastLoggedPixelOrigFrame = -1;
    int lastLoggedPixelOvrFrame = -1;
    uintptr_t lastLoggedPixOrig = 0;
    uintptr_t lastLoggedPixOvr = 0;
    // Original-frame histogram over the full documented range, so the runtime
    // log can be used to pick a real criterion instead of guessing one.
    unsigned long long frameHistogram[64] = {};
    // Bridge counters: Lua mark -> renderer gate -> applied substitution.
    unsigned long long marksTotal = 0;   // MarkCell accepted (added)
    unsigned long long marksRemoved = 0; // unmarked or cleared
    unsigned long long rendererHit = 0;  // current cell WAS marked
    unsigned long long rendererMiss = 0; // current cell was not marked
    bool markCapWarned = false;
    int firstHitX = -1;
    int firstHitY = -1;

    // -----------------------------------------------------------------------
    // Intersection diagnostics. Strictly read-only: nothing here changes a
    // frame, a pointer or a return value, and no hook was added to produce it.
    //
    // The open question these exist to settle is NOT "does the override work"
    // but "is the plumbing even connected": does the CellClass* that DrawFog
    // publishes in TLS actually arrive at 0x69E740, and do the cells Lua marks
    // ever appear among the cells DrawFog fogs. Until both are measured, a
    // pixelHits==0 result cannot be attributed to the renderer.
    // -----------------------------------------------------------------------
    unsigned long long drawMarkedHits = 0;    // DrawFog cell IS marked
    unsigned long long drawMarkedMisses = 0;  // DrawFog cell is NOT marked
    unsigned long long resolverMarkedTls = 0;   // resolver TLS cell IS marked
    unsigned long long resolverUnmarkedTls = 0; // resolver TLS cell NOT marked
    // Pointer identity. TLS carries the pointer, so a match is expected by
    // construction; counting it makes that expectation a measurement.
    unsigned long long resolverPtrMatch = 0;    // resolver cell == last DrawFog cell
    unsigned long long resolverPtrMismatch = 0;
    // Was the resolver reached from inside a DrawFog frame at all? A non-zero
    // tls_depth at resolver time is the positive proof of nesting; without it a
    // TLS hit could still be a stale or foreign pointer.
    unsigned long long resolverInsideDrawFog = 0;
    unsigned long long resolverOutsideDrawFog = 0;
    int tlsDepth = 0;
    uintptr_t lastDrawCellPtr = 0;
    int lastDrawCellX = -1;
    int lastDrawCellY = -1;
    unsigned long long drawTracePrinted = 0;    // DrawFog lines already emitted
    unsigned long long resolverTracePrinted = 0; // resolver lines already emitted

    // -----------------------------------------------------------------------
    // Post-Draw overwrite probe.
    //
    // Static analysis showed two of the passes that run after DrawFog
    // (0x6D2DE0, 0x6D3AC0) can reach 0x4114B0, the only gate that hands out the
    // render-target pointer, so "no later pass can overwrite the fog" is NOT
    // provable statically. This probe settles it at runtime without a new hook
    // and without touching engine memory.
    //
    // Method, and why it is self-validating: the destination window is derived
    // from the rect DrawFog is given, which is a guess about geometry. So the
    // window is checksummed on ENTRY and again on EXIT of the same DrawFog body.
    // Our override writes into that body, and pixelPtrDiffered already proves a
    // different SHAPE frame was blitted, so a changed checksum proves the window
    // really overlaps the write. Windows that never validate are reported as
    // such instead of being counted as evidence.
    // -----------------------------------------------------------------------
    unsigned long long regionProbeArmed = 0;    // marked cells entered the probe
    unsigned long long regionValidated = 0;     // window provably covers the write
    unsigned long long regionChanged = 0;       // changed while NO DrawFog for it
    unsigned long long regionChangedRedraw = 0; // changed, but its own DrawFog ran
    unsigned long long regionVanished = 0;      // surface gone / unreadable
    unsigned long long regionChecks = 0;        // re-checksum passes performed
    // Why arming failed. Without these the probe is silent when the geometry
    // is wrong, which is exactly what happened on the first run.
    unsigned long long probeFailNoSurface = 0;    // [0x87E8A4] unusable
    unsigned long long probeFailInvalidStruct = 0;// arg pointers not readable
    unsigned long long probeFailInvalidGeom = 0;  // tile diff gave no box
    unsigned long long probeFailEmptyRegion = 0;  // box clamped to nothing
    unsigned long long probeFailHashRead = 0;      // read faulted inside the box
    unsigned long long probeFailHashBasis = 0;     // box hashed to the FNV seed
    unsigned long long probeSurfaceLogged = 0;     // one-shot surface dump emitted
    unsigned long long probeArgLogged = 0;         // raw DrawFog args dumped
    unsigned long long probeBoxesLearned = 0;      // cells whose box was measured
    // Expected-vs-actual. Separate from the temporal diff above on purpose:
    // a post-draw pass that rewrites the same bytes every frame is invisible to
    // snapshot comparison, but is caught here, because the box is compared
    // against what the override frame SHOULD have produced.
    unsigned long long expectedChecks = 0;
    unsigned long long expectedMatch = 0;
    unsigned long long expectedMismatch = 0;
    unsigned long long expectedReadFail = 0;
    unsigned long long expectedNoWrite = 0;     // wildcard pixels in the template
    unsigned long long expectedNoTemplate = 0; // no fog shape or resolvers available
    unsigned long long expectedMismatchLogged = 0;
    // Small histogram of which source byte values failed to match, so a
    // mismatch confined to a few SHP values is visible rather than guessed.
    unsigned int expectedMissByValue[8] = {};

    // -----------------------------------------------------------------------
    // Engine-selected frame, measured WITHOUT our substitution.
    //
    // The frame is taken straight from the argument the engine passed to the
    // pixel resolver - it is NOT reconstructed from the returned pixel pointer.
    // That is the whole point: it answers "what frame would the engine have
    // asked for on this cell if we stayed out of the way".
    //
    // Separate histograms for marked and unmarked TLS cells, because the engine
    // frame distribution is expected to differ between them and only the marked
    // one drives the override decision.
    // -----------------------------------------------------------------------
    unsigned long long markedPixelCalls = 0;
    unsigned long long unmarkedPixelCalls = 0;
    unsigned long long markedFrame0 = 0;
    unsigned long long markedFrame15 = 0;
    unsigned long long markedFrameOther = 0;
    int markedFrameMin = -1;
    int markedFrameMax = -1;
    unsigned long long markedFrameHistogram[64] = {};
    unsigned long long unmarkedFrameHistogram[64] = {};
    unsigned long long markedUniqueCells = 0;
    unsigned long long markedCellTracePrinted = 0;
    uintptr_t markedCellSeen[16] = {};
    unsigned long long regionBoundaryChecks = 0;  // frame transitions observed
    unsigned long long regionSkipNoDraw = 0;      // cell had no DrawFog that frame
    unsigned long long regionSkipNotDrawnYet = 0;// baseline not captured yet
    int lastBoundaryFrame = -1;                   // last CurrentFrame verified
    int currentFrameSeen = -1;                    // CurrentFrame at last DrawFog
    int lastProbeCellX = -1;
    int lastProbeCellY = -1;
};

constexpr int kRegionTrackCap = 16;

struct RegionTrack {
    int x = -1;
    int y = -1;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    uint32_t baseline = 0;
    unsigned long long armedFrame = 0;
    unsigned long long ownDrawFogSinceArm = 0;
    int lastDrawnFrame = -1;   // CurrentFrame in which this cell was last fogged
    bool armed = false;
    bool hasBox = false;   // box measured empirically, not guessed
};
RegionTrack g_regionTrack[kRegionTrackCap];
size_t g_regionTrackCount = 0;

// Finds the tracked entry for a cell, creating it on first sight. Bounded by
// kRegionTrackCap; returns null once full so the probe never grows unbounded.
RegionTrack* FindOrCreateTrack(int x, int y) {
    for (size_t i = 0; i < g_regionTrackCount; ++i) {
        if (g_regionTrack[i].x == x && g_regionTrack[i].y == y)
            return &g_regionTrack[i];
    }
    if (g_regionTrackCount >= kRegionTrackCap) return nullptr;
    RegionTrack& t = g_regionTrack[g_regionTrackCount++];
    t = RegionTrack();
    t.x = x;
    t.y = y;
    return &t;
}

// Scratch rect, filled by the probe helpers. Plain POD so the SEH helpers below
// can stay destructor-free (C2712: a function containing __try must not own
// objects with destructors).
int g_probeRect[4] = {0, 0, 0, 0};
PocCounters g_counters;

// ---------------------------------------------------------------------------
// Marked-cell store: coordinates marked from Lua by the mod's existing sweep.
//
// Two structures, deliberately:
//   * a fixed 512x512 bit matrix - O(1) membership with a hard 32 KiB bound.
//     The membership test runs on every 0x69E740 call that carries a cell
//     (thousands per second), so a linear scan over a set that the sweep grows
//     to thousands of entries would be a real performance bug, not a nitpick.
//   * a vector of packed (y<<16)|x keys - the concrete cell list the brief asks
//     for, and what the counters and diagnostics report. Dedup comes free from
//     the bit matrix.
//
// The sweep only ever ADDS cells, so the vector grows for the whole match; it is
// capped and reports when the cap is hit rather than growing without limit.
// ---------------------------------------------------------------------------
constexpr int kFogMapSideLimit = 512;  // matches kFogMapSide in bindings_techno.cpp
constexpr size_t kMarkVectorCap = 16384;

std::vector<uint32_t> g_markedCells;
uint64_t g_markBits[(kFogMapSideLimit * kFogMapSideLimit) / 64] = {};
size_t g_markCount = 0;

inline uint32_t PackCell(int x, int y) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16) |
           static_cast<uint32_t>(static_cast<uint16_t>(x));
}

inline size_t BitIndex(int x, int y) {
    return static_cast<size_t>(y) * kFogMapSideLimit + static_cast<size_t>(x);
}

// ---------------------------------------------------------------------------
// First-N mark trace. Keeps the opening coordinates so the DrawFog/resolver
// intersection logs can be compared against a concrete list of what Lua asked
// for, instead of against an anonymous mark count. Fixed size, never grows.
// ---------------------------------------------------------------------------
constexpr size_t kMarkTraceCap = 24;
struct MarkTraceEntry {
    int x = 0;
    int y = 0;
    unsigned long long seq = 0;  // 1-based order of acceptance
};
MarkTraceEntry g_markTrace[kMarkTraceCap];
size_t g_markTraceCount = 0;

void MarkCell(int x, int y) {
    if (x < 0 || y < 0 || x >= kFogMapSideLimit || y >= kFogMapSideLimit) return;
    const size_t bi = BitIndex(x, y);
    if (g_markBits[bi >> 6] & (1ull << (bi & 63))) return;  // already marked
    g_markBits[bi >> 6] |= (1ull << (bi & 63));
    ++g_counters.marksTotal;
    if (g_markTraceCount < kMarkTraceCap) {
        MarkTraceEntry& e = g_markTrace[g_markTraceCount];
        e.x = x;
        e.y = y;
        e.seq = g_counters.marksTotal;
        ++g_markTraceCount;
    }
    if (g_markedCells.size() >= kMarkVectorCap) {
        if (!g_counters.markCapWarned) {
            g_counters.markCapWarned = true;
            LUA_LOG_WARN("[DYNAMIC_FOW][BRIDGE] mark vector cap {} reached; "
                         "membership continues via the bit matrix", kMarkVectorCap);
        }
    } else {
        g_markedCells.push_back(PackCell(x, y));
    }
    g_markCount = g_markCount + 1;
    LUA_LOG_INFO("[DYNAMIC_FOW][BRIDGE] MarkCell cell=({},{}) markCount={}",
                 x, y, g_markCount);
}

int ClearMarkedCellsInternal() {
    const int removed = static_cast<int>(g_markCount);
    g_markedCells.clear();
    for (size_t i = 0; i < (sizeof(g_markBits) / sizeof(g_markBits[0])); ++i)
        g_markBits[i] = 0;
    g_markCount = 0;
    g_markTraceCount = 0;
    g_counters.marksRemoved += static_cast<unsigned long long>(removed);
    LUA_LOG_INFO("[DYNAMIC_FOW][BRIDGE] ClearMarks removed={} markCount=0", removed);
    return removed;
}

// O(1). Never touches the vector, so it is safe no matter how large the set is.
bool IsCellMarked(int x, int y) {
    if (x < 0 || y < 0 || x >= kFogMapSideLimit || y >= kFogMapSideLimit) return false;
    if (g_markCount == 0) return false;
    const size_t bi = BitIndex(x, y);
    return (g_markBits[bi >> 6] & (1ull << (bi & 63))) != 0;
}

// Latches the FIRST cell that satisfies ShouldOverrideCell, so a handful of
// repeated draws of that exact same cell can be logged and compared. Purely
// diagnostic: no branch here is ever allowed to gate the override itself.
struct PocCellTracker {
    bool latched = false;
    int x = -1;
    int y = -1;
    int pixelLogs = 0;
    int rectLogs = 0;
};
PocCellTracker g_pocCell;

bool ShouldLogPocCell(int x, int y, int channel) {
    if (!g_pocCell.latched) {
        g_pocCell.latched = true;
        g_pocCell.x = x;
        g_pocCell.y = y;
    }
    if (x != g_pocCell.x || y != g_pocCell.y) return false;
    int& budget = (channel == 0) ? g_pocCell.rectLogs : g_pocCell.pixelLogs;
    if (budget >= kPocCellLogEvents) return false;
    ++budget;
    return true;
}

// Independent per-channel budgets. A shared budget previously let the rect
// channel consume all 8 slots and starve the pixel channel down to 4 lines,
// which made pixelPtrDiffers uninterpretable.
int VerboseBudget() {
    static int s_budget = kMaxVerboseEvents;
    return s_budget > 0 ? s_budget-- : 0;
}

int RectLogBudget() {
    static int s_budget = kMaxRectOverrideEvents;
    return s_budget > 0 ? s_budget-- : 0;
}

int PixelLogBudget() {
    static int s_budget = kMaxPixelOverrideEvents;
    return s_budget > 0 ? s_budget-- : 0;
}

// Reads the two 16-bit map coordinates out of the cell. Deliberately raw at
// +0x24 (the RE-proven layout) rather than through the YRpp member, so the log
// shows what the engine actually has. The YRpp member is logged alongside for
// the first events so any layout disagreement is visible in the evidence.
struct CellCoords {
    short x = 0;
    short y = 0;
    short yrppX = 0;
    short yrppY = 0;
    bool ok = false;
};

// __try lives here, in a function whose only local is a POD aggregate, so the
// C2712 rule (no C++ destructors alongside __try) is respected.
CellCoords ReadCellCoordsSafe(CellClass* pCell) {
    CellCoords c;
    if (!pCell) return c;
    __try {
        const auto* raw = reinterpret_cast<const short*>(reinterpret_cast<const char*>(pCell) +
                                                          kCellMapCoordsOffset);
        c.x = raw[0];
        c.y = raw[1];
        c.yrppX = static_cast<short>(pCell->MapCoords.X);
        c.yrppY = static_cast<short>(pCell->MapCoords.Y);
        c.ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        c.ok = false;
    }
    return c;
}

// Read-only peek at the two documented CellClass fog fields. Never written.
struct CellFogPeek {
    char visibility = 0;
    char foggedness = 0;
    bool ok = false;
};

CellFogPeek PeekCellFogSafe(CellClass* pCell) {
    CellFogPeek p;
    if (!pCell) return p;
    __try {
        const auto* raw = reinterpret_cast<const char*>(pCell);
        p.visibility = raw[0x120];
        p.foggedness = raw[0x121];
        p.ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        p.ok = false;
    }
    return p;
}

void LogSummary();
void PumpSummary();
// Defined below the DrawFog hook that calls it; declared here because the
// frame-boundary latch is invoked from that hook.
void PumpFrameBoundary();

// ---------------------------------------------------------------------------
// Post-Draw overwrite probe: surface view + region checksum.
//
// Both helpers are tiny, take raw scalars, and own no object with a destructor,
// so they can legally contain __try/__except (C2712). They only READ engine
// memory; nothing here is ever written back.
// ---------------------------------------------------------------------------

// Reads the render target and exposes the pixel buffer, byte pitch and extents.
//
// Layout is NOT the wrapper's own fields. The wrapper at [0x87E8A4] holds a
// sub-object at +0x14, and it is that object which carries the geometry:
//   OBJ+0x04 width   OBJ+0x08 height   OBJ+0x10 stride   OBJ+0x14 pixel buffer
// Proven from the constructor 0x410CE0 (`mov [edi+0x10],2` for the stride,
// `lea ecx,[edi+0x14]; call 0x43AD00` for the buffer, which does operator new
// and stores the pointer at +0x00 of the sub-object) and from 0x4115F0, which
// returns `buffer + stride*x + width*stride*y`.
//
// The wrapper's own +0x18/+0x20/+0x28/+0x2C were the previous source and are
// wrong: +0x18 is a stale Lock result kept only from construction time, +0x20
// is 2*width*height (total buffer size, not a pitch).
bool ReadSurfaceView(unsigned char** base, int* pitch, int* width, int* height,
                     int* outFail);

// Failure reasons for every probe step. Nothing in the probe returns a bare 0:
// a region that could not be built must say why, otherwise a wrong geometry
// reads as "no overwrite" instead of as "no evidence".
enum ProbeFail {
    kProbeOk = 0,
    kProbeNoSurface = 1,
    kProbeInvalidStruct = 2,
    kProbeInvalidGeom = 3,
    kProbeEmptyRegion = 4,
    kProbeHashRead = 5,
    kProbeHashBasis = 6
};

inline void CountProbeFail(int reason) {
    switch (reason) {
        case kProbeNoSurface:     ++g_counters.probeFailNoSurface; break;
        case kProbeInvalidStruct: ++g_counters.probeFailInvalidStruct; break;
        case kProbeInvalidGeom:   ++g_counters.probeFailInvalidGeom; break;
        case kProbeEmptyRegion:   ++g_counters.probeFailEmptyRegion; break;
        case kProbeHashRead:      ++g_counters.probeFailHashRead; break;
        case kProbeHashBasis:     ++g_counters.probeFailHashBasis; break;
        default: break;
    }
}

// Implementation of the reader declared above. outFail separates the three
// ways it can legitimately fail, so an unusable surface is never silently
// mistaken for an unchanged one:
//   kProbeNoSurface     - no wrapper, or no sub-object at wrapper+0x14
//   kProbeInvalidStruct - the pixel buffer pointer at OBJ+0x14 is null, or the
//                         descriptor chain faulted while being read
//   kProbeInvalidGeom   - width/height/stride/pitch not usable for indexing
bool ReadSurfaceView(unsigned char** base, int* pitch, int* width, int* height,
                     int* outFail) {
    *base = nullptr;
    *pitch = *width = *height = 0;
    if (outFail) *outFail = kProbeNoSurface;
    int rc = kProbeNoSurface;
    __try {
        unsigned char* const* slot =
            reinterpret_cast<unsigned char* const*>(0x87E8A4);
        unsigned char* wrapper = *slot;
        if (!wrapper) return false;
        unsigned char* obj = *reinterpret_cast<unsigned char**>(wrapper + 0x14);
        if (!obj) return false;

        unsigned char* px = *reinterpret_cast<unsigned char**>(obj + 0x14);
        const int wd = *reinterpret_cast<const int*>(obj + 0x04);
        const int ht = *reinterpret_cast<const int*>(obj + 0x08);
        const int stride = *reinterpret_cast<const int*>(obj + 0x10);
        if (!px) { rc = kProbeInvalidStruct; return false; }
        if (wd <= 0 || ht <= 0 || stride <= 0) { rc = kProbeInvalidGeom; return false; }
        const long long pt = (long long)wd * stride;
        if (pt <= 0 || pt > 0x7FFFFFFFLL) { rc = kProbeInvalidGeom; return false; }

        *base = px;
        *pitch = static_cast<int>(pt);
        *width = wd;
        *height = ht;
        rc = kProbeOk;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rc = kProbeInvalidStruct;
    }
    if (outFail) *outFail = rc;
    return rc == kProbeOk;
}

// Dumps the raw DrawFog arguments for the first few marked cells.
//
// Both stack arguments are POINTERS to structs, not a RECT: 0x47EFE0 does
// `mov esi,[edi+4]` and `mov ecx,[ebx+4]`. So the first six DWORDs of each are
// printed verbatim, with no interpretation, to establish the real layout.
// Every read is inside __try.
void ProbeDumpArgs(void* pArg1, void* pArg2, int cellX, int cellY) {
    if (g_counters.probeArgLogged >= 6) return;
    ++g_counters.probeArgLogged;
    int a[6] = {0, 0, 0, 0, 0, 0};
    int b[6] = {0, 0, 0, 0, 0, 0};
    int bad = 0;
    __try {
        const int* pa = static_cast<const int*>(pArg1);
        const int* pb = static_cast<const int*>(pArg2);
        for (int i = 0; i < 6; ++i) { a[i] = pa[i]; b[i] = pb[i]; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bad = 1;
    }
    if (bad) ++g_counters.probeFailInvalidStruct;
    LUA_LOG_INFO(
        "[DFOW] PROBE_ARG#{} cell=({},{}) arg1=0x{:08X} arg2=0x{:08X} "
        "arg1[0..5]={},{},{},{},{},{} arg2[0..5]={},{},{},{},{},{} readable={}",
        g_counters.probeArgLogged, cellX, cellY,
        reinterpret_cast<uintptr_t>(pArg1), reinterpret_cast<uintptr_t>(pArg2),
        a[0], a[1], a[2], a[3], a[4], a[5],
        b[0], b[1], b[2], b[3], b[4], b[5], bad ? 0 : 1);
}

// One-shot dump of what the render-target wrapper actually holds. Costs one log
// per process and pins the geometry question on its own.
void LogSurfaceOnce(unsigned char* base, int pitch, int w, int h) {
    if (g_counters.probeSurfaceLogged) return;
    g_counters.probeSurfaceLogged = 1;
    LUA_LOG_INFO(
        "[DFOW] PROBE_SURFACE base=0x{:08X} pitch={} width={} height={} end=0x{:08X}",
        reinterpret_cast<uintptr_t>(base), pitch, w, h,
        reinterpret_cast<uintptr_t>(base + (long long)h * pitch));
}


// ---------------------------------------------------------------------------
// Empirical region discovery.
//
// The previous attempt assumed DrawFog's arguments were a RECT. They are not:
// 0x47EFE0 dereferences BOTH stack arguments at +4, so they are struct
// pointers. Guessing again would just produce a second silent failure.
//
// Instead the window is MEASURED. Before a marked cell's DrawFog body runs, the
// whole render target is reduced to a grid of 16x16-pixel tile checksums. After
// the body, the grid is recomputed and the tiles that changed are exactly the
// pixels DrawFog wrote. Their bounding box IS the window, with no assumption
// about the argument layout involved.
//
// Cost: one full pass per side, but only for the first few marked cells until a
// box is learned, then never again for that cell. Read-only throughout.
// ---------------------------------------------------------------------------
constexpr int kProbeTileShift = 4;                 // 16 px per tile
constexpr int kProbeTilePx = 1 << kProbeTileShift; // 16
constexpr int kProbeMaxGridX = 64;
constexpr int kProbeMaxGridY = 48;
constexpr int kProbeBoxLearnLimit = 6;             // cells we learn a box for

uint32_t g_tileBefore[kProbeMaxGridX * kProbeMaxGridY];
uint32_t g_tileAfter[kProbeMaxGridX * kProbeMaxGridY];
int g_probeGridX = 0;
int g_probeGridY = 0;
unsigned long long g_probeBoxLearned = 0;

// FNV over one tile. The hash itself is unchanged; the only addition is that a
// read fault is now reported separately instead of collapsing into the value 0.
// That collision is what made the previous run unreadable: every tile hashed to
// 0 on both sides, so the diff was empty and it was logged as invalidGeom
// rather than as "the buffer could not be read".
// outFail: 0 = ok (a hash of 0 is still a valid value), 1 = read fault.
uint32_t HashTile(unsigned char* base, int pitch, int px, int py, int width, int height,
                  int* outFail) {
    *outFail = 0;
    int x0 = px * kProbeTilePx, y0 = py * kProbeTilePx;
    if (x0 >= width || y0 >= height) return 0;  // outside: legitimately empty
    int x1 = x0 + kProbeTilePx; if (x1 > width) x1 = width;
    int y1 = y0 + kProbeTilePx; if (y1 > height) y1 = height;

    uint32_t hv = 2166136261u;
    int bad = 0;
    __try {
        for (int y = y0; y < y1; ++y) {
            const unsigned char* row = base + (long long)y * pitch + (long long)x0 * 2;
            for (int x = x0; x < x1; ++x) {
                hv ^= static_cast<uint32_t>(row[(x - x0) * 2]);
                hv *= 16777619u;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bad = 1;
    }
    if (bad) { *outFail = 1; return 0; }
    return hv;
}

// Fills dst[] with the tile grid of the current surface content.
// Returns false if the surface is unusable OR if any single tile could not be
// read. Propagating a tile fault is what stops a failed snapshot from looking
// like an unchanged one; a partially filled grid is never compared.
bool SnapshotTiles(uint32_t* dst, int* outFail) {
    *outFail = kProbeOk;
    unsigned char* base = nullptr;
    int pitch = 0, w = 0, h = 0;
    if (!ReadSurfaceView(&base, &pitch, &w, &h, outFail)) return false;
    g_probeGridX = (w + kProbeTilePx - 1) / kProbeTilePx;
    g_probeGridY = (h + kProbeTilePx - 1) / kProbeTilePx;
    if (g_probeGridX > kProbeMaxGridX) g_probeGridX = kProbeMaxGridX;
    if (g_probeGridY > kProbeMaxGridY) g_probeGridY = kProbeMaxGridY;
    for (int ty = 0; ty < g_probeGridY; ++ty) {
        for (int tx = 0; tx < g_probeGridX; ++tx) {
            int tileFail = 0;
            const uint32_t h32 = HashTile(base, pitch, tx, ty, w, h, &tileFail);
            if (tileFail) { *outFail = kProbeHashRead; return false; }
            dst[ty * kProbeMaxGridX + tx] = h32;
        }
    }
    return true;
}

// Compares the two grids and reports the bounding box of the tiles that moved.
// Writes the box in PIXELS into outBox[4]. Returns the number of changed tiles.
int DiffTilesToBox(int* outBox) {
    int minTx = 0x7FFFFFFF, minTy = 0x7FFFFFFF, maxTx = -1, maxTy = -1, n = 0;
    for (int ty = 0; ty < g_probeGridY; ++ty) {
        for (int tx = 0; tx < g_probeGridX; ++tx) {
            const size_t i = (size_t)ty * kProbeMaxGridX + tx;
            if (g_tileBefore[i] == g_tileAfter[i]) continue;
            ++n;
            if (tx < minTx) minTx = tx;
            if (ty < minTy) minTy = ty;
            if (tx > maxTx) maxTx = tx;
            if (ty > maxTy) maxTy = ty;
        }
    }
    if (n == 0) { outBox[0] = outBox[1] = outBox[2] = outBox[3] = 0; return 0; }
    outBox[0] = minTx * kProbeTilePx;
    outBox[1] = minTy * kProbeTilePx;
    outBox[2] = (maxTx + 1) * kProbeTilePx;
    outBox[3] = (maxTy + 1) * kProbeTilePx;
    return n;
}

// Full-surface hash of an already-known box, used for the before/after and the
// later re-check. Fails (0) only when the box is empty or the read faults.
uint32_t HashBox(int left, int top, int right, int bottom, int* outFail) {
    *outFail = kProbeOk;
    unsigned char* base = nullptr;
    int pitch = 0, w = 0, h = 0;
    if (!ReadSurfaceView(&base, &pitch, &w, &h, outFail)) return 0;
    LogSurfaceOnce(base, pitch, w, h);
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > w) right = w;
    if (bottom > h) bottom = h;
    if (right <= left || bottom <= top) { *outFail = kProbeEmptyRegion; return 0; }

    uint32_t hv = 2166136261u;
    int bad = 0;
    __try {
        for (int y = top; y < bottom; ++y) {
            const unsigned char* row = base + (long long)y * pitch;
            for (int x = left; x < right; ++x) {
                hv ^= static_cast<uint32_t>(row[(long long)x * 2]);
                hv *= 16777619u;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bad = 1;
    }
    if (bad) { *outFail = kProbeHashRead; return 0; }
    if (hv == 2166136261u) { *outFail = kProbeHashBasis; return 0; }
    return hv;
}

// ---------------------------------------------------------------------------
// Expected-vs-actual template.
//
// The expected content is taken from the SAME resolvers the blitter uses, by
// calling the saved originals directly. This is the same technique the existing
// SHP dump (DumpOneFrame) already uses, so neither the 0x69E7E0 nor the 0x69E740
// hook is modified: geometry and pixel pointer both come from
// g_originalResolveFrame / g_originalResolvePixel.
//
// Blitter #1 semantics, from 0x47EFE0 (both loops):
//   source byte == 0xFE  ->  cmp dl,0xFE / je -> NO WRITE (wildcard)
//   otherwise            ->  xor cx,cx / mov cl,dl / mov word ptr [eax],cx
// i.e. the destination 16-bit slot receives the source byte zero-extended.
// 0xFE therefore becomes a wildcard, never a mismatch.
// ---------------------------------------------------------------------------

constexpr int kTemplateMaxPx = 256 * 256;  // hard cap on template pixels

// Builds the override frame's pixel template. Returns false if the shape or the
// resolvers are unavailable. *outNoWrite receives the number of wildcard pixels.
bool BuildOverrideTemplate(void* pShape, unsigned char* out, int* outW, int* outH,
                           int* outNoWrite) {
    *outW = *outH = 0;
    *outNoWrite = 0;
    if (!pShape || !g_originalResolveFrame || !g_originalResolvePixel) return false;

    // Authoritative geometry, same source as DumpOneFrame.
    int rect[4] = { 0, 0, 0, 0 };
    int bad = 0;
    __try {
        g_originalResolveFrame(pShape, nullptr, rect, g_overrideFrame);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bad = 1;
    }
    if (bad) return false;

    const int w = rect[2];
    const int h = rect[3];
    if (w <= 0 || h <= 0 || w * h > kTemplateMaxPx) return false;

    void* const px = g_originalResolvePixel(pShape, nullptr, g_overrideFrame);
    if (!px) return false;

    // Source is 1 byte per pixel, row stride == frame width (proved: the blitter
    // walks the source with imul esi,[ebp-0x1C] where that field is the rect
    // width). The whole copy is inside one __try so a bad shape cannot fault.
    int nw = 0;
    const unsigned char* src = static_cast<const unsigned char*>(px);
    bad = 0;
    __try {
        for (int y = 0; y < h; ++y) {
            const unsigned char* row = src + (long long)y * w;
            for (int x = 0; x < w; ++x) {
                const unsigned char v = row[x];
                if (v == 0xFE) ++nw;
                out[(size_t)y * w + x] = v;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bad = 1;
    }
    if (bad) return false;
    *outW = w;
    *outH = h;
    *outNoWrite = nw;
    return true;
}

// Searches for the override template inside one measured box.
//
// The box came from a 16-pixel tile diff, so it is tile-quantised and LARGER
// than the image the blitter actually wrote. That is why this is a search over
// placements rather than a single fixed-offset compare: the exact destination
// origin is not observable without a production hook, but "does the box contain
// the override image somewhere" is exactly the question being asked.
//
// Read-only: touches nothing but the render target, under __try.
struct TemplateFit {
    bool found = false;
    int offX = 0;
    int offY = 0;
    int firstX = -1;          // box-local coords of the first non-wildcard miss
    int firstY = -1;
    int expectedPixel = 0;
    int actualPixel = 0;
    int compared = 0;
};

TemplateFit MatchTemplateInBox(const unsigned char* tpl, int tw, int th,
                               const int* box) {
    TemplateFit fit;
    unsigned char* base = nullptr;
    int pitch = 0, bw = 0, bh = 0;
    if (!ReadSurfaceView(&base, &pitch, &bw, &bh, nullptr)) return fit;

    // Compare only the intersection of the box with the surface, so a partially
    // visible cell can never be reported as a mismatch on out-of-surface pixels.
    const int bl = box[0] < 0 ? 0 : box[0];
    const int bt = box[1] < 0 ? 0 : box[1];
    const int br = box[2] > bw ? bw : box[2];
    const int bb = box[3] > bh ? bh : box[3];
    if (br - bl < tw || bb - bt < th) return fit;

    int bestMiss = 0x7FFFFFFF;
    TemplateFit best;
    int bad = 0;
    __try {
        for (int oy = bt; oy + th <= bb; ++oy) {
            for (int ox = bl; ox + tw <= br; ++ox) {
                int miss = 0;
                for (int y = 0; y < th && !miss; ++y) {
                    const unsigned char* arow = base + (long long)(oy + y) * pitch;
                    const unsigned char* trow = tpl + (size_t)y * tw;
                    for (int x = 0; x < tw; ++x) {
                        const unsigned char want = trow[x];
                        if (want == 0xFE) continue;   // no-write: wildcard
                        const unsigned short got =
                            *reinterpret_cast<const unsigned short*>(arow + (long long)(ox + x) * 2);
                        if ((got & 0xFF) == want) continue;
                        ++miss;
                        if (miss < bestMiss) {
                            bestMiss = miss;
                            best.offX = ox; best.offY = oy;
                            best.firstX = x; best.firstY = y;
                            best.expectedPixel = want;
                            best.actualPixel = (got & 0xFF);
                            best.compared = tw * th;
                        }
                        break;
                    }
                }
                if (miss == 0) {
                    best.found = true;
                    best.offX = ox; best.offY = oy; best.compared = tw * th;
                    return best;   // exact fit, stop early
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bad = 1;
    }
    if (bad) return TemplateFit();
    return best;
}

// Collects the candidate fog shapes without touching any hook.
//
// 0x47EFE0 picks the SHP at 0x47F018..0x47F02F: bit 0x10 of [[0xA8B230]] selects
// the object at 0x89E790 (FOG.SHP) or 0x89E794 (SHROUD.SHP). Both are read here
// and both are tried, so the flag's current value does not have to be known and
// the 0x69E740 hook needs no diagnostic instrumentation at all.
int CollectFogShapes(void** out, int cap) {
    int n = 0;
    __try {
        void* const fog = *reinterpret_cast<void**>(0x89E790);
        void* const shroud = *reinterpret_cast<void**>(0x89E794);
        if (fog && n < cap) out[n++] = fog;
        if (shroud && n < cap) out[n++] = shroud;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = 0;
    }
    return n;
}

// Runs alongside VerifyAtFrameBoundary, independently, so the two methods can
// be compared. Never mutates the framebuffer or any engine state.
void VerifyExpectedVsActual(int endedFrame) {
    if (g_regionTrackCount == 0) return;
    void* shapes[2] = { nullptr, nullptr };
    const int nShapes = CollectFogShapes(shapes, 2);
    if (nShapes == 0) { ++g_counters.expectedNoTemplate; return; }

    struct Built { unsigned char px[kTemplateMaxPx]; int w, h, nw; bool ok; };
    static Built built[2];
    int nBuilt = 0;
    for (int s = 0; s < nShapes; ++s) {
        built[s].w = built[s].h = built[s].nw = 0;
        built[s].ok = BuildOverrideTemplate(shapes[s], built[s].px, &built[s].w,
                                            &built[s].h, &built[s].nw);
        if (built[s].ok) ++nBuilt;
    }
    if (nBuilt == 0) { ++g_counters.expectedNoTemplate; return; }

    for (size_t i = 0; i < g_regionTrackCount; ++i) {
        RegionTrack& t = g_regionTrack[i];
        if (!t.armed || !t.hasBox) continue;
        if (t.lastDrawnFrame != endedFrame) continue;
        ++g_counters.expectedChecks;

        const int box[4] = { t.left, t.top, t.right, t.bottom };
        bool matched = false;
        TemplateFit bestBad;
        int bestBadShape = -1;
        bool anyReadable = false;
        for (int s = 0; s < nBuilt; ++s) {
            const TemplateFit fit = MatchTemplateInBox(built[s].px, built[s].w,
                                                       built[s].h, box);
            if (fit.compared == 0) continue;
            anyReadable = true;
            if (fit.found) {
                matched = true;
                LUA_LOG_INFO(
                    "[DFOW] EXPECTED_MATCH cell=({},{}) frame={} rect=({},{},{},{}) "
                    "template={}x{} noWrite={} origin=({},{}) shape=0x{:08X}",
                    t.x, t.y, endedFrame, t.left, t.top, t.right, t.bottom,
                    built[s].w, built[s].h, built[s].nw, fit.offX, fit.offY,
                    reinterpret_cast<uintptr_t>(shapes[s]));
                break;
            }
            if (bestBadShape < 0) { bestBad = fit; bestBadShape = s; }
        }
        if (matched) { ++g_counters.expectedMatch; continue; }
        if (!anyReadable) { ++g_counters.expectedReadFail; continue; }

        ++g_counters.expectedMismatch;
        const unsigned v = static_cast<unsigned>(bestBad.expectedPixel) & 0xFFu;
        if (v < 8u) ++g_counters.expectedMissByValue[v];
        if (g_counters.expectedMismatchLogged < 4) {
            ++g_counters.expectedMismatchLogged;
            const Built& bs = built[bestBadShape];
            LUA_LOG_INFO(
                "[DFOW] EXPECTED_MISMATCH cell=({},{}) frame={} rect=({},{},{},{}) "
                "template={}x{} noWrite={} shape=0x{:08X} boxOrigin=({},{}) "
                "firstMismatch=({},{}) expectedPixel=0x{:02X} actualPixel=0x{:02X} "
                "compared={}",
                t.x, t.y, endedFrame, t.left, t.top, t.right, t.bottom,
                bs.w, bs.h, bs.nw, reinterpret_cast<uintptr_t>(shapes[bestBadShape]),
                bestBad.offX, bestBad.offY, bestBad.firstX, bestBad.firstY,
                static_cast<unsigned>(bestBad.expectedPixel),
                static_cast<unsigned>(bestBad.actualPixel), bestBad.compared);
        }
    }
}

void LogSummary() {
    const auto& c = g_counters;
    LUA_LOG_INFO(
        "[DYNAMIC_FOW][POC] summary enabled={} drawFog={} withCell={} "
        "resolverTLS={} resolverNoTLS={} rectOverrides={} rectSkippedSame={} rectDiffers={}",
        g_pocEnabled ? 1 : 0, c.drawFogCalls, c.drawFogWithCell,
        c.resolverCallsWithTls, c.resolverCallsWithoutTls, c.overridesApplied,
        c.overridesSkippedSameFrame, c.rectPtrDiffered);

    LUA_LOG_INFO(
        "[DYNAMIC_FOW][POC] summary PIXEL pixelTLS={} pixelNoTLS={} pixelOverrides={} "
        "pixelSkippedSame={} pixelPtrDiffers={}",
        c.pixelResolverCallsWithTls, c.pixelResolverCallsWithoutTls,
        c.pixelOverridesApplied, c.pixelOverridesSkippedSameFrame, c.pixelPtrDiffered);
    // Explicitly the last LOGGED event, not the last override: with finite
    // budgets the two differ, and conflating them was a defect.
    LUA_LOG_INFO(
        "[DFOW] MARKS count={} added={} removed={} pixelHits={} pixelMiss={} firstHit=({},{})",
        g_markCount, c.marksTotal, c.marksRemoved, c.rendererHit,
        c.rendererMiss, c.firstHitX, c.firstHitY);

    // Intersection diagnostic. Answers, from one line, whether the DrawFog cell
    // and the pixel-resolver cell are the same object and whether the cells Lua
    // marks are ever among the cells the engine fogs.
    LUA_LOG_INFO(
        "[DFOW] INTERSECT drawFog={} drawWithCell={} drawMarkedHits={} drawMarkedMisses={} "
        "resolverTLS={} resolverNoTLS={} resolverMarkedTls={} resolverUnmarkedTls={} "
        "ptrMatch={} ptrMismatch={} insideDrawFog={} outsideDrawFog={}",
        c.drawFogCalls, c.drawFogWithCell, c.drawMarkedHits, c.drawMarkedMisses,
        c.pixelResolverCallsWithTls, c.pixelResolverCallsWithoutTls,
        c.resolverMarkedTls, c.resolverUnmarkedTls, c.resolverPtrMatch,
        c.resolverPtrMismatch, c.resolverInsideDrawFog, c.resolverOutsideDrawFog);

    // The concrete coordinates the intersection counters above are measured
    // against, so a mismatch can be diagnosed without re-running.
    if (g_markTraceCount > 0) {
        std::string coords;
        coords.reserve(g_markTraceCount * 10);
        for (size_t i = 0; i < g_markTraceCount; ++i) {
            if (i) coords += " ";
            coords += "(" + std::to_string(g_markTrace[i].x) + "," +
                      std::to_string(g_markTrace[i].y) + ")";
        }
        LUA_LOG_INFO("[DFOW] FIRST_MARKS n={} coords={}", g_markTraceCount, coords);
    } else {
        LUA_LOG_INFO("[DFOW] FIRST_MARKS n=0 coords=<none>");
    }
    LUA_LOG_INFO(
        "[DFOW] LAST_DRAWCELL ptr=0x{:08X} cell=({},{})",
        c.lastDrawCellPtr, c.lastDrawCellX, c.lastDrawCellY);
    // Post-draw overwrite verdict inputs. regionChanged is the FAIL signal: a
    // tracked fog window changed with no DrawFog of that cell in between, so
    // something other than the fog pass wrote into it.
    LUA_LOG_INFO(
        "[DFOW] REGION armed={} validated={} tracked={} checks={} boundaryChecks={} "
        "changed={} changedRedraw={} vanished={} skipNoDraw={} skipNotDrawnYet={} "
        "frame={} lastBoundary={} probeCell=({},{})",
        c.regionProbeArmed, c.regionValidated, g_regionTrackCount, c.regionChecks,
        c.regionBoundaryChecks, c.regionChanged, c.regionChangedRedraw,
        c.regionVanished, c.regionSkipNoDraw, c.regionSkipNotDrawnYet,
        c.currentFrameSeen, c.lastBoundaryFrame,
        c.lastProbeCellX, c.lastProbeCellY);
    // Arming failures. If these dominate, the window geometry is wrong and the
    // probe has no evidence either way - that is an INCONCLUSIVE, not a PASS.
    LUA_LOG_INFO(
        "[DFOW] PASSTHROUGH SUMMARY passthrough={} markedCalls={} unmarkedCalls={} "
        "uniqueCells={} frame0={} frame15={} other={} frameMin={} frameMax={} "
        "overrideFrame={} pixelOverrides={} pixelPtrDiffers={}",
        g_passthroughMode ? 1 : 0, c.markedPixelCalls, c.unmarkedPixelCalls,
        c.markedUniqueCells, c.markedFrame0, c.markedFrame15, c.markedFrameOther,
        c.markedFrameMin, c.markedFrameMax, g_overrideFrame,
        c.pixelOverridesApplied, c.pixelPtrDiffered);
    {
        char mbuf[512], ubuf[512];
        int mu = 0, uu = 0;
        mbuf[0] = ubuf[0] = '\0';
        for (int i = 0; i < 64; ++i) {
            if (!c.markedFrameHistogram[i]) continue;
            int n = _snprintf_s(mbuf + mu, sizeof(mbuf) - mu, _TRUNCATE, "%s%d:%llu",
                                mu ? "," : "", i, c.markedFrameHistogram[i]);
            if (n <= 0) break;
            mu += n;
        }
        for (int i = 0; i < 64; ++i) {
            if (!c.unmarkedFrameHistogram[i]) continue;
            int n = _snprintf_s(ubuf + uu, sizeof(ubuf) - uu, _TRUNCATE, "%s%d:%llu",
                                uu ? "," : "", i, c.unmarkedFrameHistogram[i]);
            if (n <= 0) break;
            uu += n;
        }
        LUA_LOG_INFO("[DFOW] MARKED_FRAME_HISTOGRAM marked[{}]", mbuf);
        LUA_LOG_INFO("[DFOW] UNMARKED_FRAME_HISTOGRAM unmarked[{}]", ubuf);
    }
    LUA_LOG_INFO(
        "[DFOW] EXPECTED checks={} match={} mismatch={} readFail={} noWrite={} "
        "noTemplate={} missByValue(0..7)={},{},{},{},{},{},{},{}",
        c.expectedChecks, c.expectedMatch, c.expectedMismatch,
        c.expectedReadFail, c.expectedNoWrite, c.expectedNoTemplate,
        c.expectedMissByValue[0], c.expectedMissByValue[1], c.expectedMissByValue[2],
        c.expectedMissByValue[3], c.expectedMissByValue[4], c.expectedMissByValue[5],
        c.expectedMissByValue[6], c.expectedMissByValue[7]);
    LUA_LOG_INFO(
        "[DFOW] PROBE_FAIL noSurface={} invalidStruct={} invalidGeom={} "
        "emptyRegion={} hashRead={} hashBasis={} argDumps={} boxesLearned={}",
        c.probeFailNoSurface, c.probeFailInvalidStruct, c.probeFailInvalidGeom,
        c.probeFailEmptyRegion, c.probeFailHashRead, c.probeFailHashBasis,
        c.probeArgLogged, c.probeBoxesLearned);
    LUA_LOG_INFO(
        "[DYNAMIC_FOW][POC] summary PIXEL pixelTLS={} pixelNoTLS={} pixelOverrides={} "
        "pixelSkippedSame={} pixelPtrDiffers={} targetFrame={}",
        c.pixelResolverCallsWithTls, c.pixelResolverCallsWithoutTls,
        c.pixelOverridesApplied, c.pixelOverridesSkippedSameFrame, c.pixelPtrDiffered,
        g_overrideFrame);

    // Frame histogram, printed only for the non-zero buckets, so the log stays
    // small while still answering "which frames does the engine really emit".
    char buf[512];
    int used = 0;
    buf[0] = '\0';
    for (int i = 0; i < 64; ++i) {
        if (!c.frameHistogram[i]) continue;
        int n = _snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
                            "%s%d:%llu", used ? "," : "", i, c.frameHistogram[i]);
        if (n <= 0) break;
        used += n;
    }
    if (used > 0)
        LUA_LOG_INFO("[DYNAMIC_FOW][POC] frameHistogram(original) {}", buf);

    // -----------------------------------------------------------------------
    // PASSTHROUGH diagnostics: per-frame engine selection for MARKED cells.
    //
    // The frame recorded in the 0x69E740 detour is the `frame` argument the
    // engine passed in, i.e. the frame the engine itself selected. It is read
    // directly off the resolver call, never reconstructed from the pixel
    // pointer, and it is collected BEFORE the substitution decision, so the
    // distribution is identical whether the override is armed or not.
    //
    // The aggregate marked/unmarked histograms and the PASSTHROUGH SUMMARY live
    // in the block above; this only adds the per-bucket lines.
    //
    // Read-only: formats and logs only. It cannot change the frame, the pixel
    // pointer, the return value, or the marked-cell set.
    // -----------------------------------------------------------------------
    {
        int emitted = 0;
        for (int i = 0; i < 64 && emitted < 12; ++i) {
            if (!c.markedFrameHistogram[i]) continue;
            ++emitted;
            LUA_LOG_INFO("[DFOW] MARKED_FRAME frame={} count={}", i,
                         c.markedFrameHistogram[i]);
        }
        if (emitted >= 12) {
            int more = 0;
            for (int i = 0; i < 64; ++i)
                if (c.markedFrameHistogram[i]) ++more;
            LUA_LOG_INFO("[DFOW] MARKED_FRAME ... {} further non-zero buckets", more - 12);
        }
    }
}

// ---------------------------------------------------------------------------
// Detours
// ---------------------------------------------------------------------------

// Captures the cell, then runs the vanilla DrawFog body completely untouched.
// The frame is computed inside that body and reaches the blitters, so this hook
// exists purely to answer "which cell is being fogged right now".
void __fastcall Hooked_DrawFog(CellClass* pThis, void* /*edx*/, void* pRect, DWORD extra) {
    if (!g_originalDrawFog) return;

    // Frame-boundary latch, before anything else. A new CurrentFrame means the
    // previous frame's render passes are all finished, which is the only point
    // where a post-draw sample is unambiguous. Fires once per transition.
    PumpFrameBoundary();

    CellClass* const saved = tls_activeCell;
    tls_activeCell = pThis;
    ++g_counters.tlsDepth;

    ++g_counters.drawFogCalls;
    if (pThis) ++g_counters.drawFogWithCell;

    // Probe state for this call. Declared out here so the post-body half below
    // can use them; they are pure locals and never outlive the frame.
    uint32_t probeBefore = 0;
    bool probeActive = false;
    bool probeLearning = false;
    int probeCellX = -1;
    int probeCellY = -1;

    // ---- intersection diagnostic (read-only) ------------------------------
    // Records which cell DrawFog is fogging and whether Lua marked it. This is
    // the DrawFog half of the "is the same CellClass* flowing through both
    // hooks" question; the resolver half is in Hooked_ResolvePixel.
    if (pThis) {
        const CellCoords dc = ReadCellCoordsSafe(pThis);
        g_counters.lastDrawCellPtr = reinterpret_cast<uintptr_t>(pThis);
        g_counters.lastDrawCellX = dc.x;
        g_counters.lastDrawCellY = dc.y;
        const bool isMarked = IsCellMarked(dc.x, dc.y);
        if (isMarked) ++g_counters.drawMarkedHits;
        else ++g_counters.drawMarkedMisses;

        if (g_counters.drawTracePrinted < 8) {
            ++g_counters.drawTracePrinted;
            LUA_LOG_INFO(
                "[DYNAMIC_FOW][DIAG] DrawFog#{} cell=0x{:08X} cell=({},{}) marked={} "
                "depth={} markCount={}",
                g_counters.drawTracePrinted,
                reinterpret_cast<uintptr_t>(pThis), dc.x, dc.y, isMarked ? "true" : "false",
            g_counters.tlsDepth, g_markCount);
        }

        // ---- post-draw overwrite probe --------------------------------------
        // The window is NOT guessed. For a marked cell whose box is not yet
        // known, the whole render target is reduced to a 16x16-pixel tile grid
        // here, so the post-body pass can measure exactly which tiles DrawFog
        // wrote. Once a box is learned it is reused and hashed directly.
        // Read-only; no engine state is touched.
        if (isMarked) {
            probeCellX = dc.x;
            probeCellY = dc.y;
            ProbeDumpArgs(pRect, reinterpret_cast<void*>(extra), dc.x, dc.y);

            RegionTrack* t = FindOrCreateTrack(dc.x, dc.y);
            if (t) {
                if (!t->hasBox) {
                    if (g_probeBoxLearned < kProbeBoxLearnLimit) {
                        int snapFail = kProbeOk;
                        if (SnapshotTiles(g_tileBefore, &snapFail)) {
                            probeLearning = true;
                        } else {
                            CountProbeFail(snapFail);
                        }
                    } else {
                        CountProbeFail(kProbeInvalidGeom);
                    }
                } else {
                    int fail = kProbeOk;
                    probeBefore = HashBox(t->left, t->top, t->right, t->bottom, &fail);
                    if (probeBefore == 0) CountProbeFail(fail);
                    else probeActive = true;
                }
            }
        }
    }
    // -----------------------------------------------------------------------

    if (pThis && VerboseBudget()) {
        const CellCoords c = ReadCellCoordsSafe(pThis);
        const CellFogPeek f = PeekCellFogSafe(pThis);
        LUA_LOG_INFO(
            "[DYNAMIC_FOW][POC] DrawFog cell=0x{:08X} rawCoord=({},{}) yrppCoord=({},{}) "
            "coordOk={} cell+0x120={} cell+0x121={} fogPeekOk={} rect=0x{:08X} extra={}",
            reinterpret_cast<uintptr_t>(pThis), c.x, c.y, c.yrppX, c.yrppY,
            c.ok ? 1 : 0, static_cast<int>(f.visibility),
            static_cast<int>(f.foggedness), f.ok ? 1 : 0,
            reinterpret_cast<uintptr_t>(pRect), static_cast<unsigned>(extra));
    }

    // Vanilla body, unmodified. Any SEH here is reported and swallowed so a
    // fault in the engine draw path cannot be attributed to the PoC silently;
    // the original return path is re-entered only once.
    __try {
        g_originalDrawFog(pThis, nullptr, pRect, extra);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] SEH inside vanilla DrawFog for cell 0x{:08X}",
                     reinterpret_cast<uintptr_t>(pThis));
    }

    // ---- post-draw overwrite probe ------------------------------------------
    // Two paths, both empirical:
    //  * learning  - the tile grid taken before the body is recomputed now, and
    //    the tiles that moved give the box DrawFog actually wrote. That box is
    //    then hashed to establish the baseline.
    //  * armed     - a known box is re-hashed; a change means something wrote
    //    there. "ownDrawFogSinceArm" separates the expected redraw from a
    //    foreign write, and only the latter counts as changed.
    if (probeLearning) {
        int snapFailAfter = kProbeOk;
        if (SnapshotTiles(g_tileAfter, &snapFailAfter)) {
            int box[4] = {0, 0, 0, 0};
            const int nTiles = DiffTilesToBox(box);
            if (nTiles == 0) {
                CountProbeFail(kProbeInvalidGeom);
            } else {
                RegionTrack* t = FindOrCreateTrack(probeCellX, probeCellY);
                if (t) {
                    t->left = box[0];
                    t->top = box[1];
                    t->right = box[2];
                    t->bottom = box[3];
                    int fail = kProbeOk;
                    const uint32_t after = HashBox(box[0], box[1], box[2], box[3], &fail);
                    if (after == 0) {
                        CountProbeFail(fail);
                    } else {
                        // The box was derived from pixels this very body moved,
                        // so it provably covers the write: this is validation,
                        // not a geometry assumption.
                        t->hasBox = true;
                        t->armed = true;
                        t->baseline = after;
                        t->armedFrame = g_counters.drawFogCalls;
                        t->ownDrawFogSinceArm = 0;
                        t->lastDrawnFrame = g_counters.currentFrameSeen;
                        ++g_counters.regionValidated;
                        ++g_counters.regionProbeArmed;
                        ++g_probeBoxLearned;
                        ++g_counters.probeBoxesLearned;
                        g_counters.lastProbeCellX = probeCellX;
                        g_counters.lastProbeCellY = probeCellY;
                        LUA_LOG_INFO(
                            "[DFOW] PROBE_BOX cell=({},{}) box=({},{},{},{}) "
                            "changedTiles={} baseline=0x{:08X}",
                            probeCellX, probeCellY, box[0], box[1], box[2], box[3],
                            nTiles, after);
                    }
                }
            }
        } else {
            CountProbeFail(snapFailAfter);
        }
    } else if (probeActive) {
        RegionTrack* t = FindOrCreateTrack(probeCellX, probeCellY);
        if (t && t->hasBox) {
            int fail = kProbeOk;
            const uint32_t after = HashBox(t->left, t->top, t->right, t->bottom, &fail);
            if (after == 0) {
                ++g_counters.regionVanished;
                CountProbeFail(fail);
            } else if (after != probeBefore) {
                // The body moved the box contents, so the window is confirmed to
                // cover this cell's write. Refresh the baseline; the redraw is
                // this cell's own and must not be counted as a foreign write.
                t->baseline = after;
                t->armedFrame = g_counters.drawFogCalls;
                t->lastDrawnFrame = g_counters.currentFrameSeen;
                ++t->ownDrawFogSinceArm;
            }
        }
    }
    // -----------------------------------------------------------------------

    tls_activeCell = saved;
    if (g_counters.tlsDepth > 0) --g_counters.tlsDepth;

    // One clock read per fogged cell; the comparison does the throttling.
    PumpSummary();
}

// Re-checksums every armed window. Called from the throttled summary pump, not
// per pixel and not per DrawFog, so the cost is a few kilobytes a second.
// Verifies tracked regions ONCE per frame boundary, not mid-frame.
//
// Why it cannot run mid-frame: the engine repaints the whole visible map every
// frame, so a sample taken at the end of any DrawFog call can see the previous
// frame's terrain pass having already rewritten the box, before this frame's
// DrawFog for that cell has run. That looks exactly like a foreign overwrite and
// is a false FAIL. Sampling at the transition to a new CurrentFrame removes that
// confounder: the transition means the previous frame's passes are all done.
//
// A cell that did not receive DrawFog in the frame that just ended is SKIPPED,
// never classified: a cell scrolled out of view, or one the fog loop never
// reached, says nothing about overwriting. ownDrawFogSinceArm is not used as the
// verdict here at all; the only question asked is "did the box change between
// the end of this cell's last DrawFog and the end of that frame".
void VerifyAtFrameBoundary(int endedFrame) {
    if (g_regionTrackCount == 0) return;
    ++g_counters.regionChecks;
    ++g_counters.regionBoundaryChecks;
    for (size_t i = 0; i < g_regionTrackCount; ++i) {
        RegionTrack& t = g_regionTrack[i];
        if (!t.armed || !t.hasBox) { ++g_counters.regionSkipNotDrawnYet; continue; }
        // No DrawFog for this cell in the frame that just ended: the box was
        // never written this frame, so a difference proves nothing.
        if (t.lastDrawnFrame != endedFrame) { ++g_counters.regionSkipNoDraw; continue; }

        int failNow = kProbeOk;
        const uint32_t now = HashBox(t.left, t.top, t.right, t.bottom, &failNow);
        if (now == 0) {
            ++g_counters.regionVanished;
            CountProbeFail(failNow);
            continue;
        }
        if (now == t.baseline) continue;

        // The box changed after this cell's own DrawFog and before the end of
        // that same frame. That is the post-draw overwrite signal.
        ++g_counters.regionChanged;
        LUA_LOG_INFO(
            "[DFOW] REGION_CHANGED cell=({},{}) rect=({},{},{},{}) frame={} "
            "baseline=0x{:08X} now=0x{:08X} drawFogCalls={}",
            t.x, t.y, t.left, t.top, t.right, t.bottom, endedFrame,
            t.baseline, now, g_counters.drawFogCalls);
        t.baseline = now;
    }
}

// Called from the DrawFog hook, which runs many times per frame, so the
// transition is latched: the verification fires exactly once per new frame,
// no matter how many DrawFog calls that frame produces.
void PumpFrameBoundary() {
    const int frame = static_cast<int>(Unsorted::CurrentFrame);
    g_counters.currentFrameSeen = frame;
    if (frame == g_counters.lastBoundaryFrame) return;
    const int ended = g_counters.lastBoundaryFrame;
    g_counters.lastBoundaryFrame = frame;
    if (ended < 0) return;   // first sighting: nothing has ended yet
    VerifyAtFrameBoundary(ended);
    // Second, independent method: content-based rather than temporal.
    VerifyExpectedVsActual(ended);
}

// Which SHP is the blitter currently drawing from? Blitter #1 selects it at
// 0x47F018..0x47F02F: bit 0x10 of dword [[0xA8B230]] picks between the object
// at 0x89E790 (loaded from the string "FOG.SHP") and 0x89E794 ("SHROUD.SHP").
// A frame index therefore has DIFFERENT content depending on the sheet, which
// is the leading candidate for "forcing one index yields black squares".
// Read-only; reads nothing the PoC does not already read elsewhere.
const char* IdentifySheet(void* pShape) {
    auto* const flagAddr = reinterpret_cast<unsigned char*>(0xA8B230);
    unsigned char* base = nullptr;
    __try {
        base = *reinterpret_cast<unsigned char**>(flagAddr);
        if (!base) return "unknown(flag-null)";
        const unsigned d = *reinterpret_cast<const unsigned*>(base);
        void* const fog = *reinterpret_cast<void**>(0x89E790);
        void* const shroud = *reinterpret_cast<void**>(0x89E794);
        if (pShape && pShape == fog) return "FOG.SHP";
        if (pShape && pShape == shroud) return "SHROUD.SHP";
        return ((d & 0x10) ? "other(bit=FOG)" : "other(bit=SHROUD)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "unknown(read-fault)";
    }
}

// The gate, now driven by the Lua bridge instead of a hardcoded coordinate.
// Still shared by both detours so they can never disagree about which cell is
// being drawn.
bool ShouldOverrideCell(CellClass* pCell) {
    if (!g_pocEnabled || !pCell) return false;
    const CellCoords c = ReadCellCoordsSafe(pCell);
    if (!c.ok) return false;
    return IsCellMarked(c.x, c.y);
}

// PURE PASSTHROUGH. Kept hooked only so the frame histogram below keeps
// recording what the engine actually asks for; the frame argument is never
// substituted here.
//
// Reason, measured at runtime: 0x69E7E0 returns only the frame's GEOMETRY, and
// that geometry is identical for every frame of a tile sheet
// (origRect == ovrRect == (0,0,60,30), rectDiffers=0 over every logged
// override). Substituting the frame here is provably a visual no-op, and it
// desynchronises this channel from the pixel channel, which IS the channel that
// matters. The visual override lives at 0x69E740 only.
void* __fastcall Hooked_ResolveFrame(void* pShape, void* /*edx*/, void* pOutRect, int frame) {
    if (!g_originalResolveFrame) return pOutRect;

    CellClass* const cell = tls_activeCell;
    if (cell) {
        ++g_counters.resolverCallsWithTls;
        if (frame >= 0 && frame < 64)
            ++g_counters.frameHistogram[frame];
    } else {
        ++g_counters.resolverCallsWithoutTls;
    }

    if (VerboseBudget())
        LUA_LOG_INFO(
            "[DYNAMIC_FOW][POC] rectResolver PASSTHROUGH tls={} frame={} shape=0x{:08X} "
            "(no rect override by design)",
            cell ? 1 : 0, frame, reinterpret_cast<uintptr_t>(pShape));

    return g_originalResolveFrame(pShape, nullptr, pOutRect, frame);
}

// ---------------------------------------------------------------------------
// One-shot, READ-ONLY diagnostic dump of two SHP frames.
//
// Buffer safety is NOT assumed from "rect looks like (0,0,60,30)". The true
// geometry is obtained by calling the saved frame resolver, and the byte count
// is derived from THAT rect: the blitter at 0x47EFE0 walks the source with
// "mov dl,[ecx] / inc ecx" for exactly clippedWidth * clippedHeight pixels with
// no row reset, so the resolver's own w/h is the authoritative extent of the
// frame. We never read past it and never write anything.
//
// Nothing here changes the frame, the returned pointer, or any game memory.
// ---------------------------------------------------------------------------

constexpr int kDumpFrames[] = { 0, 15 };
constexpr int kMaxDumpElements = 1 << 20;  // hard sanity ceiling

struct SHPShapeDump {
    bool done = false;
    uintptr_t shape = 0;
};
SHPShapeDump g_shpDump;

// Returns the number of distinct byte values present, and fills hist[].
void DumpOneFrame(void* pShape, int frame) {
    // 1. Authoritative geometry from the real resolver, not a constant.
    int rect[4] = { 0, 0, 0, 0 };
    g_originalResolveFrame(pShape, nullptr, rect, frame);

    // 2. Authoritative pixel pointer from the real resolver.
    void* const px = g_originalResolvePixel(pShape, nullptr, frame);

    const int w = rect[2];
    const int h = rect[3];

    LUA_LOG_INFO(
        "[DYNAMIC_FOW][SHP] shape=0x{:08X} frame={} rect=({},{},{},{}) w={} h={} "
        "pixelPtr=0x{:08X}",
        reinterpret_cast<uintptr_t>(pShape), frame, rect[0], rect[1], rect[2], rect[3],
        w, h, reinterpret_cast<uintptr_t>(px));

    if (!pShape || !px) {
        LUA_LOG_WARN("[DYNAMIC_FOW][SHP] frame={} skipped: null shape or pixelPtr", frame);
        return;
    }
    // 3. Refuse anything implausible rather than guessing a size.
    if (w <= 0 || h <= 0 || (long long)w * h > kMaxDumpElements) {
        LUA_LOG_WARN(
            "[DYNAMIC_FOW][SHP] frame={} DUMP_BLOCKED: implausible geometry w={} h={}",
            frame, w, h);
        return;
    }

    const int total = w * h;
    int countFe = 0, countNonFe = 0, vmin = 255, vmax = 0;
    int bx1 = w, by1 = h, bx2 = -1, by2 = -1;
    unsigned hist[256];
    for (int i = 0; i < 256; ++i) hist[i] = 0;

    bool ok = false;
    // __try lives in a function whose only locals are POD, so C2712 is respected.
    __try {
        const auto* const p = static_cast<const unsigned char*>(px);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const unsigned v = p[(long long)y * w + x];
                ++hist[v];
                if ((int)v < vmin) vmin = (int)v;
                if ((int)v > vmax) vmax = (int)v;
                if (v == 0xFEu) {
                    ++countFe;
                } else {
                    ++countNonFe;
                    if (x < bx1) bx1 = x;
                    if (y < by1) by1 = y;
                    if (x > bx2) bx2 = x;
                    if (y > by2) by2 = y;
                }
            }
        }
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    if (!ok) {
        LUA_LOG_WARN("[DYNAMIC_FOW][SHP] frame={} read faulted while scanning {} elements",
                     frame, total);
        return;
    }

    int unique = 0;
    for (int i = 0; i < 256; ++i)
        if (hist[i]) ++unique;

    // Permille keeps the percentage exact integer arithmetic.
    const int permilleFe = (int)((long long)countFe * 1000 / total);
    const int permilleNon = (int)((long long)countNonFe * 1000 / total);

    LUA_LOG_INFO(
        "[DYNAMIC_FOW][SHP] frame={} total={} count_FE={} count_nonFE={} "
        "pct_FE={}.{:01}% pct_nonFE={}.{:01}% min=0x{:02X} max=0x{:02X} uniqueValues={}",
        frame, total, countFe, countNonFe, permilleFe / 10, permilleFe % 10,
        permilleNon / 10, permilleNon % 10, vmin, vmax, unique);

    if (bx2 >= 0)
        LUA_LOG_INFO("[DYNAMIC_FOW][SHP] frame={} nonFE_bbox={},{} - {},{}",
                     frame, bx1, by1, bx2, by2);
    else
        LUA_LOG_INFO("[DYNAMIC_FOW][SHP] frame={} nonFE_bbox=EMPTY (every element is 0xFE)",
                     frame);

    // List only values actually present, capped so the log stays small.
    int listed = 0;
    for (int i = 0; i < 256 && listed < 24; ++i) {
        if (!hist[i]) continue;
        LUA_LOG_INFO("[DYNAMIC_FOW][SHP] frame={} value=0x{:02X} count={}",
                     frame, i, hist[i]);
        ++listed;
    }
    if (unique > listed)
        LUA_LOG_INFO("[DYNAMIC_FOW][SHP] frame={} ... {} further distinct values not listed",
                     frame, unique - listed);
}

// Runs once per session, on the first pixel-source call that carries a cell.
void MaybeDumpShape(void* pShape) {
    if (g_shpDump.done || !pShape) return;
    g_shpDump.done = true;
    g_shpDump.shape = reinterpret_cast<uintptr_t>(pShape);
    LUA_LOG_INFO("[DYNAMIC_FOW][SHP] one-shot READ-ONLY dump, shape=0x{:08X}, frames 0 and 15",
                 g_shpDump.shape);
    for (int f : kDumpFrames)
        DumpOneFrame(pShape, f);
}

// The pixel-source channel. 0x69E7E0 only yields the frame's geometry; the
// bytes the blitter actually reads come from here (record+0x14). Overriding
// this argument is the experiment the previous PoC was missing.
void* __fastcall Hooked_ResolvePixel(void* pShape, void* /*edx*/, int frame) {
    if (!g_originalResolvePixel) return nullptr;

    CellClass* const cell = tls_activeCell;
    if (!cell) {
        ++g_counters.pixelResolverCallsWithoutTls;
        if (VerboseBudget())
            LUA_LOG_INFO(
                "[DYNAMIC_FOW][POC] pixelResolver no-TLS -> passthrough frame={} shape=0x{:08X}",
                frame, reinterpret_cast<uintptr_t>(pShape));
        return g_originalResolvePixel(pShape, nullptr, frame);
    }

    ++g_counters.pixelResolverCallsWithTls;

    // ---- intersection diagnostic (read-only) ------------------------------
    // Diagnostic only: nothing below changes `frame`, the returned pointer or
    // the substitution decision made further down.
    //
    // The decisive number is resolverPtrMatch. tls_activeCell is written by
    // DrawFog and read here, so identity is expected by construction - this
    // turns that expectation into a measurement. resolverInsideDrawFog is the
    // independent check: it is non-zero only if DrawFog is genuinely still on
    // the stack, which rules out a stale or foreign TLS value.
    {
        const CellCoords rc = ReadCellCoordsSafe(cell);
        const bool rMarked = IsCellMarked(rc.x, rc.y);
        if (rMarked) {
            ++g_counters.resolverMarkedTls;

            // Engine-selected frame for a MARKED cell, recorded before any
            // substitution so it is identical in override and passthrough mode.
            ++g_counters.markedPixelCalls;
            if (frame >= 0 && frame < 64) {
                ++g_counters.markedFrameHistogram[frame];
                if (g_counters.markedFrameMin < 0 || frame < g_counters.markedFrameMin)
                    g_counters.markedFrameMin = frame;
                if (frame > g_counters.markedFrameMax) g_counters.markedFrameMax = frame;
                if (frame == 0) ++g_counters.markedFrame0;
                else if (frame == 15) ++g_counters.markedFrame15;
                else ++g_counters.markedFrameOther;
            }
            // First few DISTINCT marked cells, so a reader can tie a frame back
            // to an object. Coordinates come from the existing safe reader, not
            // from any new disassembly.
            const uintptr_t rPtrForTrace = reinterpret_cast<uintptr_t>(cell);
            bool known = false;
            for (unsigned i = 0; i < g_counters.markedUniqueCells && i < 16; ++i)
                if (g_counters.markedCellSeen[i] == rPtrForTrace) { known = true; break; }
            if (!known && g_counters.markedUniqueCells < 16) {
                g_counters.markedCellSeen[g_counters.markedUniqueCells++] = rPtrForTrace;
                if (g_counters.markedCellTracePrinted < 8) {
                    ++g_counters.markedCellTracePrinted;
                    LUA_LOG_INFO(
                        "[DFOW] MARKED_CELL#{} cell=0x{:08X} coords=({},{}) "
                        "engineFrame={} shape=0x{:08X}",
                        g_counters.markedCellTracePrinted, rPtrForTrace, rc.x, rc.y,
                        frame, reinterpret_cast<uintptr_t>(pShape));
                }
            }
        } else {
            ++g_counters.resolverUnmarkedTls;
            ++g_counters.unmarkedPixelCalls;
            if (frame >= 0 && frame < 64) ++g_counters.unmarkedFrameHistogram[frame];
        }

        const uintptr_t rPtr = reinterpret_cast<uintptr_t>(cell);
        if (g_counters.lastDrawCellPtr != 0 && rPtr == g_counters.lastDrawCellPtr)
            ++g_counters.resolverPtrMatch;
        else
            ++g_counters.resolverPtrMismatch;

        if (g_counters.tlsDepth > 0) ++g_counters.resolverInsideDrawFog;
        else ++g_counters.resolverOutsideDrawFog;

        if (g_counters.resolverTracePrinted < 8) {
            ++g_counters.resolverTracePrinted;
            LUA_LOG_INFO(
                "[DYNAMIC_FOW][DIAG] resolver#{} cell=0x{:08X} cell=({},{}) marked={} "
                "ptrMatch={} depth={} insideDrawFog={} frame={}",
                g_counters.resolverTracePrinted, rPtr, rc.x, rc.y,
                rMarked ? "true" : "false",
                (g_counters.lastDrawCellPtr != 0 && rPtr == g_counters.lastDrawCellPtr)
                    ? "true" : "false",
                g_counters.tlsDepth, g_counters.tlsDepth > 0 ? "true" : "false", frame);
        }
    }
    // ----------------------------------------------------------------------

    // Passive, read-only, one-shot. Runs AFTER the counters are updated and
    // BEFORE any return, and touches no engine state, so the frame, the pixel
    // pointer and the return value are all unaffected.
    MaybeDumpShape(pShape);

    int resolved = frame;
    bool applied = false;

    // The target frame must actually exist in the sheet the blitter picked, or
    // the resolver returns a null rect and the cell would draw nothing at all -
    // strictly worse than leaving it alone. Frame count lives at shape+0x06 and
    // is the same field 0x69E7E0 bounds-checks against.
    int frameCount = -1;
    __try {
        frameCount = static_cast<int>(*reinterpret_cast<const short*>(
            reinterpret_cast<const char*>(pShape) + 6));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        frameCount = -1;
    }

    if (g_passthroughMode) {
        // Diagnostic passthrough: the gate above already decided this cell is
        // marked, and the engine-selected frame was recorded. Return the
        // engine's own pixel pointer so nothing is substituted, and so the
        // frame histogram describes unmodified engine behaviour.
        return g_originalResolvePixel(pShape, nullptr, frame);
    }

    if (ShouldOverrideCell(cell)) {
        ++g_counters.rendererHit;
        const CellCoords hc = ReadCellCoordsSafe(cell);
        if (g_counters.firstHitX < 0) {
            g_counters.firstHitX = hc.x;
            g_counters.firstHitY = hc.y;
        }
        if (g_overrideFrame < 0 || frameCount < 1 || g_overrideFrame >= frameCount) {
            // Marked, but the substitution is not safe: no frame selected, or
            // the target index is outside this sheet. Count as a miss for the
            // substitution and leave the engine untouched.
            ++g_counters.rendererMiss;
            if (VerboseBudget())
                LUA_LOG_WARN(
                    "[DYNAMIC_FOW][BRIDGE] marked cell=({},{}) but NOT substituted: "
                    "overrideFrame={} shapeFrameCount={}",
                    hc.x, hc.y, g_overrideFrame, frameCount);
            return g_originalResolvePixel(pShape, nullptr, frame);
        }
        if (frame == g_overrideFrame) {
            ++g_counters.pixelOverridesSkippedSameFrame;
        } else {
            resolved = g_overrideFrame;
            applied = true;
            ++g_counters.pixelOverridesApplied;
        }
    } else {
        ++g_counters.rendererMiss;
    }

    // Every applied override is measured, unconditionally. pixelPtrDiffered
    // must be a full numerator against pixelOverridesApplied as denominator,
    // so it is NOT gated by any log budget.
    if (applied) {
        // Variant A, same rationale as the rect channel: resolve both frames
        // through the saved original pointer (never through the hooked
        // address, so there is no recursion) and return the override pointer.
        // 0x69E740 is a pure lookup behind an idempotent lazy-load guard, so
        // calling it twice for the same shape is safe.
        void* const ptrOrig = g_originalResolvePixel(pShape, nullptr, frame);
        void* const ptrOvr = g_originalResolvePixel(pShape, nullptr, resolved);
        const bool ptrDiffers = ptrOrig != ptrOvr;
        if (ptrDiffers) ++g_counters.pixelPtrDiffered;

        // Logging state is evaluated only for its side effect of recording the
        // last LOGGED event; it never gates the override itself.
        const CellCoords c = ReadCellCoordsSafe(cell);
        if (PixelLogBudget() || ShouldLogPocCell(c.x, c.y, 1)) {
            g_counters.lastLoggedPixelX = c.x;
            g_counters.lastLoggedPixelY = c.y;
            g_counters.lastLoggedPixelOrigFrame = frame;
            g_counters.lastLoggedPixelOvrFrame = resolved;
            g_counters.lastLoggedPixOrig = reinterpret_cast<uintptr_t>(ptrOrig);
            g_counters.lastLoggedPixOvr = reinterpret_cast<uintptr_t>(ptrOvr);
            LUA_LOG_INFO(
                "[DYNAMIC_FOW][BRIDGE] PIXEL_HIT cell=0x{:08X} coord=({},{}) sheet={} "
                "shape=0x{:08X} originalFrame={} -> overrideFrame={} shapeFrameCount={} "
                "originalPixelPtr=0x{:08X} overridePixelPtr=0x{:08X} pixelPtrDiffers={}",
                reinterpret_cast<uintptr_t>(cell), c.x, c.y, IdentifySheet(pShape),
                reinterpret_cast<uintptr_t>(pShape), frame, resolved, frameCount,
                reinterpret_cast<uintptr_t>(ptrOrig), reinterpret_cast<uintptr_t>(ptrOvr),
                ptrDiffers ? 1 : 0);
        }
        return ptrOvr;
    }

    if (VerboseBudget()) {
        const CellCoords c = ReadCellCoordsSafe(cell);
        LUA_LOG_INFO(
            "[DYNAMIC_FOW][POC] pixelResolver cell=0x{:08X} coord=({},{}) shape=0x{:08X} "
            "originalFrame={} resolved={} applied={}",
            reinterpret_cast<uintptr_t>(cell), c.x, c.y,
            reinterpret_cast<uintptr_t>(pShape), frame, resolved, applied ? 1 : 0);
    }

    return g_originalResolvePixel(pShape, nullptr, resolved);
}

// ---------------------------------------------------------------------------
// Periodic summary
// ---------------------------------------------------------------------------

struct SummaryPump {
    DWORD last = GetTickCount();
    SummaryPump() { last = GetTickCount(); }
};

SummaryPump g_pump;

void PumpSummary() {
    const DWORD now = GetTickCount();
    if (now - g_pump.last < kSummaryPeriodMs) return;
    g_pump.last = now;
    // Region verification deliberately does NOT happen here: this runs in the
    // middle of a render frame, where a sample is ambiguous. It is driven by
    // the CurrentFrame transition in Hooked_DrawFog instead.
    LogSummary();
}

// All-or-nothing install. If any of the three hooks cannot be created or
// enabled, every hook already enabled by this PoC is disabled again and the
// original pointers are cleared, so a partially active configuration is never
// left behind (a live DrawFog capture with no consumer is pure overhead, and a
// live rect override with no pixel override is exactly the blind spot this PoC
// exists to close).
void RollBackAll() {
    if (g_originalResolvePixel) {
        MH_DisableHook(reinterpret_cast<LPVOID>(kPixelSourceAddr));
        g_originalResolvePixel = nullptr;
    }
    if (g_originalResolveFrame) {
        MH_DisableHook(reinterpret_cast<LPVOID>(kFrameResolverAddr));
        g_originalResolveFrame = nullptr;
    }
    if (g_originalDrawFog) {
        MH_DisableHook(reinterpret_cast<LPVOID>(kDrawFogAddr));
        g_originalDrawFog = nullptr;
    }
    g_hooksInstalled = false;
    LUA_LOG_WARN("[DYNAMIC_FOW][POC] rolled back all PoC hooks (degraded: vanilla only)");
}

// ---------------------------------------------------------------------------
// Camera source for the visual A/B test.
//
// Existing project/YRpp API, no new engine hook: TacticalClass (the same
// 0x887324 singleton DrawFog already reads) publishes the engine's OWN list of
// currently visible cells as `VisibleCells[800]` + `VisibleCellCount`. Taking
// the centroid of that list yields a cell the engine itself put on screen this
// frame, so it needs no view_bound / screen-size address guessing at all.
//
// Read-only. Touches no engine state.
// ---------------------------------------------------------------------------
bool ViewportCentreCell(int& outX, int& outY) {
    outX = -1;
    outY = -1;
    bool ok = false;
    const int kSampleCap = 64;
    __try {
        auto* const tac = TacticalClass::Instance;
        if (!tac) return false;
        const int n = static_cast<int>(tac->VisibleCellCount);
        if (n <= 0) return false;
        const int step = (n > kSampleCap) ? (n / kSampleCap) : 1;
        long long sx = 0;
        long long sy = 0;
        int used = 0;
        for (int i = 0; i < n; i += step) {
            CellClass* const c = tac->VisibleCells[i];
            if (!c) continue;
            // CellClass+0x24 = MapCoords, the same raw read the renderer detour
            // uses on tls_activeCell.
            const auto* const raw =
                reinterpret_cast<const short*>(reinterpret_cast<const char*>(c) + 0x24);
            sx += raw[0];
            sy += raw[1];
            ++used;
        }
        if (used == 0) return false;
        int cx = static_cast<int>(sx / used);
        int cy = static_cast<int>(sy / used);
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx >= kFogMapSideLimit) cx = kFogMapSideLimit - 1;
        if (cy >= kFogMapSideLimit) cy = kFogMapSideLimit - 1;
        outX = cx;
        outY = cy;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

} // namespace

bool Install() {
    if (g_hooksInstalled) return true;

    // Opt-in since 2026-09-29 (crash bisection): the DrawFog/resolver detours
    // stay DETACHED unless LUAAPI_DFOW_HOOKS=1. With visual marks off (the
    // production dynamic_fow config) the hooks only ever observed - nothing
    // in the render path needs them. Set the env var to re-attach for PoC
    // pixel experiments.
    {
        const char* hooksEnv = std::getenv("LUAAPI_DFOW_HOOKS");
        const bool hooksOn = (hooksEnv && hooksEnv[0] == '1' && hooksEnv[1] == '\0');
        if (!hooksOn) {
            LUA_LOG_INFO("[DYNAMIC_FOW][POC] hooks SKIPPED (LUAAPI_DFOW_HOOKS!=1): "
                         "no render-path detours attached");
            return false;
        }
    }

    // Run control, read once at install time so no rebuild is needed.
    //   LUAAPI_DYNAMIC_FOW_POC=1   -> enable mutation. DEFAULT IS DISABLED, so
    //                                  the shipped DLL is a clean passthrough
    //                                  baseline (hooks attached, nothing
    //                                  changed) until someone opts in.
    //   LUAAPI_DYNAMIC_FOW_FRAME=N -> frame index to substitute. Unset leaves
    //                                  the override inert even with POC=1.
    const char* pocEnv = std::getenv("LUAAPI_DYNAMIC_FOW_POC");
    g_pocEnabled = (pocEnv && pocEnv[0] == '1' && pocEnv[1] == '\0');

    const char* frameEnv = std::getenv("LUAAPI_DYNAMIC_FOW_FRAME");
    g_overrideFrame = kOverrideFrameInert;
    if (frameEnv && frameEnv[0] != '\0') {
        const int parsed = std::atoi(frameEnv);
        if (parsed >= 0) g_overrideFrame = parsed;
    }

    // LUAAPI_DYNAMIC_FOW_CAMERA_TEST=1 -> the Lua mod marks the cell at the
    // centre of the current viewport instead of the hardcoded (64,64), so the
    // marked cell is guaranteed to be inside the drawn region.
    const char* camEnv = std::getenv("LUAAPI_DYNAMIC_FOW_CAMERA_TEST");
    g_cameraTest = (camEnv && camEnv[0] == '1' && camEnv[1] == '\0');

    // Diagnostic passthrough: marking and the full diagnostic pipeline stay
    // active, but no frame or pixel pointer is ever substituted. Lets the
    // engine-selected frame be measured for marked cells without the override
    // contaminating the framebuffer.
    const char* ptEnv = std::getenv("LUAAPI_DYNAMIC_FOW_PASSTHROUGH");
    g_passthroughMode = (ptEnv && ptEnv[0] == '1' && ptEnv[1] == '\0');
    if (g_passthroughMode) {
        LUA_LOG_INFO("[DYNAMIC_FOW][POC] PASSTHROUGH mode: marking stays active, "
                     "substitution disabled (overrideFrame={} is NOT applied)",
                     g_overrideFrame);
    }
    if (g_pocEnabled && g_overrideFrame < 0) {
        LUA_LOG_INFO("[DYNAMIC_FOW][POC] mutation ENABLED but no frame selected "
                     "(LUAAPI_DYNAMIC_FOW_FRAME unset) -> staying a pure passthrough");
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kDrawFogAddr), 64,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] VirtualProtect(DrawFog 0x{:X}) failed (error {})",
                     kDrawFogAddr, GetLastError());
    }
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kFrameResolverAddr), 64,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] VirtualProtect(0x69E7E0) failed (error {})",
                     GetLastError());
    }
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kPixelSourceAddr), 64,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] VirtualProtect(0x69E740) failed (error {})",
                     GetLastError());
    }

    MH_STATUS st = MH_CreateHook(reinterpret_cast<LPVOID>(kDrawFogAddr),
                                  reinterpret_cast<LPVOID>(&Hooked_DrawFog),
                                  reinterpret_cast<LPVOID*>(&g_originalDrawFog));
    LUA_LOG_INFO("[DYNAMIC_FOW][POC] MH_CreateHook(DrawFog @ 0x{:X}) -> {} ({})",
                 kDrawFogAddr, MH_StatusToString(st), static_cast<int>(st));
    if (st != MH_OK) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] DrawFog hook NOT installed (degraded: no capture)");
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kDrawFogAddr));
    if (st != MH_OK) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] DrawFog hook could not be enabled (error {})",
                     static_cast<int>(st));
        return false;
    }

    st = MH_CreateHook(reinterpret_cast<LPVOID>(kFrameResolverAddr),
                       reinterpret_cast<LPVOID>(&Hooked_ResolveFrame),
                       reinterpret_cast<LPVOID*>(&g_originalResolveFrame));
    LUA_LOG_INFO("[DYNAMIC_FOW][POC] MH_CreateHook(frameResolver @ 0x{:X}) -> {} ({})",
                 kFrameResolverAddr, MH_StatusToString(st), static_cast<int>(st));
    if (st != MH_OK) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] resolver hook NOT installed "
                     "(DrawFog capture stays active but no frame can change)");
        // Roll the first hook back so we do not leave a live capture with no
        // consumer, which would be pure overhead.
        RollBackAll();
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kFrameResolverAddr));
    if (st != MH_OK) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] resolver hook could not be enabled (error {})",
                     static_cast<int>(st));
        RollBackAll();
        return false;
    }

    st = MH_CreateHook(reinterpret_cast<LPVOID>(kPixelSourceAddr),
                       reinterpret_cast<LPVOID>(&Hooked_ResolvePixel),
                       reinterpret_cast<LPVOID*>(&g_originalResolvePixel));
    LUA_LOG_INFO("[DYNAMIC_FOW][POC] MH_CreateHook(pixelResolver @ 0x{:X}) -> {} ({})",
                 kPixelSourceAddr, MH_StatusToString(st), static_cast<int>(st));
    if (st != MH_OK) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] pixel-source hook NOT installed "
                     "(rect channel would stay active but the visual pixels "
                     "would remain original - that is the known blind spot)");
        RollBackAll();
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kPixelSourceAddr));
    if (st != MH_OK) {
        LUA_LOG_WARN("[DYNAMIC_FOW][POC] pixel-source hook could not be enabled (error {})",
                     static_cast<int>(st));
        RollBackAll();
        return false;
    }

    g_hooksInstalled = true;
    // Install() starts from an empty mark set. Statics are already zeroed, but
    // say so explicitly: Install() may run again in a process that Disable()'d,
    // and "marks empty at install" is a lifecycle guarantee, not an accident of
    // static initialisation.
    g_markedCells.clear();
    for (size_t i = 0; i < (sizeof(g_markBits) / sizeof(g_markBits[0])); ++i)
        g_markBits[i] = 0;
    g_markCount = 0;
    LUA_LOG_INFO(
        "[DYNAMIC_FOW][POC] hooks installed: DrawFog=0x{:X} rectResolver=0x{:X} "
        "pixelResolver=0x{:X} enabled={} overrideFrame={} "
        "rectOverride=DISABLED(passthrough) markCount=0",
        kDrawFogAddr, kFrameResolverAddr, kPixelSourceAddr, g_pocEnabled ? 1 : 0,
        g_overrideFrame);
    LogSummary();
    return true;
}

int ClearMarks() { return ClearMarkedCellsInternal(); }

void Disable() {
    // Keep the hooks installed so the "off" path is still exercised and
    // measurable; only the substitution stops. A new match therefore starts
    // with vanilla frames and a clean cell capture.
    g_pocEnabled = false;
    tls_activeCell = nullptr;
    g_pump.last = GetTickCount();
    // Marked coordinates are per-match state. ResetSession() routes here, so a
    // new scenario can never inherit a previous match's reveal cells.
    ClearMarks();
    LUA_LOG_INFO("[DYNAMIC_FOW][POC] substitution disabled (hooks still attached, "
                 "frames pass through)");
    LogSummary();
}

// ---------------------------------------------------------------------------
// Lua bridge
// ---------------------------------------------------------------------------
namespace {

// DynamicFow.SetCell(x, y, on) -> boolean
int DynamicFow_SetCell(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    const int on = lua_toboolean(L, 3) ? 1 : 0;

    if (x < 0 || y < 0 || x >= kFogMapSideLimit || y >= kFogMapSideLimit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (on) {
        MarkCell(x, y);
    } else {
        const size_t bi = static_cast<size_t>(y) * kFogMapSideLimit +
                          static_cast<size_t>(x);
        if (g_markBits[bi >> 6] & (1ull << (bi & 63))) {
            g_markBits[bi >> 6] &= ~(1ull << (bi & 63));
            if (g_markCount > 0) --g_markCount;
            ++g_counters.marksRemoved;
            const uint32_t key = PackCell(x, y);
            for (size_t i = 0; i < g_markedCells.size(); ++i) {
                if (g_markedCells[i] != key) continue;
                g_markedCells.erase(g_markedCells.begin() +
                                    static_cast<ptrdiff_t>(i));
                break;
            }
        }
    }

    lua_pushboolean(L, 1);
    return 1;
}

// DynamicFow.ClearCells() -> integer (number of cells that were cleared)
int DynamicFow_ClearCells(lua_State* L) {
    lua_pushinteger(L, ClearMarkedCellsInternal());
    return 1;
}

// DynamicFow.GetMarkCount() -> integer
int DynamicFow_GetMarkCount(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(g_markCount));
    return 1;
}

// DynamicFow.SetOverrideFrame(frame) -> boolean
// Runtime opt-in for the pixel-source override (0x69E740 substitution).
// Graduates the path from env-gated diagnostic to mod-driven visual: sets the
// frame index AND enables the gate, so no LUAAPI_DYNAMIC_FOW_POC=1 /
// LUAAPI_DYNAMIC_FOW_FRAME=N environment is required. frame < 0 disables
// (back to pure passthrough measurement). Logged; bounds-checked against the
// documented 0..48 sheet range (frame 15 = fully-occluded black diamond,
// proven live: pixelPtrDiffers>0 with honest pixel substitution).
int DynamicFow_SetOverrideFrame(lua_State* L) {
    const int frame = static_cast<int>(luaL_checkinteger(L, 1));
    if (frame < 0) {
        g_overrideFrame = kOverrideFrameInert;
        g_pocEnabled = false;
        LUA_LOG_INFO("[DYNAMIC_FOW][BRIDGE] SetOverrideFrame(-): override DISABLED");
        lua_pushboolean(L, 1);
        return 1;
    }
    if (frame > 48) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_overrideFrame = frame;
    g_pocEnabled = true;
    LUA_LOG_INFO("[DYNAMIC_FOW][BRIDGE] SetOverrideFrame({}): override ENABLED at runtime",
                 frame);
    lua_pushboolean(L, 1);
    return 1;
}

// DynamicFow.CameraTestEnabled() -> boolean
int DynamicFow_CameraTestEnabled(lua_State* L) {
    lua_pushboolean(L, g_cameraTest ? 1 : 0);
    return 1;
}

// DynamicFow.GetViewportCenterCell() -> x, y
// Read-only camera query built on the engine's own TacticalClass visible-cell
// list. Returns nil when the camera state is not available yet.
int DynamicFow_GetViewportCenterCell(lua_State* L) {
    int x = -1;
    int y = -1;
    if (!ViewportCentreCell(x, y)) {
        lua_pushnil(L);
        return 1;
    }
    LUA_LOG_INFO("[DYNAMIC_FOW][BRIDGE] GetViewportCenterCell -> ({},{})", x, y);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

} // namespace

void RegisterDynamicFowBindings(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, DynamicFow_SetCell);
    lua_setfield(L, -2, "SetCell");
    lua_pushcfunction(L, DynamicFow_ClearCells);
    lua_setfield(L, -2, "ClearCells");
    lua_pushcfunction(L, DynamicFow_SetOverrideFrame);
    lua_setfield(L, -2, "SetOverrideFrame");
    lua_pushcfunction(L, DynamicFow_GetMarkCount);
    lua_setfield(L, -2, "GetMarkCount");
    lua_pushcfunction(L, DynamicFow_CameraTestEnabled);
    lua_setfield(L, -2, "CameraTestEnabled");
    lua_pushcfunction(L, DynamicFow_GetViewportCenterCell);
    lua_setfield(L, -2, "GetViewportCenterCell");
    lua_setglobal(L, "DynamicFow");
    LUA_LOG_INFO("[DYNAMIC_FOW][BRIDGE] Lua bindings registered: "
                 "DynamicFow.SetCell / ClearCells / SetOverrideFrame / "
                 "GetMarkCount / CameraTestEnabled / GetViewportCenterCell  cameraTest={}",
                 g_cameraTest ? 1 : 0);
}

} // namespace DynamicFowPoc
} // namespace LuaAPI
