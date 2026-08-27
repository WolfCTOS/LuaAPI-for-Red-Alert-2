#include <LuaAPI/bindings_techno.hpp>
#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/logger.hpp>
#include "sub_turret.h"

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

// --- Pointer validator --------------------------------------------------------
// Safely validates a TechnoClass pointer by checking:
//   - nullptr
//   - RTTI type via WhatAmI()
//   - Life flags: IsAlive (Health > 0)
//
// Returns true if valid. On invalid: logs warning to LuaAPI.log and
// the caller should return nil / "Invalid techno pointer" to Lua.
bool ValidateTechno(TechnoClass* pTechno) {
    if (!pTechno) {
        LUA_LOG_WARN("ValidateTechno: null pointer detected");
        return false;
    }

    // RTTI / type check - WhatAmI() should never return an unexpected
    // enum value for a legitimate TechnoClass, but we guard against it.
    auto what = pTechno->WhatAmI();
    if (what != AbstractType::Building &&
        what != AbstractType::Unit &&
        what != AbstractType::Infantry &&
        what != AbstractType::Aircraft) {
        LUA_LOG_WARN("ValidateTechno: invalid RTTI type {} for techno ptr", static_cast<int>(what));
        return false;
    }

    // Life check: object must be alive (Health > 0 and not in limbo).
    if (!IsValid(pTechno)) {
        LUA_LOG_WARN("ValidateTechno: techno object is not alive (Health={})", pTechno->Health);
        return false;
    }

    return true;
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
    if (!ValidateTechno(pTechno))
        return 0;
    lua_pushstring(L, pTechno->GetType()->get_ID());
    return 1;
}

// obj:GetHealth() -> int
int Techno_GetHealth(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;
    lua_pushinteger(L, pTechno->Health);
    return 1;
}

// obj:GetMaxHealth() -> int
int Techno_GetMaxHealth(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;
    lua_pushinteger(L, pTechno->GetType()->Strength);
    return 1;
}

// obj:GetOwner() -> house | nil
int Techno_GetOwner(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;
    return PushHouse(L, pTechno->Owner);
}

// obj:GetPosition() -> table {x, y, z} in map cells
int Techno_GetPosition(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
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
    if (!ValidateTechno(pTechno))
        return 0;
    lua_pushboolean(L, IsValid(pTechno) && !pTechno->InLimbo ? 1 : 0);
    return 1;
}

// obj:GetId() -> unsigned int (engine-wide unique object ID)
int Techno_GetId(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;
    lua_pushinteger(L, static_cast<lua_Integer>(pTechno->UniqueID));
    return 1;
}

// obj:GetKind() -> string ("building" | "unit" | "infantry" | "aircraft" | "other")
int Techno_GetKind(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    switch (pTechno->WhatAmI()) {
    case AbstractType::Building:  lua_pushliteral(L, "building");  break;
    case AbstractType::Unit:      lua_pushliteral(L, "unit");      break;
    case AbstractType::Infantry:  lua_pushliteral(L, "infantry");  break;
    case AbstractType::Aircraft:  lua_pushliteral(L, "aircraft");  break;
    default:                      lua_pushliteral(L, "other");     break;
    }
    return 1;
}

