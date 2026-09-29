#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/crash_reporter.hpp>
#include <LuaAPI/logger.hpp>

#include <cstring>
#include <string>
#include <unordered_map>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

// YRpp game classes
#include <YRPP.h>

namespace LuaAPI {

// === LOSE-PROBE-REMOVE-ME BEGIN ============================================
// TEMPORARY INTERNAL DIAGNOSTIC — not public API, not in API.md, not in any
// gate. Purpose: answer one question that the repo currently cannot answer —
// what does HouseClass::Lose(false) do to the OTHER houses in a multi-house
// match (1 human + N AI)? SmartAI's surrender path is already proven
// per-house, and the only prior Lose(false) evidence (SURRENDER_PHASE2
// EXPERIMENT B, Runs B/C/D) was collected 1-vs-1, where "match ends when the
// AI is defeated" is the CORRECT engine behaviour and therefore cannot
// distinguish the two hypotheses.
//
// This probe is read-only. It never calls a defeat/engine-transition API. It
// only snapshots HouseClass state and session/scenario configuration and
// writes it to LuaAPI.log.
//
// C2712: every __try helper below is POD-only (no destructors, no std types);
// formatting happens in the callers, outside __try.
//
// REMOVAL (3 places, nothing else references this):
//   1. this whole block in src/bindings_house.cpp
//   2. the single LoseProbe_Tick() call + comment in OnGameFrame
//      (src/lua_engine.cpp)
//   3. the LoseProbe_Tick() declaration in include/LuaAPI/bindings_house.hpp
// ===========================================================================

namespace {

// POD snapshot of one house. Read inside __try, formatted outside.
struct LoseProbeHouse {
    int  arrayIndex;
    int  isLoser;
    int  defeated;
    int  isGameOver;
    int  isWinner;
    int  isHuman;
    int  inPlayerControl;
    int  isObserver;
    int  isNeutral;
    int  borrowedTime;
    unsigned int allies;      // raw HouseClass::Allies bitfield
    int  buildings;
    int  units;
    int  technos;
    int  flagsOk;
    int  censusOk;
    // HouseClass exposes NO field literally named "IsActive". The two real
    // per-house liveness/production flags it does have are recorded here so
    // the intent is covered by actual engine state rather than a guess:
    //   production   -> HouseClass::Production ("AI production has begun")
    // The process-level "is the game active" flag is a GLOBAL (Game::IsActive),
    // logged once per dump as g_Active rather than per house.
    int  production;
    char id[40];
};

// POD snapshot of the match/session configuration that decides whether the
// three AI houses are really independent, or engine-level allies/team-mates.
struct LoseProbeConfig {
    int houseCount;
    int currentPlayerIndex;
    int observerIndex;
    int gameMode;
    int shortGame;
    int alliesAllowed;
    int aiPlayers;
    int sessionAllies[8];
    int fixedAlliance;
    int ctfMode;
    int scenarioInert;
    int multiplayerOnly;
    int scenarioInstanceOk;
    int ok;
};

constexpr int kProbeMaxHouses = 8;

// ---- POD-only readers (C2712: no destructors inside __try) -----------------

void LoseProbe_ReadFlags(LoseProbeHouse& s, HouseClass* pHouse) {
    __try {
        s.arrayIndex    = pHouse->ArrayIndex;
        s.isLoser       = pHouse->IsLoser ? 1 : 0;
        s.defeated      = pHouse->Defeated ? 1 : 0;
        s.isGameOver    = pHouse->IsGameOver ? 1 : 0;
        s.isWinner      = pHouse->IsWinner ? 1 : 0;
        s.isHuman       = pHouse->IsHumanPlayer ? 1 : 0;
        s.inPlayerControl = pHouse->IsInPlayerControl ? 1 : 0;
        s.isObserver    = (HouseClass::Observer == pHouse) ? 1 : 0;
        s.isNeutral     = (pHouse->Type && pHouse->Type->MultiplayPassive) ? 1 : 0;
        s.borrowedTime  = pHouse->BorrowedTime.GetTimeLeft();
        s.production    = pHouse->Production ? 1 : 0;
        s.allies        = pHouse->Allies.data;
        s.id[0] = '\0';
        if (pHouse->Type) {
            const char* pid = pHouse->Type->get_ID();
            if (pid) {
                for (int i = 0; i < 39 && pid[i]; ++i)
                    s.id[i] = pid[i];
                s.id[39] = '\0';
            }
        }
        s.flagsOk = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.flagsOk = 0;
    }
}

// Separate __try block from the flags: if the asset census walks an array the
// engine is currently tearing down, we still keep the flag readings.
void LoseProbe_ReadCensus(LoseProbeHouse& s, HouseClass* pHouse) {
    __try {
        int b = 0, un = 0, tc = 0;
        for (int i = 0; i < BuildingClass::Array.Count; ++i) {
            BuildingClass* pb = BuildingClass::Array.GetItem(i);
            if (pb && pb->GetOwningHouse() == pHouse) ++b;
        }
        for (int i = 0; i < UnitClass::Array.Count; ++i) {
            UnitClass* pu = UnitClass::Array.GetItem(i);
            if (pu && pu->GetOwningHouse() == pHouse) ++un;
        }
        for (int i = 0; i < TechnoClass::Array.Count; ++i) {
            TechnoClass* pt = TechnoClass::Array.GetItem(i);
            if (pt && pt->GetOwningHouse() == pHouse) ++tc;
        }
        s.buildings = b;
        s.units = un;
        s.technos = tc;
        s.censusOk = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.censusOk = 0;
    }
}

// Number of live HouseClass entries. POD-only.
int LoseProbe_HouseCount() {
    __try {
        return HouseClass::Array.Count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int LoseProbe_ReadAll(LoseProbeHouse* out, int cap) {
    int n = LoseProbe_HouseCount();
    if (n < 0)
        return -1;
    if (n > cap)
        n = cap;
    for (int i = 0; i < n; ++i) {
        HouseClass* pHouse = nullptr;
        __try {
            pHouse = HouseClass::Array.GetItem(i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            pHouse = nullptr;
        }
        if (!pHouse) {
            std::memset(&out[i], 0, sizeof(LoseProbeHouse));
            out[i].arrayIndex = i;
            out[i].flagsOk = 0;
            out[i].censusOk = 0;
            out[i].id[0] = '?';
            out[i].id[1] = '\0';
            continue;
        }
        std::memset(&out[i], 0, sizeof(LoseProbeHouse));
        LoseProbe_ReadFlags(out[i], pHouse);
        if (out[i].flagsOk)
            LoseProbe_ReadCensus(out[i], pHouse);
    }
    return n;
}

void LoseProbe_ReadConfig(LoseProbeConfig& c) {
    __try {
        c.houseCount = HouseClass::Array.Count;
        c.currentPlayerIndex =
            HouseClass::CurrentPlayer ? HouseClass::CurrentPlayer->ArrayIndex : -1;
        c.observerIndex =
            HouseClass::Observer ? HouseClass::Observer->ArrayIndex : -1;
        c.gameMode = static_cast<int>(SessionClass::Instance.GameMode);
        c.shortGame = SessionClass::Instance.Config.ShortGame ? 1 : 0;
        c.alliesAllowed = SessionClass::Instance.Config.AlliesAllowed ? 1 : 0;
        c.aiPlayers = SessionClass::Instance.Config.AIPlayers;
        for (int i = 0; i < 8; ++i)
            c.sessionAllies[i] = SessionClass::Instance.Config.AISlots.Allies[i];
        ScenarioClass* sc = ScenarioClass::Instance;
        if (sc) {
            c.fixedAlliance = sc->SpecialFlags.FixedAlliance ? 1 : 0;
            c.ctfMode = sc->SpecialFlags.CTFMode ? 1 : 0;
            c.scenarioInert = sc->SpecialFlags.Inert ? 1 : 0;
            c.multiplayerOnly = sc->MultiplayerOnly ? 1 : 0;
            c.scenarioInstanceOk = 1;
        } else {
            c.scenarioInstanceOk = 0;
        }
        c.ok = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        c.ok = 0;
    }
}

// House ids in array order, so the allies bitmask can be resolved to names.
// Read with the same POD-only rule; caller formats.
int LoseProbe_ReadIds(char ids[kProbeMaxHouses][40], int cap) {
    int n = LoseProbe_HouseCount();
    if (n < 0)
        return -1;
    if (n > cap)
        n = cap;
    for (int i = 0; i < n; ++i) {
        ids[i][0] = '?';
        ids[i][1] = '\0';
        __try {
            HouseClass* pHouse = HouseClass::Array.GetItem(i);
            if (pHouse && pHouse->Type) {
                const char* pid = pHouse->Type->get_ID();
                if (pid) {
                    for (int k = 0; k < 39 && pid[k]; ++k)
                        ids[i][k] = pid[k];
                    ids[i][39] = '\0';
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return n;
}

// ---- formatters (outside __try: may use fmt / std freely) ------------------

void LoseProbe_LogConfig(const char* phase) {
    LoseProbeConfig c;
    std::memset(&c, 0, sizeof(c));
    c.currentPlayerIndex = -1;
    c.observerIndex = -1;
    c.scenarioInstanceOk = 0;
    c.ok = 0;
    LoseProbe_ReadConfig(c);
    if (!c.ok) {
        LUA_LOG_WARN("[LOSE_PROBE] {} CONFIG read FAILED (SEH)", phase);
        return;
    }
    LUA_LOG_INFO(
        "[LOSE_PROBE] {} CONFIG frame={} houses={} gamemode={} (5=Skirmish) "
        "shortgame={} alliesallowed={} aiplayers={} "
        "scen(fixedalliance={} ctf={} inert={} mponly={} scen_ok={}) "
        "curplayer_idx={} observer_idx={} "
        "lobbyAISlotsAllies=[{} {} {} {} {} {} {} {}]",
        phase, Unsorted::CurrentFrame, c.houseCount, c.gameMode, c.shortGame,
        c.alliesAllowed, c.aiPlayers, c.fixedAlliance, c.ctfMode,
        c.scenarioInert, c.multiplayerOnly, c.scenarioInstanceOk,
        c.currentPlayerIndex, c.observerIndex,
        c.sessionAllies[0], c.sessionAllies[1], c.sessionAllies[2],
        c.sessionAllies[3], c.sessionAllies[4], c.sessionAllies[5],
        c.sessionAllies[6], c.sessionAllies[7]);
}

// Process-level activity flag. Game::IsActive is a GLOBAL (0xA8E9A0, declared
// in YRpp Unsorted.h inside `class Game`), not a per-house field. Its exact
// semantics are NOT established by the repository — treat it as a raw
// observation, not as a match-running indicator.
// POD-only (it must live outside LoseProbe_LogAll, which holds std::string and
// therefore cannot contain __try — C2712).
int LoseProbe_ReadGlobalActive() {
    __try {
        return Game::IsActive ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void LoseProbe_LogAll(const char* phase, bool withConfig, bool withAllies) {
    LoseProbeHouse states[kProbeMaxHouses];
    int n = LoseProbe_ReadAll(states, kProbeMaxHouses);
    if (n < 0) {
        LUA_LOG_WARN("[LOSE_PROBE] {} HouseClass::Array read FAILED (SEH)", phase);
        return;
    }
    if (withConfig)
        LoseProbe_LogConfig(phase);
    const int gActive = LoseProbe_ReadGlobalActive();
    for (int i = 0; i < n; ++i) {
        const LoseProbeHouse& s = states[i];
        LUA_LOG_INFO(
            "[LOSE_PROBE] {} HOUSE_STATE frame={} idx={} id={} loser={} "
            "defeated={} gameover={} winner={} borrowed={} production={} "
            "human={} incontrol={} observer={} neutral={} "
            "allies=0x{:08X} bld={} un={} tc={} flags_ok={} census_ok={} "
            "g_Active={}",
            phase, Unsorted::CurrentFrame, s.arrayIndex, s.id, s.isLoser,
            s.defeated, s.isGameOver, s.isWinner, s.borrowedTime, s.production,
            s.isHuman, s.inPlayerControl, s.isObserver, s.isNeutral, s.allies,
            s.buildings, s.units, s.technos, s.flagsOk, s.censusOk, gActive);
    }
    if (withAllies) {
        char ids[kProbeMaxHouses][40];
        int m = LoseProbe_ReadIds(ids, kProbeMaxHouses);
        for (int i = 0; i < m && i < n; ++i) {
            std::string mutual;
            for (int j = 0; j < m; ++j) {
                if (i == j)
                    continue;
                bool a = (states[i].allies & (1u << j)) != 0u;
                bool b = (states[j].allies & (1u << i)) != 0u;
                if (a || b) {
                    mutual += (a && b) ? "MUT:" : "ONE:";
                    mutual += ids[j];
                    mutual += " ";
                }
            }
            LUA_LOG_INFO(
                "[LOSE_PROBE] {} HOUSE_ALLIES frame={} id={} idx={} "
                "raw=0x{:08X} resolved=[{}]",
                phase, Unsorted::CurrentFrame, ids[i], i, states[i].allies,
                mutual.empty() ? "none" : mutual.c_str());
        }
    }
}

// ---- deferred AFTER_1 / AFTER_30 / AFTER_60 / AFTER_90 sequence -------------
// Driven from Hooked_MainLoop (game thread, every main-loop iteration, BEFORE
// the IsInGameMatch() gate and before the logic-frame dedup). No engine call,
// no ordering, no allocation beyond the log line.
//
// Why not OnGameFrame: OnGameFrame returns early unless IsInGameMatch() and is
// reached only when Unsorted::CurrentFrame changed. Both stop being true the
// moment the match ends, which is exactly the window this probe exists to
// observe. Driving from the MainLoop detour keeps observing for as long as the
// process lives, which is what distinguishes "the engine transitioned" from
// "our timer stopped working".

const int kProbeOffsets[7] = { 1, 30, 60, 90, 120, 180, 300 };
const char* const kProbePhases[7] = { "AFTER_1", "AFTER_30", "AFTER_60",
                                      "AFTER_90", "AFTER_120", "AFTER_180",
                                      "AFTER_300" };
constexpr int kProbePointCount = 7;

// Heartbeat cadence, in main-loop invocations, emitted only AFTER Arm so the
// pre-surrender part of the match produces no per-frame log spam. Counters are
// maintained in memory from process start regardless.
constexpr unsigned int kProbeHeartbeatEvery = 60;

// How many consecutive heartbeats PRE must observe an unchanged postDelta
// before declaring the POST side stalled (i.e. g_originalMainLoop stopped
// returning while the detour is still being entered).
constexpr int kPostStallConfirmations = 2;

int           s_probeArmed = 0;
int           s_probeBase = 0;
unsigned int  s_probePreCount = 0;   // incremented at Hooked_MainLoop entry,
                                     // BEFORE g_originalMainLoop()
unsigned int  s_probePostCount = 0;  // incremented AFTER g_originalMainLoop()
                                     // returned
unsigned int  s_probeBasePre = 0;
unsigned int  s_probeBasePost = 0;
bool          s_pointDone[kProbePointCount] = {};
bool          s_pointPreSeen[kProbePointCount] = {};
bool          s_pointPreFired[kProbePointCount] = {};
int           s_postStallSeen = 0;
bool          s_postStallLogged = false;

int LoseProbe_CountDone() {
    int n = 0;
    for (int i = 0; i < kProbePointCount; ++i) {
        if (s_pointDone[i] || s_pointPreFired[i])
            ++n;
    }
    return n;
}

// Index of the first not-yet-observed probe point whose threshold has been
// reached, or -1. Two independent bases, deliberately NOT CurrentFrame-only:
//   * frameDelta  - engine logic frames
//   * countDelta  - raw main-loop invocations from the requested side
int LoseProbe_NextDue(int frameDelta, unsigned int countDelta) {
    for (int i = 0; i < kProbePointCount; ++i) {
        if (s_pointDone[i])
            continue;
        const unsigned int off = static_cast<unsigned int>(kProbeOffsets[i]);
        if (frameDelta >= 0 && static_cast<unsigned int>(frameDelta) >= off)
            return i;
        if (countDelta >= off)
            return i;
    }
    return -1;
}

// Emitted once when PRE keeps advancing but POST has clearly stopped tracking
// it. This is the control-flow evidence that separates "detour no longer
// entered" from "detour entered but g_originalMainLoop never returned".
void LoseProbe_NotePostStall(unsigned int preDelta, unsigned int postDelta,
                             int frame, int frameDelta) {
    ++s_postStallSeen;
    if (s_postStallSeen < kPostStallConfirmations || s_postStallLogged)
        return;
    s_postStallLogged = true;
    LUA_LOG_WARN(
        "[LOSE_PROBE] POST_STALLED pre_count={} post_count={} pre_delta={} "
        "post_delta={} frame={} frame_delta={} points={}/{} | "
        "CONTROL_FLOW_BOUNDARY=INSIDE_ORIGINAL_MAINLOOP "
        "(detour still entered; control did not return from "
        "g_originalMainLoop). Not a game-over verdict.",
        s_probePreCount, s_probePostCount, preDelta, postDelta, frame,
        frameDelta, LoseProbe_CountDone(), kProbePointCount);
}

void LoseProbe_Arm() {
    s_probeArmed = 1;
    s_probeBase = static_cast<int>(Unsorted::CurrentFrame);
    s_probeBasePre = s_probePreCount;
    s_probeBasePost = s_probePostCount;
    for (int i = 0; i < kProbePointCount; ++i) {
        s_pointDone[i] = false;
        s_pointPreSeen[i] = false;
        s_pointPreFired[i] = false;
    }
    s_postStallSeen = 0;
    s_postStallLogged = false;
    LUA_LOG_INFO(
        "[LOSE_PROBE] ARMED base_frame={} base_pre_count={} base_post_count={} "
        "pending=[+1 +30 +60 +90 +120 +180 +300]",
        s_probeBase, s_probeBasePre, s_probeBasePost);
}

void LoseProbe_TryClose(int frame, int frameDelta, const char* side) {
    if (LoseProbe_CountDone() < kProbePointCount)
        return;
    LUA_LOG_INFO(
        "[LOSE_PROBE] SEQUENCE_COMPLETE side={} frame={} frame_delta={} "
        "pre_count={} post_count={} pre_delta={} post_delta={}. All {} points "
        "observed (post={} pre={}).",
        side, frame, frameDelta, s_probePreCount, s_probePostCount,
        s_probePreCount - s_probeBasePre, s_probePostCount - s_probeBasePost,
        kProbePointCount, [&]{
            int n = 0;
            for (int i = 0; i < kProbePointCount; ++i) if (s_pointDone[i]) ++n;
            return n;
        }(), LoseProbe_CountDone());
    s_probeArmed = 0;
}

// ---- PRE side: runs at detour entry, BEFORE g_originalMainLoop() -------------
// This side exists because the POST side is structurally incapable of
// observing anything that happens after control fails to return from the
// original call. PRE covers exactly the probe points POST never reached.
void LoseProbe_TickPreImpl() {
    ++s_probePreCount;
    if (!s_probeArmed)
        return;

    const int frame = static_cast<int>(Unsorted::CurrentFrame);
    const int frameDelta = (frame >= s_probeBase) ? (frame - s_probeBase) : -1;
    const unsigned int preDelta = s_probePreCount - s_probeBasePre;
    const unsigned int postDelta = s_probePostCount - s_probeBasePost;

    if (preDelta != 0 && (preDelta % kProbeHeartbeatEvery) == 0) {
        LUA_LOG_INFO(
            "[LOSE_PROBE] HEARTBEAT side=PRE pre_count={} post_count={} "
            "pre_delta={} post_delta={} frame={} frame_delta={}",
            s_probePreCount, s_probePostCount, preDelta, postDelta, frame,
            frameDelta);
        // POST falling behind PRE by a growing margin means the original call
        // is not returning for some iterations.
        if (preDelta > postDelta + 1)
            LoseProbe_NotePostStall(preDelta, postDelta, frame, frameDelta);
    }

    const int idx = LoseProbe_NextDue(frameDelta, preDelta);
    if (idx < 0)
        return;
    // First time this side sees the point as due: let POST take it, so a
    // healthy loop produces exactly one dump per point, not two.
    if (!s_pointPreSeen[idx]) {
        s_pointPreSeen[idx] = true;
        return;
    }
    if (s_pointDone[idx] || s_pointPreFired[idx])
        return;
    // POST did not capture it on the previous iteration -> cover it here.
    s_pointPreFired[idx] = true;
    LUA_LOG_INFO(
        "[LOSE_PROBE] {} point {}/{} side=PRE via=pre_invocations frame={} "
        "frame_delta={} pre_delta={} pre_count={} post_count={} "
        "(POST did not capture this point)",
        kProbePhases[idx], idx + 1, kProbePointCount, frame, frameDelta,
        preDelta, s_probePreCount, s_probePostCount);
    LoseProbe_LogAll(kProbePhases[idx], /*withConfig=*/false,
                     /*withAllies=*/false);
    LoseProbe_TryClose(frame, frameDelta, "PRE");
}

// ---- POST side: runs AFTER g_originalMainLoop() returned ----------------------
void LoseProbe_TickImpl() {
    if (!s_probeArmed)
        return;

    const int frame = static_cast<int>(Unsorted::CurrentFrame);
    const unsigned int postDelta = s_probePostCount - s_probeBasePost;

    if (frame < s_probeBase) {
        LUA_LOG_WARN(
            "[LOSE_PROBE] FRAME_RESTART frame={} < base={} pre_count={} "
            "post_count={} pre_delta={} post_delta={}: disarming with {}/{} "
            "points observed",
            frame, s_probeBase, s_probePreCount, s_probePostCount,
            s_probePreCount - s_probeBasePre, postDelta,
            LoseProbe_CountDone(), kProbePointCount);
        s_probeArmed = 0;
        return;
    }

    const int frameDelta = frame - s_probeBase;
    if (postDelta != 0 && (postDelta % kProbeHeartbeatEvery) == 0) {
        LUA_LOG_INFO(
            "[LOSE_PROBE] HEARTBEAT side=POST pre_count={} post_count={} "
            "pre_delta={} post_delta={} frame={} frame_delta={}",
            s_probePreCount, s_probePostCount,
            s_probePreCount - s_probeBasePre, postDelta, frame, frameDelta);
    }

    const int idx = LoseProbe_NextDue(frameDelta, postDelta);
    if (idx < 0)
        return;
    s_pointDone[idx] = true;
    LUA_LOG_INFO(
        "[LOSE_PROBE] {} point {}/{} side=POST via=logic_frame frame={} "
        "frame_delta={} post_delta={} pre_count={} post_count={}",
        kProbePhases[idx], idx + 1, kProbePointCount, frame, frameDelta,
        postDelta, s_probePreCount, s_probePostCount);
    LoseProbe_LogAll(kProbePhases[idx], /*withConfig=*/false,
                     /*withAllies=*/false);
    LoseProbe_TryClose(frame, frameDelta, "POST");
}

} // anonymous namespace

// Exported call sites (src/lua_engine.cpp, Hooked_MainLoop).
void LoseProbe_NoteMainLoop() {
    ++s_probePostCount;
}

void LoseProbe_Tick() {
    LoseProbe_TickImpl();
}

void LoseProbe_TickPre() {
    LoseProbe_TickPreImpl();
}

// === LOSE-PROBE-REMOVE-ME END ==============================================

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

// INTERNAL SmartAI surrender bridge (NOT public API — deliberately absent
// from API.md; SmartAI is the only consumer). Engine.__SmartAILose(house)
// validates the house and calls HouseClass::Lose(false) exactly as the
// Phase-2 spike verified (IsLoser=1, BorrowedTime countdown, assets kept).
// No other candidate exists here by construction: Lose(true), FlagToDie,
// AcceptDefeat and Win are unreachable through this entry point.
// Idempotence: refuses (false) when the house already IsLoser — the flag
// itself is the guard, so no stale per-match sets are needed and a fresh
// match re-latching legitimately calls again. SEH-wrapped, POD-only
// locals (C2712). Returns boolean, never throws into Lua.
int House_SmartAILose(lua_State* L) {
    void* ud = luaL_testudata(L, 1, kMetaName);
    if (!ud) {
        lua_pushboolean(L, 0);
        return 1;
    }
    HouseClass* pHouse = *static_cast<HouseClass**>(ud);
    if (!pHouse) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool member = false;
    __try {
        for (int i = 0; i < HouseClass::Array.Count; ++i) {
            if (HouseClass::Array.GetItem(i) == pHouse) {
                member = true;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!member) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool already = false;
    __try {
        already = pHouse->IsLoser;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (already) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // LOSE-PROBE-REMOVE-ME — temporary diagnostic around the single
    // HouseClass::Lose(false) call. Read-only snapshots of EVERY house in
    // HouseClass::Array plus the session/scenario alliance configuration.
    // BEFORE is taken immediately before the call, AFTER_0 immediately after
    // it returns, and the +1/+30/+60/+90 follow-ups are driven from
    // OnGameFrame. Nothing here changes behaviour.
    //
    // The target index is captured BEFORE the call: after Lose(false) returns
    // the house may be mid-teardown, so the exception handler must not touch
    // pHouse at all.
    int loseTargetIdx = -1;
    __try {
        loseTargetIdx = pHouse->ArrayIndex;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    LoseProbe_LogAll("BEFORE", /*withConfig=*/true, /*withAllies=*/true);
    LUA_FLUSH_LOG();
    __try {
        pHouse->Lose(false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LUA_LOG_WARN("[LOSE_PROBE] Lose(false) raised SEH on house idx={}",
                     loseTargetIdx);
        lua_pushboolean(L, 0);
        return 1;
    }
    LoseProbe_LogAll("AFTER_0", /*withConfig=*/false, /*withAllies=*/false);
    LUA_FLUSH_LOG();
    LoseProbe_Arm();
    // LOSE-PROBE-REMOVE-ME END
    lua_pushboolean(L, 1);
    return 1;
}

} // anonymous namespace

// Кэш userdata для домов: HouseClass* -> реестровая Lua-ссылка.
// Дома живут весь матч, поэтому один и тот же HouseClass* надёжно возвращает
// ОДИН И ТОТ ЖЕ userdata — это даёт истинное `a == b` для GetPlayer()/GetOwner().
// Обнуляется в ResetSession (см. ClearHouseCache).
static std::unordered_map<HouseClass*, int> g_houseCache;

int PushHouse(lua_State* L, HouseClass* pHouse) {
    if (!pHouse)
        return 0;

    // Уже создан — вернуть тот же самый объект из реестра.
    auto it = g_houseCache.find(pHouse);
    if (it != g_houseCache.end()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
        return 1;
    }

    // Новый: создаём userdata и кладём в реестр, чтобы переиспользовать.
    NewHouse(L, pHouse);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);      // убирает userdata со стека в реестр
    g_houseCache.emplace(pHouse, ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);        // вернуть тот же объект
    return 1;
}

void ClearHouseCache(lua_State* L) {
    for (auto& kv : g_houseCache) {
        if (L)
            luaL_unref(L, LUA_REGISTRYINDEX, kv.second);
    }
    g_houseCache.clear();
}

namespace {

// House.GetPlayer() -> house | nil
int House_GetPlayer(lua_State* L) {
    HouseClass* pHouse = HouseClass::CurrentPlayer;
    if (!pHouse)
        return 0; // nil

    return PushHouse(L, pHouse);
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

    return PushHouse(L, pHouse);
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

// house:GetAIDifficulty() -> "easy" | "normal" | "hard"
// Read-only view of HouseClass::AIDifficulty. Engine ground truth is REVERSED
// (GeneralDefinitions.h: Hard == 0, Normal == 1, Easy == 2); the binding
// normalizes it so Lua never sees the raw enum. SEH-wrapped: the house
// userdata is validated and array-membership is not re-checked here (PushHouse
// userdata is only handed out for live houses), but the field read is guarded
// like every other engine dereference. Returns nil when the read fails so
// callers can fall back instead of treating a failure as a difficulty.
int House_GetAIDifficulty(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    int ok = 0;
    unsigned int raw = 0;
    __try {
        raw = pHouse->GetAIDifficultyIndex();
        ok = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    if (!ok) {
        LUA_LOG_WARN("[House] GetAIDifficulty: read failed (SEH)");
        return 0; // nil -> caller falls back
    }
    // YRpp AIDifficulty: Hard == 0, Normal == 1, Easy == 2 (intentionally
    // reversed upstream). Anything unexpected also reads as "normal".
    const char* level = "normal";
    if (raw == 0)       level = "hard";
    else if (raw == 2)  level = "easy";
    lua_pushstring(L, level);
    return 1;
}

// house:IsHuman() -> bool
int House_IsHuman(lua_State* L) {
    HouseClass* pHouse = CheckHouse(L, 1);
    lua_pushboolean(L, pHouse->IsControlledByHuman() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// SEH-защищённые обёртки над движком. В этой ветке YRpp нет статической фабрики
// UnitClass::Create (классический API), поэтому спавн выполняется эквивалентно:
// GameCreate<UnitClass>(pType, pHouse) + Unlimbo(coord, dir) — так же, как это
// делает родной Create. Все обращения к движку обернуты в __try/__except.
// ---------------------------------------------------------------------------
static UnitTypeClass* FindUnitType(const char* typeId) {
    __try {
        return UnitTypeClass::Find(typeId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static UnitClass* CreateUnitAt(UnitTypeClass* pType, HouseClass* pHouse, int x, int y, int facing,
                               int* outActualX, int* outActualY) {
    __try {
        auto* pUnit = GameCreate<UnitClass>(pType, pHouse);
        if (!pUnit)
            return nullptr;
        CellStruct cell{ static_cast<short>(x), static_cast<short>(y) };
        CoordStruct coord = CellClass::Cell2Coord(cell);
        DirType dir = static_cast<DirType>(static_cast<unsigned char>(facing));
        if (!pUnit->Unlimbo(coord, dir))
            return nullptr;
        // Фактическая клетка, куда встал юнит после Unlimbo (движок мог сместить).
        if (outActualX && outActualY) {
            CellStruct actual = CellClass::Coord2Cell(pUnit->GetCoords());
            *outActualX = actual.X;
            *outActualY = actual.Y;
        }
        return pUnit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Клеточная сетка движка: индекс ячейки = (Y << 9) + X, а массив Cells имеет
// размер MaxCells = 0x40000 = 512 * 512. Значит валидная ячейка лежит в
// [0, 512) x [0, 512); за этими границами GetCellIndex алиасит ячейки/уходит в
// минус, поэтому GetCellAt вызывать нельзя. YRpp-форк не экспонирует ширину/
// высоту конкретной карты, поэтому берём сеточный предел движка.
static constexpr int kMapCellSide = 512;

// Внутри ли клеточной сетки карты (SEH-безопасно).
static bool IsInsideMap(int x, int y) {
    __try {
        if (x < 0 || y < 0)
            return false;
        if (x >= kMapCellSide || y >= kMapCellSide)
            return false;
        int idx = MapClass::GetCellIndex(CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        return idx >= 0 && idx < MapClass::MaxCells;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SpeedType юнита (для проверки проходимости клетки), SEH-безопасно.
static SpeedType GetUnitSpeedType(UnitTypeClass* pType) {
    __try {
        return pType->SpeedType;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return SpeedType::Foot;
    }
}

// Проходима ли клетка (x, y) для данного типа движения, SEH-безопасно.
// MapClass::Instance — это reference на игру (DEFINE_REFERENCE), поэтому обращение
// к ней при ещё не созданной карте даёт AV, который ловится __except -> false.
static bool IsCellClear(int x, int y, SpeedType st) {
    if (!IsInsideMap(x, y))   // вне сетки GetCellAt не вызываем
        return false;
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell)
            return false;
        return cell->IsClearToMove(st, false, false, -1, MovementZone::Normal, -1, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Поиск ближайшей свободной клетки по спирали: центр, затем кольца 1..3
// (периметр квадрата; внешнее кольцо r=3 даёт 24 клетки). Первая свободная
// возвращается в *outX/*outY, иначе false.
static bool FindSpawnCell(int cx, int cy, SpeedType st, int* outX, int* outY) {
    if (IsCellClear(cx, cy, st)) { *outX = cx; *outY = cy; return true; }
    for (int r = 1; r <= 3; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                if ((dx < 0 ? -dx : dx) != r && (dy < 0 ? -dy : dy) != r)
                    continue; // не на периметре кольца
                int nx = cx + dx;
                int ny = cy + dy;
                if (IsCellClear(nx, ny, st)) { *outX = nx; *outY = ny; return true; }
            }
        }
    }
    return false;
}

// Приказ "Охота" (Hunt) юниту, SEH-безопасно. Эквивалент Hunt() по ТЗ — через
// MissionClass::QueueMission(Mission::Hunt, true) (в YRpp-форке нет Hunt()).
static void OrderHunt(UnitClass* pUnit) {
    if (!pUnit)
        return;
    __try {
        pUnit->QueueMission(Mission::Hunt, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LUA_LOG_WARN("[House] SpawnUnit: Hunt() failed (SEH)");
    }
}

// house:IsAlliedWith(other_house) -> bool
int House_IsAlliedWith(lua_State* L) {
    HouseClass* pSelf = CheckHouse(L, 1);

    void* ud = luaL_testudata(L, 2, kMetaName);
    if (!ud)
        return luaL_argerror(L, 2, "expected a house object");

    auto* pOther = *static_cast<HouseClass**>(ud);
    if (!pSelf || !pOther) {
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, pSelf->IsAlliedWith(pOther) ? 1 : 0);
    return 1;
}

// house:SpawnUnit(typeId, count?, x, y, facing?, force?, action?) -> int
// Debug-инструмент для AI-модов: прямой спавн count юнитов типа typeId для
// этой дома-хозяина. Возвращает фактическое число успешно созданных юнитов.
//   facing - опционально, направление (DirType 0..255), default 0 (North).
//   force  - опционально, default false. true = спавн без проверок проходимости.
//            false = проверка проходимости клетки + BFS-поиск свободной клетки
//            по спирали в радиусе 3.
//   action - опционально, строка. "hunt" = сразу дать каждому созданному юниту
//            приказ Hunt().
int House_SpawnUnit(lua_State* L) {
    LuaAPI::CrashReporter::Note("House_SpawnUnit");
    HouseClass* pHouse = CheckHouse(L, 1);
    const char* typeId = luaL_checkstring(L, 2);
    int count = static_cast<int>(luaL_optinteger(L, 3, 1));
    int x = static_cast<int>(luaL_checkinteger(L, 4));
    int y = static_cast<int>(luaL_checkinteger(L, 5));
    int facing = static_cast<int>(luaL_optinteger(L, 6, 0));
    bool force = lua_toboolean(L, 7) != 0; // default false
    const char* action = luaL_optstring(L, 8, "");
    bool doHunt = (action && _stricmp(action, "hunt") == 0);

    if (count < 1)
        count = 1;

    // Найти тип юнита по ID.
    UnitTypeClass* pType = FindUnitType(typeId);
    if (!pType) {
        LUA_LOG_WARN("[House] SpawnUnit: unknown typeId '{}'", typeId);
        lua_pushinteger(L, 0);
        return 1;
    }

    SpeedType st = GetUnitSpeedType(pType);

    int created = 0;
    for (int i = 0; i < count; ++i) {
        // Границы карты: вне сетки сразу пропуск, GetCellAt не вызываем.
        if (!IsInsideMap(x, y)) {
            LUA_LOG_WARN("[House] SpawnUnit: requested ({},{}) outside map grid [0..{}), skipping unit {}/{}",
                         x, y, kMapCellSide, i + 1, count);
            continue;
        }

        int actualX = x;
        int actualY = y;
        UnitClass* pUnit = nullptr;

        if (force) {
            // "Спавнить любой ценой": сначала пробуем в запрошенной клетке, при
            // неудаче Unlimbo — спиральный поиск ближайшей свободной клетки.
            pUnit = CreateUnitAt(pType, pHouse, x, y, facing, &actualX, &actualY);
            if (!pUnit) {
                int fbX = x, fbY = y;
                if (FindSpawnCell(x, y, st, &fbX, &fbY)) {
                    pUnit = CreateUnitAt(pType, pHouse, fbX, fbY, facing, &actualX, &actualY);
                }
            }
        } else {
            // Проверка проходимости + BFS-поиск свободной клетки по спирали.
            int spX = x, spY = y;
            if (!FindSpawnCell(x, y, st, &spX, &spY)) {
                LUA_LOG_WARN("[House] SpawnUnit: no free cell within radius 3 for '{}' near ({},{}), skipping unit {}/{}",
                             typeId, x, y, i + 1, count);
                continue;
            }
            pUnit = CreateUnitAt(pType, pHouse, spX, spY, facing, &actualX, &actualY);
        }

        if (pUnit) {
            ++created;
            if (doHunt) {
                OrderHunt(pUnit);
                LUA_LOG_INFO("[House] SpawnUnit: hunt ordered for '{}' at actual ({},{})",
                             typeId, actualX, actualY);
            } else {
                LUA_LOG_INFO("[House] SpawnUnit: created '{}' at actual ({},{}) [requested ({},{}), force={}]",
                             typeId, actualX, actualY, x, y, force ? 1 : 0);
            }
        } else {
            LUA_LOG_WARN("[House] SpawnUnit: creation failed for '{}' near ({},{}) (iteration {}/{})",
                         typeId, x, y, i + 1, count);
        }
    }

    if (created > 0) {
        LUA_LOG_INFO("[House] SpawnUnit: total created {} '{}' (requested {}, force={})",
                     created, typeId, count, force ? 1 : 0);
    } else {
        LUA_LOG_WARN("[House] SpawnUnit: no unit created for '{}' at ({},{})", typeId, x, y);
    }

    lua_pushinteger(L, created);
    return 1;
}

const luaL_Reg kHouseMethods[] = {
    { "GetCredits",     House_GetCredits     },
    { "SetCredits",     House_SetCredits     },
    { "AddCredits",     House_AddCredits     },
    { "GetPowerOutput", House_GetPowerOutput },
    { "GetPowerDrain",  House_GetPowerDrain  },
    { "GetName",        House_GetName        },
    { "GetAIDifficulty", House_GetAIDifficulty },
    { "IsHuman",        House_IsHuman        },
    { "IsAlliedWith",   House_IsAlliedWith   },
    { "SpawnUnit",      House_SpawnUnit      },
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

    // INTERNAL surrender bridge on the Engine table (see House_SmartAILose).
    // The Engine table is created in CreateEngine before this runs.
    lua_getglobal(L, "Engine");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    lua_pushcfunction(L, House_SmartAILose);
    lua_setfield(L, -2, "__SmartAILose");
    lua_setglobal(L, "Engine");
}

} // namespace LuaAPI