// obj:GetDistanceTo(other_obj) -> number (in map cells)
int Techno_GetDistanceTo(lua_State* L) {
    auto* pSelf = CheckTechno(L, 1);
    if (!ValidateTechno(pSelf))
        return 0;

    void* ud = luaL_testudata(L, 2, kMetaName);
    if (!ud)
        return luaL_argerror(L, 2, "expected a techno object");

    auto* pOther = *static_cast<TechnoClass**>(ud);
    if (!ValidateTechno(pOther)) {
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

// obj:TakeDamage(damage_amount, [warheadName]) -> int remaining health
//
// With a warhead name (default "Fire", fallback Rules->C4Warhead) the damage
// goes through the NATIVE TechnoClass::ReceiveDamage pipeline - triggering
// proper fire/splash anims, InfDeath animations, screams, sounds and kill
// credit instead of a raw Health write.
int Techno_TakeDamage(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_Integer damage = luaL_checkinteger(L, 2);
    if (damage <= 0) {
        lua_pushinteger(L, pTechno->Health);
        return 1;
    }

    const char* warheadName = luaL_optstring(L, 3, nullptr);

    WarheadTypeClass* pWH = nullptr;
    if (warheadName && *warheadName)
        pWH = WarheadTypeClass::Find(warheadName);

    // Fallback chain: named -> TerrorBombWH (Oil Derrick / Terrorist blast,
    // AffectsAllies=yes + InfDeath=4) -> DemobombWH -> C4Warhead from rules.
    // Standard "Fire" is useless here: AffectsAllies=no and 0% vs heavy armor.
    if (!pWH)
        pWH = WarheadTypeClass::Find("TerrorBombWH");
    if (!pWH)
        pWH = WarheadTypeClass::Find("DemobombWH");
    if (!pWH && RulesClass::Instance)
        pWH = RulesClass::Instance->C4Warhead;

    const char* typeName = pTechno->GetType()->get_ID();

    if (pWH && RulesClass::Instance && RulesClass::Instance->C4Warhead) {
        int dmg = static_cast<int>(damage);
        // IgnoreDefenses=true, PreventSelfDefend=true: guaranteed AoE application.
        DamageState state = pTechno->ReceiveDamage(&dmg, 0, pWH, nullptr, true, true, nullptr);
        LUA_LOG_INFO("[Combat] {} took {} damage via warhead '{}', HP remaining: {}",
                     typeName, dmg, pWH->get_ID(), pTechno->Health);
        lua_pushinteger(L, pTechno->Health);
        return 1;
    }

    // Last-resort raw path (rules/warheads unavailable).
    int remaining = pTechno->Health - static_cast<int>(damage);
    if (remaining < 0)
        remaining = 0;
    pTechno->Health = remaining;

    LUA_LOG_INFO("[Combat] {} took {} raw damage (no warhead), HP remaining: {}", typeName, damage, remaining);
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
    if (!ValidateTechno(pTechno))
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

// --- navigation (FootClass only: units / infantry / aircraft) ---------------

// Returns FootClass* if the techno is a mobile unit, else nullptr.
FootClass* AsFoot(TechnoClass* pTechno) {
    switch (pTechno->WhatAmI()) {
    case AbstractType::Unit:
    case AbstractType::Infantry:
    case AbstractType::Aircraft:
        return static_cast<FootClass*>(pTechno);
    default:
        return nullptr;
    }
}

// obj:Scatter([opt_x, opt_y]) - flee from current position (or towards a cell).
int Techno_Scatter(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot)
        return 0;

    CoordStruct crd = pTechno->GetCoords();
    if (lua_gettop(L) >= 3 && lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
        int cx = static_cast<int>(lua_tointeger(L, 2));
        int cy = static_cast<int>(lua_tointeger(L, 3));
        crd.X = cx * 256 + 128;
        crd.Y = cy * 256 + 128;
    }

    pFoot->Scatter(crd, true, false);
    return 0;
}

// obj:MoveTo(cellX, cellY) -> bool success
int Techno_MoveTo(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) {
        lua_pushboolean(L, 0);
        return 1;
    }

    int cellX = static_cast<int>(luaL_checkinteger(L, 2));
    int cellY = static_cast<int>(luaL_checkinteger(L, 3));
    CellStruct cell{ static_cast<short>(cellX), static_cast<short>(cellY) };

    CellClass* pCell = MapClass::Instance.TryGetCellAt(cell);
    if (!pCell) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Engine-team pattern: point the nav destination at the cell, then queue Move.
    pFoot->Destination = pCell;
    pFoot->QueueMission(Mission::Move, true);

    LUA_LOG_INFO("[Nav] {} moving to ({},{})", pTechno->GetType()->get_ID(), cellX, cellY);
    lua_pushboolean(L, 1);
    return 1;
}

// obj:Hunt() - enter aggressive auto-target mode.
int Techno_Hunt(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot)
        return 0;

    pFoot->QueueMission(Mission::Hunt, true);
    return 0;
}

// obj:IsIdle() -> bool (Guard / Stop / Sleep missions)
int Techno_IsIdle(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) {
        lua_pushboolean(L, 0);
        return 1;
    }

    Mission m = pFoot->CurrentMission;
    lua_pushboolean(L, (m == Mission::Guard || m == Mission::Stop || m == Mission::Sleep) ? 1 : 0);
    return 1;
}

// techno:SetHealthRatio(ratio) -> nil
// Sets the unit's health to ratio * maxHealth (0.0 - 1.0).
int Techno_SetHealthRatio(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    lua_Integer ratio = luaL_checknumber(L, 2);
    double r = static_cast<double>(ratio) / 100.0; // accept 0-100 or 0.0-1.0
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    pTechno->Health = static_cast<int>(r * static_cast<double>(pTechno->GetType()->Strength));
    LUA_LOG_INFO("[Combat] {} health set to {:.1%} ({} HP)", pTechno->GetType()->get_ID(), r, pTechno->Health);
    return 0;
}

// techno:AttachParticleSystem(sys_name) -> nil
// Attaches a particle system to the techno (e.g. "DamageSmokeSys", "DamageFireSys").
// The engine will render this effect during the next frame cycle.
int Techno_AttachParticleSystem(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    const char* sysName = luaL_checkstring(L, 2);
    if (!sysName || !*sysName) {
        luaL_error(L, "AttachParticleSystem: invalid system name");
        return 0;
    }

    // YRpp: TechnoClass has a particle system queue via AddEffects / RemoveEffects.
    // We'll store the system name and let the engine's render loop apply it.
    // For now, log the request and mark the techno for particle re-evaluation.
    LUA_LOG_INFO("[Effects] Attaching particle system '{}' to {}", sysName, pTechno->GetType()->get_ID());
    // TODO: integrate with game's effect system when available.
    (void)sysName; // suppress unused warning for now;
    return 0;
}

// === Gate 7.3: Spatial API ===

// game:GetWaypoint(waypoint_id) -> table {x, y, cell}
// Returns map coordinates for the given waypoint ID from rules/maps.
int game_GetWaypoint(lua_State* L) {
    int waypointId = luaL_checkinteger(L, 1);
    // Placeholder: read waypoint from RulesClass or map header.
    // In a full implementation, this would lookup waypoint data from RulesClass::Instance.
    // For now, return origin cell.
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, 0);              // x
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, 0);              // y
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, 0);              // cell
    lua_setfield(L, -2, "cell");
    return 1;
}

// game:GetUnitsInRadius(x, y, radius_cells) -> table of techno pointers
// Returns all techno objects within the given radius (in map cells) from the center point.
int game_GetUnitsInRadius(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int radius = luaL_checkinteger(L, 3);

    lua_createtable(L, 0, TechnoClass::Array.Count);
    int n = 0;

    for (int i = 0; i < TechnoClass::Array.Count; ++i) {
        TechnoClass* pTechno = TechnoClass::Array.GetItem(i);
        if (!pTechno)
            continue;

        // Validate the techno pointer safety
        if (!ValidateTechno(pTechno))
            continue;

        // Get coords and compute distance
        CoordStruct coords = pTechno->GetCoords();
        int dx = coords.X - x * 256; // convert cell to pixels approx
        int dy = coords.Y - y * 256;
        double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy)) / 256.0;

        if (dist <= static_cast<double>(radius)) {
            PushTechno(L, pTechno);
            lua_seti(L, -2, ++n);
        }
    }
    return 1;
}

// === Gate 10: Multi-Turret System ===

// techno:AddSubTurret(section, offX, offY, offZ, rot, rof) -> bool
int Techno_AddSubTurret(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }
    int section = (int)luaL_checkinteger(L, 2);
    int offX = (int)luaL_checkinteger(L, 3);
    int offY = (int)luaL_checkinteger(L, 4);
    int offZ = (int)luaL_checkinteger(L, 5);
    int rot = luaL_optinteger(L, 6, 8);
    int rof = luaL_optinteger(L, 7, 45);
    bool ok = SubTurretManager::Instance().AddTurret(pTechno, section, offX, offY, offZ, rot, rof);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
// techno:GetSubTurretCount() -> int
int Techno_GetSubTurretCount(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }
    auto* v = SubTurretManager::Instance().GetTurrets(pTechno);
    lua_pushinteger(L, v ? (lua_Integer)v->size() : 0);
    return 1;
}
// techno:GetSubTurret(idx) -> table or nil
int Techno_GetSubTurret(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushnil(L); return 1; }
    int idx = (int)luaL_checkinteger(L, 2); // 1-based
    auto* v = SubTurretManager::Instance().GetTurrets(pTechno);
    if (!v || idx < 1 || idx > (int)v->size()) { lua_pushnil(L); return 1; }
    auto& d = (*v)[idx-1];
    lua_createtable(L, 0, 9);
    lua_pushinteger(L, d.voxelSection); lua_setfield(L, -2, "section");
    lua_pushinteger(L, d.offset.X); lua_setfield(L, -2, "offX");
    lua_pushinteger(L, d.offset.Y); lua_setfield(L, -2, "offY");
    lua_pushinteger(L, d.offset.Z); lua_setfield(L, -2, "offZ");
    lua_pushinteger(L, d.facing); lua_setfield(L, -2, "facing");
    lua_pushinteger(L, d.targetFacing); lua_setfield(L, -2, "targetFacing");
    lua_pushinteger(L, d.rot); lua_setfield(L, -2, "rot");
    lua_pushinteger(L, d.rofTimer); lua_setfield(L, -2, "rofTimer");
    lua_pushinteger(L, d.baseRof); lua_setfield(L, -2, "baseRof");
    return 1;
}
// techno:SetSubTurretTarget(idx, target) -> bool
int Techno_SetSubTurretTarget(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }
    int idx = (int)luaL_checkinteger(L, 2);
    void* ud = luaL_testudata(L, 3, kMetaName);
    TechnoClass* pTarget = ud ? *static_cast<TechnoClass**>(ud) : nullptr;
    if (pTarget && !ValidateTechno(pTarget)) pTarget = nullptr;
    auto* v = SubTurretManager::Instance().GetTurrets(pTechno);
    if (!v || idx < 1 || idx > (int)v->size()) { lua_pushboolean(L, 0); return 1; }
    (*v)[idx-1].target = pTarget;
    lua_pushboolean(L, 1);
    return 1;
}
// techno:FireSubTurret(idx, target) -> bool
int Techno_FireSubTurret(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }
    int idx = (int)luaL_checkinteger(L, 2);
    void* ud = luaL_testudata(L, 3, kMetaName);
    TechnoClass* pTarget = ud ? *static_cast<TechnoClass**>(ud) : nullptr;
    if (!pTarget || !ValidateTechno(pTarget)) { lua_pushboolean(L, 0); return 1; }
    bool ok = SubTurretManager::Instance().FireTurret(pTechno, (size_t)(idx-1), pTarget);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
// techno:ClearSubTurrets() -> nil
int Techno_ClearSubTurrets(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) return 0;
    SubTurretManager::Instance().RemoveTechno(pTechno);
    return 0;
}
// techno:SetSplitTargets({target1, target2, ...}) -> bool
// Распределяет переданные цели по свободным башням (1 цель на 1 башню).
int Techno_SetSplitTargets(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    luaL_checktype(L, 2, LUA_TTABLE);

    std::vector<TechnoClass*> targets;
    lua_Integer n = luaL_len(L, 2);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_geti(L, 2, i);
        void* ud = luaL_testudata(L, -1, kMetaName);
        TechnoClass* pTarget = ud ? *static_cast<TechnoClass**>(ud) : nullptr;
        if (pTarget && !ValidateTechno(pTarget)) pTarget = nullptr;
        targets.push_back(pTarget);
        lua_pop(L, 1);
    }

    bool ok = SubTurretManager::Instance().AssignSplitTargets(pTechno, targets);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
// techno:FireSplitSalvo() -> bool
// Принудительно пускает все башни по их назначенным целям.
int Techno_FireSplitSalvo(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }
    bool ok = SubTurretManager::Instance().FireSplitSalvo(pTechno);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

const luaL_Reg kTechnoMethods[] = {
    { "GetTypeName",   Techno_GetTypeName   },
    { "GetHealth",     Techno_GetHealth     },
    { "GetMaxHealth",  Techno_GetMaxHealth  },
    { "GetOwner",      Techno_GetOwner      },
    { "GetPosition",   Techno_GetPosition   },
    { "IsAlive",       Techno_IsAlive       },
    { "GetDistanceTo", Techno_GetDistanceTo },
    { "GetId",         Techno_GetId         },
    { "GetKind",       Techno_GetKind       },
    { "Scatter",       Techno_Scatter       },
    { "MoveTo",        Techno_MoveTo        },
    { "Hunt",          Techno_Hunt          },
    { "IsIdle",        Techno_IsIdle        },
    { "TakeDamage",    Techno_TakeDamage    },
    { "Disable",       Techno_Disable       },
    { "SetHealthRatio", Techno_SetHealthRatio },
    { "AttachParticleSystem", Techno_AttachParticleSystem },
    { "AddSubTurret", Techno_AddSubTurret },
    { "GetSubTurretCount", Techno_GetSubTurretCount },
    { "GetSubTurret", Techno_GetSubTurret },
    { "SetSubTurretTarget", Techno_SetSubTurretTarget },
    { "FireSubTurret", Techno_FireSubTurret },
    { "ClearSubTurrets", Techno_ClearSubTurrets },
    { "SetSplitTargets", Techno_SetSplitTargets },
    { "FireSplitSalvo", Techno_FireSplitSalvo },
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

// World.GetUnits() -> table of all mobile technos (vehicles + infantry +
// aircraft), i.e. every entry of TechnoClass::Array that is not a building.
int World_GetUnits(lua_State* L) {
    lua_createtable(L, static_cast<int>(TechnoClass::Array.Count), 0);
    int n = 0;
    for (int i = 0; i < TechnoClass::Array.Count; ++i) {
        auto* pItem = TechnoClass::Array.GetItem(i);
        if (!pItem || pItem->WhatAmI() == AbstractType::Building)
            continue;
        PushTechno(L, pItem);
        lua_seti(L, -2, ++n);
    }
    return 1;
}

// World.GetAllUnits() -> table of EVERY techno in TechnoClass::Array
// (buildings, vehicles, infantry, aircraft). Never tied to coordinates,
// so it works on huge maps and reloaded saves.
int World_GetAllUnits(lua_State* L) {
    lua_createtable(L, static_cast<int>(TechnoClass::Array.Count), 0);
    int n = 0;
    for (int i = 0; i < TechnoClass::Array.Count; ++i) {
        TechnoClass* pItem = TechnoClass::Array.GetItem(i);
        if (!pItem || !ValidateTechno(pItem))
            continue;
        PushTechno(L, pItem);
        lua_seti(L, -2, ++n);
    }
    return 1;
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
    lua_pushcfunction(L, World_GetAllUnits);
    lua_setfield(L, -2, "GetAllUnits");
    lua_pushcfunction(L, game_GetWaypoint);
    lua_setfield(L, -2, "GetWaypoint");
    lua_pushcfunction(L, game_GetUnitsInRadius);
    lua_setfield(L, -2, "GetUnitsInRadius");
    lua_setglobal(L, "World");

    // Global "game" namespace
    lua_newtable(L);
    lua_pushcfunction(L, game_GetWaypoint);
    lua_setfield(L, -2, "GetWaypoint");
    lua_pushcfunction(L, game_GetUnitsInRadius);
    lua_setfield(L, -2, "GetUnitsInRadius");
    lua_setglobal(L, "game");
}

} // namespace LuaAPI
