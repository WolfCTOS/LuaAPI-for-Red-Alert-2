#include <LuaAPI/bindings_techno.hpp>
#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/crash_reporter.hpp>
#include <LuaAPI/logger.hpp>
#include "barrel_pitch.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

// YRpp game classes
#include <YRPP.h>

#include <cmath>
#include <string>
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
        LUA_LOG_DEBUG("ValidateTechno: techno object is not alive (Health={})", pTechno->Health);
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

// obj:GetVeterancy() -> string ("rookie" | "veteran" | "elite")
// Читает TechnoClass::Veterancy (VeterancyStruct). SEH-защищённый доступ.
// Безопасен для Building/Unit/Infantry/Aircraft.
int Techno_GetVeterancy(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushliteral(L, "rookie"); return 1; }

    __try {
        auto& v = pTechno->Veterancy;
        if (v.IsElite())   lua_pushliteral(L, "elite");
        else if (v.IsVeteran()) lua_pushliteral(L, "veteran");
        else               lua_pushliteral(L, "rookie");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushliteral(L, "rookie");
    }
    return 1;
}

// obj:GetAmmo() -> int
// Читает TechnoClass::Ammo. SEH-защищённый доступ; безопасен для всех техно.
int Techno_GetAmmo(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int ammo = 0;
    __try {
        ammo = pTechno->Ammo;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ammo = 0;
    }
    lua_pushinteger(L, ammo);
    return 1;
}

// obj:SetAmmo(value) -> int (new ammo value)
// Записывает TechnoClass::Ammo. SEH-защищённый доступ; безопасен для всех техно.
int Techno_SetAmmo(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int value = static_cast<int>(luaL_checkinteger(L, 2));
    if (value < 0) value = 0;
    __try {
        pTechno->Ammo = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
    }
    lua_pushinteger(L, value);
    return 1;
}


// --- M16 Gate 1: runtime turret HVA frame control --------------------------

// obj:GetTurretAnimFrame() -> int
// Reads TechnoClass::TurretAnimFrame - the HVA frame index the engine feeds
// into MinorVoxelIndexKey.TurretFrameIndex when drawing the turret/barrel
// voxel (Drawing.h: key = key | ((TurretAnimFrame % HVA->FrameCount) << 16)).
// SEH-guarded; returns 0 for invalid technos.
int Techno_GetTurretAnimFrame(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int frame = 0;
    __try {
        frame = pTechno->TurretAnimFrame;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        frame = 0;
    }
    lua_pushinteger(L, frame);
    return 1;
}

// obj:SetTurretAnimFrame(frame) -> int (new frame value)
// Writes TechnoClass::TurretAnimFrame. The engine applies % FrameCount when
// building the draw key, so values >= FrameCount are safe (they wrap).
// The engine rewrites this field whenever the turret rotates - holding a
// value requires rewriting it every frame from Lua (barrel_elevation_diag
// does exactly that). SEH-guarded.
int Techno_SetTurretAnimFrame(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int value = static_cast<int>(luaL_checkinteger(L, 2));
    if (value < 0) value = 0;
    __try {
        pTechno->TurretAnimFrame = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
    }
    lua_pushinteger(L, value);
    return 1;
}

// obj:GetTurretAnimFrameCount() -> int (0 = no voxel turret / HVA not loaded)
// Returns MotLib::FrameCount of the unit type's turret voxel
// (Type->TurretVoxel.HVA->FrameCount). This is the number of turret
// orientation matrices available to the renderer; 1 means "single matrix -
// pitch cannot be changed by frame selection". SHP turrets report 0.
int Techno_GetTurretAnimFrameCount(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int count = 0;
    __try {
        auto* pType = static_cast<TechnoTypeClass*>(pTechno->GetType());
        if (pType && pType->TurretVoxel.VXL && pType->TurretVoxel.HVA) {
            count = pType->TurretVoxel.HVA->FrameCount;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        count = 0;
    }
    lua_pushinteger(L, count);
    return 1;
}

// obj:GetCost() -> int
// Стоимость постройки юнита (TechnoTypeClass::Cost), напр. Rhino (HTNK) = 700.
// SEH-обёртка + ValidateTechno; при ошибке 0.
int Techno_GetCost(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int cost = 0;
    __try {
        auto* pType = static_cast<TechnoTypeClass*>(pTechno->GetType());
        if (pType)
            cost = pType->Cost;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cost = 0;
    }
    lua_pushinteger(L, cost);
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
LuaAPI::CrashReporter::Note("Techno_Disable");
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

// obj:GetBaseSpeed() -> int (TechnoTypeClass::Speed — «сырая» базовая скорость
// из INI Speed=, одинакова для юнитов и пехоты). Нужна как знаменатель для
// расчёта доли выравнивания скорости. SEH + ValidateTechno; при ошибке 0.
int Techno_GetBaseSpeed(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int speed = 0;
    __try {
        auto* pType = static_cast<TechnoTypeClass*>(pTechno->GetType());
        if (pType)
            speed = pType->Speed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        speed = 0;
    }
    lua_pushinteger(L, speed);
    return 1;
}

// obj:GetSight() -> int (TechnoTypeClass::Sight — базовый радиус обзора из INI
// Sight=, в клетках; для наземных равен финальному радиусу See — см.
// docs/research/SHROUD_RCA.md §2.4: radius = Sight при Z ~= 0).
// Нужен dynamic_fow как дистанционный гард "клетка под живым обзором":
// ShroudCounter > 0 в живой игре доказанно мёртв (50 свипов, 750 кадров,
// scPosTotal=0 в LuaAPI.log 2026-09-28), других сигналов "смотрят сейчас" у
// движка нет. SEH + ValidateTechno; при ошибке 0.
int Techno_GetSight(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushinteger(L, 0); return 1; }

    int sight = 0;
    __try {
        auto* pType = static_cast<TechnoTypeClass*>(pTechno->GetType());
        if (pType)
            sight = pType->Sight;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sight = 0;
    }
    lua_pushinteger(L, sight);
    return 1;
}

// obj:GetSpeedFactor() -> double
// Текущий FootClass::SpeedMultiplier (доля полной скорости). Читаем её ДО
// clamp'а, чтобы потом корректно вернуть юниту исходную скорость (напр.
// ветеранский бонус). Не Foot -> 1.0.
int Techno_GetSpeedFactor(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushnumber(L, 1.0); return 1; }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) { lua_pushnumber(L, 1.0); return 1; }

    double f = 1.0;
    __try {
        f = pFoot->SpeedMultiplier;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        f = 1.0;
    }
    lua_pushnumber(L, f);
    return 1;
}

// obj:SetSpeedPercent(percent) -> bool
// Прямой clamp доли скорости. Пишем ОБА поля разом (FootClass::SpeedMultiplier
// — основной множитель движения, на нём живут ветеранство/криты)
// и FieldSpeedPercentage (на случай, если локомотор читает его).
// 1.0 = полная скорость, 0.5 = половина. SEH + ValidateTechno; только FootClass.
int Techno_SetSpeedPercent(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    double pct = luaL_checknumber(L, 2);
    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) { lua_pushboolean(L, 0); return 1; }

    __try {
        pFoot->SpeedMultiplier = pct;
        pFoot->SpeedPercentage = pct;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0); return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
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

    LUA_LOG_DEBUG("[Nav] {} moving to ({},{})", pTechno->GetType()->get_ID(), cellX, cellY);
    lua_pushboolean(L, 1);
    return 1;
}

// obj:GetHarvestLocation() -> {x, y} | nil
// Читает текущий пункт назначения харвестера (FootClass::Destination).
// Если это CellClass — возвращает клетку в {x, y} (в клетках карты), иначе nil.
// SEH-обёртка: объект-назначение может быть освобождён движком в любой момент.
int Techno_GetHarvestLocation(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) {
        lua_pushnil(L);
        return 1;
    }

    CoordStruct coords{};
    bool got = false;
    __try {
        AbstractClass* pDest = pFoot->Destination;
        if (pDest && pDest->WhatAmI() == AbstractType::Cell) {
            coords = static_cast<CellClass*>(pDest)->GetCoords();
            got = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        got = false;
    }

    if (!got) {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, 0, 2);
    lua_pushinteger(L, coords.X / 256);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, coords.Y / 256);
    lua_setfield(L, -2, "y");
    return 1;
}

// obj:HarvestAt(cellX, cellY) -> bool
// То же, что MoveTo, но ставит миссию Mission::Harvest: харвестер идёт и
// добывает в указанной клетке. Возвращает true, если клетка разрешима и
// команда принята.
int Techno_HarvestAt(lua_State* L) {
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

    bool ok = false;
    __try {
        pFoot->Destination = pCell;
        pFoot->QueueMission(Mission::Harvest, true);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LUA_LOG_INFO("[Nav] {} ordered to harvest at ({},{})", pTechno->GetType()->get_ID(), cellX, cellY);
    lua_pushboolean(L, ok ? 1 : 0);
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

// obj:GetMission() -> string | number
// Читает текущую миссию через нативное поле TechnoClass::CurrentMission.
// Возвращает строковое имя ("Guard", "Move", "Attack", "Stop", ...) через
// MissionControlClass::FindName, либо числовой код Mission, если имя недоступно.
int Techno_GetMission(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushnil(L); return 1; }

    Mission m = Mission::None;
    const char* name = nullptr;
    __try {
        m = pTechno->CurrentMission;
        name = MissionControlClass::FindName(m);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        m = Mission::None;
        name = nullptr;
    }

    if (name) {
        lua_pushstring(L, name);
    } else {
        lua_pushinteger(L, static_cast<int>(m));
    }
    return 1;
}

// obj:Attack(target) -> bool
// Нативный приказ атаки на конкретную цель: TechnoClass::SetTarget +
// QueueMission(Mission::Attack). НЕ использует хук ActiveClickWith.
// Возвращает true, если цель валидна и команда принята.
int Techno_Attack(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) { lua_pushboolean(L, 0); return 1; }

    void* ud = luaL_testudata(L, 2, kMetaName);
    TechnoClass* pTarget = ud ? *static_cast<TechnoClass**>(ud) : nullptr;
    if (!pTarget || !ValidateTechno(pTarget)) { lua_pushboolean(L, 0); return 1; }

    __try {
        pFoot->SetTarget(pTarget);
        pFoot->QueueMission(Mission::Attack, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }

    LUA_LOG_DEBUG("[Nav] {} ordered to attack {}", pTechno->GetType()->get_ID(),
                  pTarget->GetType()->get_ID());
    lua_pushboolean(L, 1);
    return 1;
}

// obj:Stop() -> bool
// Нативная остановка: сбрасывает цель и пункт назначения, затем ставит миссию
// Mission::Stop через нативный QueueMission. Возвращает true для мобильного юнита.
int Techno_Stop(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) { lua_pushboolean(L, 0); return 1; }

    __try {
        pFoot->SetTarget(nullptr);
        pFoot->Destination = nullptr;
        pFoot->QueueMission(Mission::Stop, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }

    LUA_LOG_DEBUG("[Nav] {} stopped", pTechno->GetType()->get_ID());
    lua_pushboolean(L, 1);
    return 1;
}

// obj:Unload() -> bool
// Queues the native Unload mission (vanilla deploy path for simple deployers
// such as SCHP: Mission_Unload drives Deploy()/Undeploy()).
// Same safe pattern as Techno_Stop: FootClass validation + SEH.
int Techno_Unload(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) { lua_pushboolean(L, 0); return 1; }

    __try {
        pFoot->QueueMission(Mission::Unload, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }

    LUA_LOG_DEBUG("[Nav] {} ordered to Unload", pTechno->GetType()->get_ID());
    lua_pushboolean(L, 1);
    return 1;
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

// obj:IsAttacking() -> bool (CurrentMission == Mission::Attack)
int Techno_IsAttacking(lua_State* L) {
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
    lua_pushboolean(L, (m == Mission::Attack) ? 1 : 0);
    return 1;
}

// obj:IsOnFloor() -> bool
// Returns true if the object is on the ground (landed).
// For aircraft: true when landed on helipad/airfield; false when airborne.
int Techno_IsOnFloor(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    bool onFloor = false;
    __try {
        onFloor = pTechno->IsOnFloor();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        onFloor = false;
    }
    lua_pushboolean(L, onFloor ? 1 : 0);
    return 1;
}

// obj:IsInAir() -> bool
// Returns true if the object is airborne.
// For aircraft: true when flying; false when landed.
int Techno_IsInAir(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    bool inAir = false;
    __try {
        inAir = pTechno->IsInAir();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        inAir = false;
    }
    lua_pushboolean(L, inAir ? 1 : 0);
    return 1;
}

// obj:IsLanding() -> bool
// Returns true if the aircraft is currently in landing descent.
// Only meaningful for AircraftClass with FlyLocomotionClass.
int Techno_IsLanding(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Aircraft) {
        lua_pushboolean(L, 0);
        return 1;
    }

    bool isLanding = false;
    __try {
        auto* pAircraft = static_cast<AircraftClass*>(pTechno);
        if (pAircraft->Locomotor) {
            auto* pFlyLoco = locomotion_cast<FlyLocomotionClass*>(pAircraft->Locomotor);
            if (pFlyLoco) {
                isLanding = pFlyLoco->IsLanding;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        isLanding = false;
    }
    lua_pushboolean(L, isLanding ? 1 : 0);
    return 1;
}

// obj:Return() -> bool
// Orders aircraft to return to nearest airfield/helipad and land (Mission::Return).
// Only works for AircraftClass.
int Techno_Return(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Aircraft) {
        lua_pushboolean(L, 0);
        return 1;
    }

    FootClass* pFoot = AsFoot(pTechno);
    if (!pFoot) {
        lua_pushboolean(L, 0);
        return 1;
    }

    bool ok = false;
    __try {
        pFoot->SetTarget(nullptr);
        pFoot->Destination = nullptr;
        pFoot->QueueMission(Mission::Return, true);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LUA_LOG_DEBUG("[Nav] {} ordered to Return (land)", pTechno->GetType()->get_ID());
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// obj:Deploy() -> bool
// Orders a deployable unit (e.g., Siege Chopper) to deploy into its ground mode.
// Calls native UnitClass::Deploy().
int Techno_Deploy(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Unit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* pUnit = static_cast<UnitClass*>(pTechno);
    bool ok = false;
    __try {
        pUnit->Deploy();
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LUA_LOG_DEBUG("[Deploy] {} Deploy() called, ok={}", pTechno->GetType()->get_ID(), ok);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// obj:TryToDeploy() -> bool
// Diagnostic: calls native UnitClass::TryToDeploy() and returns its boolean result.
// Does not call Deploy() or CanDeployNow().
int Techno_TryToDeploy(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Unit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* pUnit = static_cast<UnitClass*>(pTechno);
    bool ok = false;
    __try {
        ok = pUnit->TryToDeploy();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LUA_LOG_DEBUG("[Deploy] {} TryToDeploy() called, ok={}", pTechno->GetType()->get_ID(), ok);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// obj:Undeploy() -> bool
// Orders a deployed unit to undeploy into its mobile/air mode.
// Calls native UnitClass::Undeploy().
int Techno_Undeploy(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Unit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* pUnit = static_cast<UnitClass*>(pTechno);
    bool ok = false;
    __try {
        pUnit->Undeploy();
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    LUA_LOG_DEBUG("[Deploy] {} Undeploy() called, ok={}", pTechno->GetType()->get_ID(), ok);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// obj:CanDeployNow() -> bool
// Checks if the unit can currently deploy at its location.
// Calls native FootClass::CanDeployNow().
int Techno_CanDeployNow(lua_State* L) {
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

    bool canDeploy = false;
    __try {
        canDeploy = pFoot->CanDeployNow();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        canDeploy = false;
    }
    lua_pushboolean(L, canDeploy ? 1 : 0);
    return 1;
}

// obj:IsDeployed() -> bool
// Returns true if the unit is currently in its deployed state.
int Techno_IsDeployed(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Unit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* pUnit = static_cast<UnitClass*>(pTechno);
    bool deployed = false;
    __try {
        deployed = pUnit->Deployed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        deployed = false;
    }
    lua_pushboolean(L, deployed ? 1 : 0);
    return 1;
}

// obj:IsDeploying() -> bool
// Returns true if the unit is currently in the process of deploying.
int Techno_IsDeploying(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Unit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* pUnit = static_cast<UnitClass*>(pTechno);
    bool deploying = false;
    __try {
        deploying = pUnit->Deploying;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        deploying = false;
    }
    lua_pushboolean(L, deploying ? 1 : 0);
    return 1;
}

// obj:IsUndeploying() -> bool
// Returns true if the unit is currently in the process of undeploying.
int Techno_IsUndeploying(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (pTechno->WhatAmI() != AbstractType::Unit) {
        lua_pushboolean(L, 0);
        return 1;
    }

    auto* pUnit = static_cast<UnitClass*>(pTechno);
    bool undeploying = false;
    __try {
        undeploying = pUnit->Undeploying;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        undeploying = false;
    }
    lua_pushboolean(L, undeploying ? 1 : 0);
    return 1;
}

// obj:GetTarget() -> techno | nil
// Тихая валидация (без LUA_LOG_WARN, чтобы не спамить лог): возвращает текущую
// цель юнита как TechnoClass, если это живое техно (Building/Unit/Infantry/Aircraft).
int Techno_GetTarget(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushnil(L); return 1; }

    AbstractClass* pTarget = pTechno->Target;
    if (!pTarget) { lua_pushnil(L); return 1; }

    auto what = pTarget->WhatAmI();
    if (what != AbstractType::Building && what != AbstractType::Unit &&
        what != AbstractType::Infantry && what != AbstractType::Aircraft) {
        lua_pushnil(L); return 1;
    }

    auto* pTargetTechno = static_cast<TechnoClass*>(pTarget);
    if (pTargetTechno->Health <= 0) { lua_pushnil(L); return 1; }

    PushTechno(L, pTargetTechno);
    return 1;
}

// techno:SetHealthRatio(ratio) -> nil
// Sets the unit's health to ratio * maxHealth (0.0 - 1.0).
// HARDENED 2026-09-29 (silent engine AV minutes after repair verbs):
// every engine contact inside __try (C2712: POD-only frame), skip limbo
// objects (off-map/transit/paradrop - the engine does not expect HP writes
// there and desyncs), null-check the type (unguarded GetType()->Strength
// deref used to sit on the hot path).
int Techno_SetHealthRatio(lua_State* L) {
    LuaAPI::CrashReporter::Note("Techno_SetHealthRatio");
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;

    lua_Integer ratio = luaL_checknumber(L, 2);
    double r = static_cast<double>(ratio) / 100.0; // accept 0-100 or 0.0-1.0
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    __try {
        if (pTechno->InLimbo)
            return 0;
        auto* pType = pTechno->GetType();
        if (!pType)
            return 0;
        pTechno->Health = static_cast<int>(r * static_cast<double>(pType->Strength));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    LUA_LOG_INFO("[Combat] health set to {:.1%}", r);
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
struct WaypointRead {
    bool ok;
    int x;
    int y;
};

// Tiny SEH helper: __try must not share a frame with C++ objects (C2712).
// All engine contact (Instance read, JMP_THIS calls) happens here.
WaypointRead ReadWaypointSafe(int idx) {
    WaypointRead out{ false, 0, 0 };
    __try {
        ScenarioClass* pScen = ScenarioClass::Instance;
        if (pScen && idx >= 0 && idx < 702 && pScen->IsDefinedWaypoint(idx)) {
            CellStruct cell = pScen->GetWaypointCoords(idx);
            out.ok = true;
            out.x = static_cast<int>(cell.X);
            out.y = static_cast<int>(cell.Y);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.ok = false;
    }
    return out;
}

int game_GetWaypoint(lua_State* L) {
    int waypointId = static_cast<int>(luaL_checkinteger(L, 1));
    if (waypointId < 0 || waypointId >= 702)
        return 0; // nil: outside the engine's [0..701] waypoint range

    WaypointRead r = ReadWaypointSafe(waypointId);
    if (!r.ok)
        return 0; // nil: no live scenario, undefined waypoint, or engine fault

    lua_createtable(L, 0, 2);
    lua_pushinteger(L, r.x);              // x
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, r.y);              // y
    lua_setfield(L, -2, "y");
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

        // Get coords and compute distance.
        // Leptons overflow signed 32-bit when squared (1 cell = 256 leptons),
        // so the delta math is 64-bit.
        CoordStruct coords = pTechno->GetCoords();
        long long dx = static_cast<long long>(coords.X) - static_cast<long long>(x) * 256;
        long long dy = static_cast<long long>(coords.Y) - static_cast<long long>(y) * 256;
        double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy)) / 256.0;

        if (dist <= static_cast<double>(radius)) {
            PushTechno(L, pTechno);
            lua_seti(L, -2, ++n);
        }
    }
    return 1;
}

// M10 multi-turret bindings removed 2026-09-21 (see CHANGELOG). IronCurtain kept below.
// (M10 SetSubTurretTarget / FireSubTurret / ClearSubTurrets / SetSplitTargets /
// FireSplitSalvo removed 2026-09-21 with the SubTurretManager.)

// (M10 FireProjectile removed 2026-09-21 with BulletHook; it was the only
// BulletHook::Register caller and had no live consumers.)

// techno:IronCurtain(durationFrames) -> bool
// Применяет нативный эффект Железного занавеса (тёмный оттенок + временная
// неуязвимость) тем же путём, что супероружие Советов: ObjectClass::IronCurtain
// (ForceShield=true). Источник — владелец юнита. Успех проверяем по IsIronCurtained().
// SEH-обёртка + ValidateTechno.
int Techno_IronCurtain(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    int duration = static_cast<int>(luaL_checkinteger(L, 2));
    if (duration <= 0) { lua_pushboolean(L, 0); return 1; }

    bool ok = false;
    __try {
        HouseClass* pSource = pTechno->Owner;
        pTechno->IronCurtain(duration, pSource, true);
        ok = pTechno->IsIronCurtained();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// --- Bounty mark overlay (Gate 2A, draw-only) --------------------------------
// The overlay itself lives in the DrawAsVXL detour (barrel_pitch.cpp) and is
// keyed by UniqueID; these bindings only register/clear the mark. Draw path
// covers UnitClass (vehicles/ships); other kinds return false (scope limit,
// not an error in the mark system).
//
// obj:MarkBounty([color [, durationFrames [, label]]]) -> bool
//   color: COLORREF (default green 0x00FF00); the C++ overlay converts it
//   to raw 5-6-5 for DrawRect and passes it as-is to DrawText.
//   durationFrames 0/nil = until cleared.
int Techno_MarkBounty(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }

    unsigned int id = 0;
    bool isUnit = false;
    __try {
        id = pTechno->UniqueID;
        isUnit = (pTechno->WhatAmI() == AbstractType::Unit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0); return 1;
    }
    if (id == 0 || !isUnit) { lua_pushboolean(L, 0); return 1; }

    unsigned int color = static_cast<unsigned int>(luaL_optinteger(L, 2, 0x00FF00));
    lua_Integer dur = luaL_optinteger(L, 3, 0);
    if (dur < 0) dur = 0;
    const char* label = nullptr;
    if (!lua_isnoneornil(L, 4)) {
        label = lua_tostring(L, 4); // may be nil on type error; callee tolerates it
    }
    LuaAPI::BarrelPitch::MarkBounty(id, color, static_cast<unsigned int>(dur), label);
    lua_pushboolean(L, 1);
    return 1;
}

// obj:ClearBountyMark() -> nil
int Techno_ClearBountyMark(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno))
        return 0;
    unsigned int id = 0;
    __try { id = pTechno->UniqueID; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    LuaAPI::BarrelPitch::ClearBountyMark(id);
    return 0;
}

// obj:Sell() -> bool — sells a building through the native
// ObjectClass::Sell virtual (vtable dispatch, no hardcoded address — the
// same call the engine makes for the player sell action). Buildings only;
// the engine honors per-type Unsellable and settles occupants/repair state
// itself. SEH-guarded, POD-only locals (C2712). Added for SmartAI surrender
// visibility (a surrendered house liquidates its base); any Lua caller gets
// the standard engine semantics, including refund.
// Returns true when the sell path was entered.
int Techno_Sell(lua_State* L) {
    auto* pTechno = CheckTechno(L, 1);
    if (!ValidateTechno(pTechno)) { lua_pushboolean(L, 0); return 1; }
    bool isBuilding = false;
    __try {
        isBuilding = (pTechno->WhatAmI() == AbstractType::Building);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0); return 1;
    }
    if (!isBuilding) { lua_pushboolean(L, 0); return 1; }
    __try {
        pTechno->Sell(-1); // -1 = Always sell (ObjectClass contract)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0); return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

const luaL_Reg kTechnoMethods[] = {
    { "GetTypeName",   Techno_GetTypeName   },
    { "GetHealth",     Techno_GetHealth     },
    { "GetMaxHealth",  Techno_GetMaxHealth  },
    { "GetVeterancy",  Techno_GetVeterancy  },
    { "GetAmmo",       Techno_GetAmmo       },
    { "SetAmmo",       Techno_SetAmmo       },
    { "GetTurretAnimFrame", Techno_GetTurretAnimFrame },
    { "SetTurretAnimFrame", Techno_SetTurretAnimFrame },
    { "GetTurretAnimFrameCount", Techno_GetTurretAnimFrameCount },
    { "GetCost",       Techno_GetCost       },
    { "GetBaseSpeed",  Techno_GetBaseSpeed  },
    { "GetSight",      Techno_GetSight      },
    { "SetSpeedPercent", Techno_SetSpeedPercent },
    { "GetOwner",      Techno_GetOwner      },
    { "GetPosition",   Techno_GetPosition   },
    { "IsAlive",       Techno_IsAlive       },
    { "GetDistanceTo", Techno_GetDistanceTo },
    { "GetId",         Techno_GetId         },
    { "GetKind",       Techno_GetKind       },
    { "Scatter",       Techno_Scatter       },
    { "MoveTo",        Techno_MoveTo        },
    { "GetHarvestLocation", Techno_GetHarvestLocation },
    { "HarvestAt",     Techno_HarvestAt     },
    { "Hunt",          Techno_Hunt          },
    { "Attack",        Techno_Attack        },
    { "Stop",          Techno_Stop          },
    { "Unload",        Techno_Unload        },
    { "GetMission",    Techno_GetMission    },
    { "IsIdle",        Techno_IsIdle        },
    { "IsAttacking",   Techno_IsAttacking   },
    { "IsOnFloor",     Techno_IsOnFloor     },
    { "IsInAir",       Techno_IsInAir       },
    { "IsLanding",     Techno_IsLanding     },
    { "Return",        Techno_Return        },
    { "Deploy",        Techno_Deploy        },
    { "TryToDeploy",   Techno_TryToDeploy   },
    { "Undeploy",      Techno_Undeploy      },
    { "CanDeployNow",  Techno_CanDeployNow  },
    { "IsDeployed",    Techno_IsDeployed    },
    { "IsDeploying",   Techno_IsDeploying   },
    { "IsUndeploying", Techno_IsUndeploying },
    { "GetTarget",     Techno_GetTarget     },
    { "TakeDamage",    Techno_TakeDamage    },
    { "Disable",       Techno_Disable       },
    { "SetHealthRatio", Techno_SetHealthRatio },
    { "AttachParticleSystem", Techno_AttachParticleSystem },
    { "IronCurtain",    Techno_IronCurtain    },
    { "MarkBounty",     Techno_MarkBounty     },
    { "ClearBountyMark", Techno_ClearBountyMark },
    { "Sell",           Techno_Sell           },
    { nullptr, nullptr }
};

// --- AI team visibility (SmartAI M1-A, read-only) ----------------------------
// Vanilla AI organizes attacks via TeamClass instances (TeamClass::Array):
// each team has a TeamType (INI id), an owner/target HouseClass, a current
// script mission index, and FootClass members linked FirstUnit->
// NextTeamMember. LuaAPI previously exposed none of this, so Lua could
// neither see nor attribute vanilla attack waves.
//
// World.GetAITeams() returns plain data tables (no userdata, no writes):
//   { index, teamtype, owner, targetHouse|nil, scriptMission|nil,
//     totalObjects, isFullStrength, isUnderStrength, isHasBeen,
//     creationFrame, members = {UniqueID, ...} }
// House names reuse the house:GetName() ID space for cross-reference.
// Member identity uses engine UniqueID; team identity uses the scan-local
// array index (TeamClass instances are NOT assigned engine UniqueIDs —
// live 2026-09-23: UniqueID reads back 0xFFFFFFFF for every team — so for
// tracking across scans combine teamtype + creationFrame + member set).
// Array membership proves liveness — never raw pointers. All engine reads
// are SEH-guarded in tiny POD-only helpers (C2712: no C++ objects with
// destructors inside __try).

namespace {

constexpr int kMaxTeamMembers = 64;
constexpr int kMaxTeamsPerCall = 512;
constexpr int kIdBuf = 64;

struct TeamSnapshot {
    bool ok = false;
    unsigned int id = 0; // scan-local TeamClass::Array index (see note above)
    char teamtype[kIdBuf] = {};
    char owner[kIdBuf] = {};
    char target[kIdBuf] = {};
    bool hasTarget = false;
    int scriptMission = -1;
    bool hasScript = false;
    int totalObjects = 0;
    bool isFullStrength = false;
    bool isUnderStrength = false;
    bool isHasBeen = false;
    int creationFrame = 0;
    unsigned int members[kMaxTeamMembers] = {};
    int memberCount = 0;
};

void CopyIdChars(char* dst, const char* src) {
    if (!dst)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (int i = 0; i < kIdBuf - 1; ++i) {
        char c = '\0';
        __try {
            c = src[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        dst[i] = c;
        if (c == '\0')
            return;
    }
    dst[kIdBuf - 1] = '\0';
}

// HouseClass* -> house type ID string (same space as house:GetName()).
// Membership is proven by HouseClass::Array pointer comparison (no deref);
// the ID read itself is SEH-guarded. Returns false when the pointer is not
// a live house.
bool ReadHouseId(HouseClass* pHouse, char* dst) {
    if (!pHouse || !dst)
        return false;
    bool member = false;
    __try {
        for (int i = 0; i < HouseClass::Array.Count; ++i) {
            if (HouseClass::Array.GetItem(i) == pHouse) {
                member = true;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!member)
        return false;
    const char* idStr = nullptr;
    __try {
        idStr = pHouse->get_ID();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    CopyIdChars(dst, idStr);
    return dst[0] != '\0';
}

void ReadTeamSafe(TeamClass* pTeam, TeamSnapshot& out) {
    if (!pTeam)
        return;
    __try {
        if (pTeam->WhatAmI() != AbstractType::Team)
            return;
        // NOTE: TeamClass::UniqueID is never assigned by the engine (reads
        // back 0xFFFFFFFF live) — the caller stamps the scan-local index.
        out.totalObjects = pTeam->TotalObjects;
        out.isFullStrength = pTeam->IsFullStrength;
        out.isUnderStrength = pTeam->IsUnderStrength;
        out.isHasBeen = pTeam->IsHasBeen;
        out.creationFrame = pTeam->CreationFrame;

        TeamTypeClass* pType = pTeam->Type;
        if (pType) {
            const char* typeId = pType->get_ID();
            CopyIdChars(out.teamtype, typeId);
        }

        ScriptClass* pScript = pTeam->CurrentScript;
        if (pScript) {
            out.scriptMission = pScript->CurrentMission;
            out.hasScript = true;
        }

        FootClass* pCur = pTeam->FirstUnit;
        int guard = 0;
        while (pCur && out.memberCount < kMaxTeamMembers && guard < kMaxTeamMembers + 8) {
            ++guard;
            auto what = pCur->WhatAmI();
            if (what != AbstractType::Unit && what != AbstractType::Infantry &&
                what != AbstractType::Aircraft) {
                break;
            }
            auto* pTech = static_cast<TechnoClass*>(pCur);
            if (pTech->Health <= 0)
                break;
            out.members[out.memberCount++] = pTech->UniqueID;
            pCur = pCur->NextTeamMember;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    // Pointer-chasing reads (owner/target houses) stay outside the main
    // __try so a fault there cannot mask a good partial snapshot; each is
    // guarded internally.
    if (!ReadHouseId(pTeam->Owner, out.owner))
        return;
    char tgt[kIdBuf] = {};
    if (pTeam->Target && ReadHouseId(pTeam->Target, tgt)) {
        for (int i = 0; i < kIdBuf; ++i) {
            out.target[i] = tgt[i];
            if (tgt[i] == '\0')
                break;
        }
        out.hasTarget = true;
    }
    out.ok = true;
}

} // anonymous namespace

// World.GetAITeams() -> array of plain team tables (see above).
int World_GetAITeams(lua_State* L) {
    lua_newtable(L);
    int n = 0;
    int count = 0;
    __try {
        count = TeamClass::Array.Count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1; // empty table on unreadable array
    }
    if (count < 0 || count > kMaxTeamsPerCall)
        return 1;
    for (int i = 0; i < count; ++i) {
        TeamClass* pTeam = nullptr;
        __try {
            pTeam = TeamClass::Array.GetItem(i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        TeamSnapshot snap;
        ReadTeamSafe(pTeam, snap);
        if (!snap.ok)
            continue;
        snap.id = static_cast<unsigned int>(i);

        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(snap.id));
        lua_setfield(L, -2, "index");
        lua_pushstring(L, snap.teamtype);
        lua_setfield(L, -2, "teamtype");
        lua_pushstring(L, snap.owner);
        lua_setfield(L, -2, "owner");
        if (snap.hasTarget) {
            lua_pushstring(L, snap.target);
        } else {
            lua_pushnil(L);
        }
        lua_setfield(L, -2, "targetHouse");
        if (snap.hasScript) {
            lua_pushinteger(L, snap.scriptMission);
        } else {
            lua_pushnil(L);
        }
        lua_setfield(L, -2, "scriptMission");
        lua_pushinteger(L, snap.totalObjects);
        lua_setfield(L, -2, "totalObjects");
        lua_pushboolean(L, snap.isFullStrength ? 1 : 0);
        lua_setfield(L, -2, "isFullStrength");
        lua_pushboolean(L, snap.isUnderStrength ? 1 : 0);
        lua_setfield(L, -2, "isUnderStrength");
        lua_pushboolean(L, snap.isHasBeen ? 1 : 0);
        lua_setfield(L, -2, "isHasBeen");
        lua_pushinteger(L, snap.creationFrame);
        lua_setfield(L, -2, "creationFrame");
        lua_createtable(L, snap.memberCount, 0);
        for (int m = 0; m < snap.memberCount; ++m) {
            lua_pushinteger(L, static_cast<lua_Integer>(snap.members[m]));
            lua_seti(L, -2, m + 1);
        }
        lua_setfield(L, -2, "members");

        lua_seti(L, -2, ++n);
    }
    return 1;
}

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

// World.GetAircraft() -> table of all aircraft (from AircraftClass::Array)
int World_GetAircraft(lua_State* L) {
    lua_createtable(L, static_cast<int>(AircraftClass::Array.Count), 0);
    int n = 0;
    for (int i = 0; i < AircraftClass::Array.Count; ++i) {
        auto* pItem = AircraftClass::Array.GetItem(i);
        if (!pItem)
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

// World.GetSelectedUnits() -> table of TechnoClass (только боевые юниты).
// Читает текущее выделение движка (ObjectClass::CurrentObjects = список
// выбранных объектов). Пустая таблица, если ничего не выделено. Здания и
// пехоту пропускаем — возвращаем только UnitClass.
int World_GetSelectedUnits(lua_State* L) {
    lua_createtable(L, ObjectClass::CurrentObjects.Count, 0);
    int n = 0;
    for (int i = 0; i < ObjectClass::CurrentObjects.Count; ++i) {
        ObjectClass* pObj = ObjectClass::CurrentObjects.GetItem(i);
        if (!pObj)
            continue;
        __try {
            AbstractType what = pObj->WhatAmI();
            if (what != AbstractType::Unit)
                continue;
            auto* pTechno = static_cast<TechnoClass*>(pObj);
            if (pTechno->Health <= 0)
                continue;
            PushTechno(L, pTechno);
            lua_seti(L, -2, ++n);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }
    return 1;
}

// World.GetSelectedTechnos() -> table of TechnoClass (Unit + Infantry).
// Читает текущее выделение движка (ObjectClass::CurrentObjects). В отличие от
// World.GetSelectedUnits (который отсеивал пехоту) возвращает ВСЕ мобильные
// техно — юниты И пехоту, — чтобы мод синхронизации строя учитывал и
// тихоходных солдат. Здания пропускаем: двигаться они не могут.
int World_GetSelectedTechnos(lua_State* L) {
    lua_createtable(L, ObjectClass::CurrentObjects.Count, 0);
    int n = 0;
    for (int i = 0; i < ObjectClass::CurrentObjects.Count; ++i) {
        ObjectClass* pObj = ObjectClass::CurrentObjects.GetItem(i);
        if (!pObj)
            continue;
        __try {
            AbstractType what = pObj->WhatAmI();
            if (what != AbstractType::Unit && what != AbstractType::Infantry)
                continue;
            auto* pTechno = static_cast<TechnoClass*>(pObj);
            if (pTechno->Health <= 0)
                continue;
            PushTechno(L, pTechno);
            lua_seti(L, -2, ++n);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }
    return 1;
}

// Предыдущее состояние клавиш для edge-детекта (Input.WasKeyPressed).
bool g_keyPrevState[256] = { false };

// Input.WasKeyPressed(vk) -> bool
// Возвращает true ОДИН раз на переход "не нажата -> нажата" (edge-triggered),
// затем false, пока клавиша удерживается. SEH-защищённый опрос GetAsyncKeyState.
int Input_WasKeyPressed(lua_State* L) {
    int vk = static_cast<int>(luaL_checkinteger(L, 1));
    if (vk < 0 || vk > 255) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool pressed = false;
    __try {
        pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pressed = false;
    }
    bool edge = pressed && !g_keyPrevState[vk];
    g_keyPrevState[vk] = pressed;
    lua_pushboolean(L, edge ? 1 : 0);
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
    for (auto it = g_disabledEntries.begin(); it != g_disabledEntries.end();) {        // Validate BEFORE any dereference: objects destroyed by damage/victory
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

void ClearDisabledObjects() {
    g_disabledEntries.clear();
}

void ClearKeyPrevState() {
    for (int i = 0; i < 256; ++i)
        g_keyPrevState[i] = false;
}

void PushTechno(lua_State* L, void* pTechno) {
    auto* ud = static_cast<void**>(lua_newuserdatauv(L, sizeof(void*), 0));
    *ud = pTechno;
    luaL_getmetatable(L, kMetaName);
    lua_setmetatable(L, -2);
}

// --- M16 Gate 1: one-shot HVA turret frame-count scan -----------------------

// Logs TypeID -> TurretVoxel.HVA->FrameCount for every UnitTypeClass, plus the
// static Type->FireAngle for context. Purpose: close the M16 UNKNOWN "how many
// orientation matrices do stock turret HVAs actually have" with repository
// evidence instead of assumptions. Runs once per session on the first logic
// frame (rules/voxels are loaded by then; they are NOT loaded at DLL bootstrap).
// SEH-guarded per item; a broken entry skips itself.
void LogTurretHvaFrameCounts() {
    LUA_LOG_INFO("[M16] HVA scan begin: {} UnitTypeClass entries",
                 UnitTypeClass::Array.Count);

    int voxel = 0;
    int multi = 0;

    for (int i = 0; i < UnitTypeClass::Array.Count; ++i) {
        UnitTypeClass* pType = nullptr;
        const char* id = nullptr;
        int frameCount = 0;
        int fireAngle = 0;
        bool hasVoxelTurret = false;

        __try {
            pType = UnitTypeClass::Array.GetItem(i);
            if (!pType) continue;
            id = pType->get_ID();
            fireAngle = pType->FireAngle;

            if (pType->TurretVoxel.VXL && pType->TurretVoxel.HVA) {
                hasVoxelTurret = true;
                frameCount = pType->TurretVoxel.HVA->FrameCount;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LUA_LOG_WARN("[M16] HVA scan: SEH on entry {}", i);
            continue;
        }

        if (!hasVoxelTurret)
            continue;

        ++voxel;

        if (frameCount > 1)
            ++multi;

        LUA_LOG_INFO("[M16] HVA {} id={} frames={} fireAngle={}",
                     i, id ? id : "?", frameCount, fireAngle);
    }

    LUA_LOG_INFO("[M16] HVA scan end: {} voxel-turret unit types, {} multi-frame",
                 voxel, multi);
    LUA_LOG_INFO("[M16] (unit types without a voxel turret are not listed)");
}


// (Phase-2 spike block removed 2026-09-24 per its REMOVE-ME checklist;
// production House_SmartAILose bridge lives in bindings_house.cpp.)













// ============================================================================
// Fog-of-war / shroud READ-ONLY bindings (FOW track, 2026-09-26)
//
// Ground truth, established by static RE of gamemd.exe and written up in
// docs/research/SHROUD_RCA.md §2 (grade: STATIC VERIFIED):
//
//   * `MapClass::IsLocationShrouded` @ 0x00586360 reduces to a single test at
//     0x0058647A:   shrouded  <=>  !(CellClass+0x12C & 0x8)
//     That one bit is what the entire game reads (43 direct call sites), so it
//     is the authoritative answer to "has this cell ever been explored".
//
//   * CellClass fields used here. Offsets are read explicitly and are solid;
//     the SEMANTICS of two of the bits below were revised after three runtime
//     probe matches (2026-09-26, 9380 probe calls). See
//     docs/research/SHROUD_RCA.md section 8 and API.md "World - Fog of War".
//       +0x120  BYTE   shroud occlusion frame  (-2 occluded, -1 visible, 0..48)
//       +0x121  BYTE   fog    occlusion frame  (same encoding; NOT always equal
//                              to +0x120 - the two diverged by one cell in run 3)
//       +0x12C  DWORD  AltFlags  bit3 = explored (no shroud) - SOLID
//                              bit4 = previously read as "visible now";
//                              WITHDRAWN. No instruction anywhere clears only
//                              bit4: every writer does `or al,0x18` and every
//                              clearer `and ..,~0x18`, so on the paths examined
//                              the bit is sticky. The combination bit3=1/bit4=0
//                              was observed 0 times in 9380 live probe calls.
//       +0x140  DWORD  Flags     bit0 CenterRevealed, bit1 EdgeRevealed,
//                              bit5 / bit6 - OFFSETS DISPUTED, see below
//
//   * UNRESOLVED DISCREPANCY on FlagToShroud / Fogged. This code reads
//     `Flags+0x140` bit 0x20 for FlagToShroud and bit 0x40 for Fogged.
//     third_party/YRpp/CellClass.h + GeneralDefinitions.h instead place
//     `FlagToShroud = 0x20` and `Fogged = 0x400000` in AltFlags (+0x12C).
//     The offsets have NOT been changed: both fields were false in 100% of live
//     observations, so neither reading has been exercised, and no disassembly
//     evidence gathered so far decides between them. Do not rely on either
//     field until it is re-derived from the binary.
//
//   * NOTE on YRpp field names: third_party/YRpp/CellClass.h declares
//     `char Visibility;` at +0x120 and `char Foggedness;` at +0x121. Disassembly
//     shows +0x120 is fed by `TacticalClass::GetOcclusion(coords, fog=0)`
//     (which tests the *shroud* bit AltFlags&8) and +0x121 by
//     `GetOcclusion(coords, fog=1)` (which tests the *fog* bit Flags&2).
//     So those two YRpp names are swapped relative to the values they hold.
//     They are therefore read here by explicit documented offset rather than
//     through the accessors, so this binding cannot silently change meaning if
//     YRpp is later corrected. AltFlags and Flags are used by name; their names
//     are correct.
//
// READ-ONLY BY DESIGN. There is deliberately no setter, and none should be
// added casually: granting vision, or writing shroud so that explored ground
// re-grows, is exactly the "fake vision" that FSM/FEASIBILITY_TRIAGE.md:268
// puts out of scope. Observation is fine; manufacturing vision is not.
//
// NOT OWNER-AWARE, and unable to be through this binding. The shroud bitfield
// is ONE per cell: `TechnoClass::See` writes the cell's AltFlags regardless of
// which house asked, so on a 2-human MP map this reports the shared cell state
// and must not be presented as "what house X currently sees".
//
// That is a limit of THIS binding, not proof the engine lacks a per-house
// concept. YRpp documents house-scoped fog entry points that are simply not
// exposed here: `DisplayClass::RevealFogShroud(CellStruct*, HouseClass*, bool)`,
// `DisplayClass::MapCellFoggedness(CellStruct*, HouseClass*)`,
// `MapClass::Reveal(HouseClass*)`, `MapClass::Reshroud(HouseClass*)`,
// `HouseClass::ReshroudMap()`, and the per-cell `CellClass::FoggedObjects` list.
// GetFogState takes NO HouseClass argument, so none of that is observable from
// Lua today. Reaching it would mean a new binding, not a change here.
// ============================================================================

static constexpr int kFogMapSide = 512;  // MaxCells = 0x40000 = 512*512
static constexpr int kFogRegionMaxCells = 0x40000;

// One byte per cell, packed, for the bulk region read.
enum : unsigned char {
    kFogBitShrouded = 0x01,  // !(AltFlags & 0x8)  -> never explored. SOLID.
    kFogBitVisible  = 0x02,  //  (AltFlags & 0x10) -> NOT a reliable "visible now";
                             //  see the withdrawn-semantics note in the block
                             //  comment above. Do not rename this without also
                             //  fixing the docs.
    kFogBitCenter   = 0x04,  //  (Flags & 0x01)
    kFogBitEdge     = 0x08,  //  (Flags & 0x02)
    kFogBitToShroud = 0x10,  //  (Flags & 0x20)  -- offset DISPUTED, unverified
    kFogBitFogged   = 0x20,  //  (Flags & 0x40)  -- offset DISPUTED, unverified
};

// CellClass byte offsets, named for what the values actually mean.
static constexpr ptrdiff_t kOffShroudFrame = 0x120;
static constexpr ptrdiff_t kOffFogFrame    = 0x121;

// Set by WriteCellShrouded on every successful write; consumed by
// World_FlushShroudRedraw. File-static, POD, no locking (game thread only).
static bool g_shroudDirty = false;

struct FogCellState {
    bool shrouded;
    bool visible;
    bool centerRevealed;
    bool edgeRevealed;
    bool flagToShroud;
    bool fogged;
    int  shroudFrame;
    int  fogFrame;
    // CellClass+0x130 ShroudCounter: how many sight sources currently cover this
    // cell. This is the ONLY engine-authoritative "is anybody watching this
    // right now" signal available, and it is the one a dynamic-fog implementation
    // must use. `shroudFrame` cannot be used for that: -1 means "no neighbour is
    // in shroud" (entry 0x00 of the 0x7F4194 table), not "this cell is lit", so
    // a mod that shrouds its own surroundings drives that count to zero and
    // destroys its own guard. Measured live 2026-09-26: the lit count fell
    // 213 -> 113 -> 20 -> 4 -> 0 across 60 sweeps.
    int  shroudCounter;
    int  gapsCovering;
};

// ---------------------------------------------------------------------------
// Dynamic FOW WRITE PATH (2026-09-26) - RE-SHROUD ONLY, opt-in, bounded.
//
// WHY THIS EXISTS. Static RE of gamemd.exe found that the engine ALREADY
// contains a two-way shroud mechanism and that only one direction is ever
// driven during play:
//
//   DisplayClass::MapCellVisibility @ 0x4A9CA0 takes a `bIncrease` argument:
//     0x4A9CF2  je   0x4A9CFB
//     0x4A9CF4  call 0x487690   ; bIncrease != 0 -> CellClass::IncreaseShroudCounter
//     0x4A9CFB  call 0x487630   ; bIncrease == 0 -> CellClass::ReduceShroudCounter
//
// TechnoClass::See @ 0x70ADC0 -> MapClass::RevealArea1 @ 0x5673A0 reaches this
// path with bIncrease HARD-CODED to 0, so ordinary sight can only ever REMOVE
// shroud. CellClass::IncreaseShroudCounter @ 0x487690 (which sets
// Flags|=0x20 FlagToShroud) has exactly two static callers, and the only one
// that runs during play is DisplayClass::MapCellVisibility - which is never
// entered with bIncrease=1 by the game.
//
// That is why shroud in RA2/YR never comes back as a unit moves away, and it
// is the mechanism a "dynamic fog" has to drive by hand.
//
// WHY ONLY THE FRAME BYTE IS WRITTEN - the important part.
// The obvious approach, clearing `AltFlags & 8` on one cell, DOES NOT WORK, and
// the first attempt at this mod proved it at runtime: nothing changed on screen.
// The reason is in TacticalClass::GetOcclusion @ 0x6D8700:
//
//   0x6D8AC6  movsx eax, byte ptr [ebp + 0x7F4194]
//
// a 256-byte signed table indexed by an 8-NEIGHBOUR mask (0x40 NW, 0x80 N,
// 0x01 NE, 0x20 W, 0x02 E, 0x10 SW, 0x08 S, 0x04 SE; shroud mode tests
// `AltFlags & 8`, fog mode tests `Flags & 2`). Entry 0x00 -> -1 fully visible,
// 0xFF -> -2 fully occluded, 0x01..0x30 partial sprite frames.
//
// The cell's OWN explored bit is NOT part of its own mask. It is a pure
// function of the eight neighbours. So clearing bit 3 on cell X only changes
// how X's NEIGHBOURS render; X itself is unaffected. To darken X, +0x120 has
// to be written directly.
//
// That write sticks because MapCellVisibility - the only thing that recomputes
// +0x120, at 0x4A9D1B - only runs for cells that are being revealed. A cell the
// player is not looking at is never recomputed, so -2 persists until a unit
// walks back over it.
//
// CORRECTION 2026-09-28 v0.5 (live: 11539 writes, fail=0, zero visual change;
// root-caused by disassembly, not by guessing). The frame-byte write above
// can NEVER blacken the viewport, and the Center/Edge re-arm does not help:
// CellClass::DrawFog @ 0x4801F0 recomputes BOTH bytes from scratch on EVERY
// draw and overwrites them before blitting:
//
//   0x480202  call 0x6D8700 (GetOcclusion fog=0)
//   0x480207  mov [esi+0x120], al   ; our byte overwritten, then blitted from al
//   ...
//   0x48023E  call 0x6D8700 (GetOcclusion fog=1)
//   0x480243  mov [esi+0x121], al
//
// GetOcclusion's shroud mode tests `AltFlags & 8` on each of the eight
// NEIGHBOURS, so the only engine-state write the viewport can ever show is
// clearing the explored bit itself. The v0.2 fat write did that (and worked
// visually - the screenshot black square) but bundled it with clearing 0x10,
// Flags 0x03 and zeroing the counters, which desynced the renderer inputs
// (staircase). The v0.3 "minimal" write kept the renderer inputs consistent
// but wrote a field no draw path reads - invisible by construction.
// The middle path below clears the 0x18 pair (+Center/Edge) so the cell is
// genuinely-unexplored state, which the renderer handles everywhere by
// definition. Counters are kept, so no desync.
// CORRECTION 2026-09-28 v0.5b (live screenshot: reshroud renders as
// translucent BLUE, not black shroud, plus a ragged hole pattern). v0.5
// cleared 0x8 but kept 0x10 - a combination no engine writer ever leaves
// behind: every setter uses `or al, 0x18` and every clearer `and ~0x18`
// (ReduceShroudCounter 0x487664, Unshroud 0x4876F6, Reshroud 0x577B48 /
// 0x577C3A / 0x577D3C / 0x57815D). Explored-but-invisible is alien state and
// the renderer answers it with the fog tint instead of the shroud sheet.
// The fix matches vanilla Reshroud exactly: clear the 0x18 pair together.
// (Single-bit states exist transiently in vanilla - 0x4A98EC sets 0x8 alone,
// 0x5680D8 clears 0x8 alone, 0x48346A clears 0x10 alone - but no steady
// unexplored cell keeps 0x10 set, and Reshroud, the steady-state writer,
// clears both.)
//
// Gameplay note: with 0x8 clear, MapClass::IsLocationShrouded (43 call sites:
// radar, targeting) treats the cell as unexplored - this is real FOW, not a
// visual overlay. Single-player oriented: per-client sight guards diverge,
// so MP determinism is NOT claimed.
//
// SCOPE / SAFETY. This REMOVES information; it never grants any, so it is not
// the "fake vision" that FSM/FEASIBILITY_TRIAGE.md:268 forbids (that clause is
// about revealing). It does change gameplay, so the Lua mod is the gate and
// this function is never called from C++. Cell pointers are validated and every
// write is inside __try.
// ---------------------------------------------------------------------------

// Re-shroud one cell. Returns true on success.
// MINIMAL WRITE, AND WHY IT IS NOW MINIMAL.
//
// v0.2 also cleared `AltFlags 0x18`, `Flags 0x03`, and zeroed
// ShroudCounter/GapsCovering. That produced the blocky staircase artefact and
// shroud appearing next to the player's own units. The reason is that those
// fields are the renderer's INPUT for OTHER cells, not for the one being
// written: GetOcclusion's shroud mode tests `AltFlags & 8` and its fog mode
// tests `Flags & 2` on each of the eight NEIGHBOURS, so clearing them on cell X
// changes how X's neighbours draw, and the corruption spreads outward.
//
// So the write below clears the 0x18 pair plus the Center/Edge pair: the cell
// becomes genuinely-unexplored state. The viewport recomputes from neighbour
// AltFlags on every DrawFog, so X and its neighbours draw exactly like
// never-explored ground - no cascade, no staircase (those came from the v0.2
// combo of additionally zeroing the counters on top).
// The engine restores the cell by itself when a unit walks back over it:
// with 0x8 clear, RevealArea1 (0x56785E je) takes the reveal path to
// MapCellVisibility, ReduceShroudCounter sets 0x18 again, GetOcclusion
// recomputes. Both directions run the vanilla loop; nothing is forced.
//
// Kept: ShroudCounter/GapsCovering are NOT touched (engine bookkeeping;
// zeroing the counters desyncs them and the next ReduceShroudCounter
// underflows). Frame bytes are still written (harmless: DrawFog overwrites
// them; the radar/minimap path may read the stored value).
static bool WriteCellShrouded(int x, int y) {
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide)
        return false;
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell)
            return false;
        unsigned char* bytes = reinterpret_cast<unsigned char*>(cell);

        // Frame bytes NOT written (2026-09-28, blackWatch RCA): DrawFog
        // recomputes +0x120/+0x121 from GetOcclusion on every draw, so the
        // viewport never needs stored values - but stamping 0xFE (-2) made
        // the cell look PRISTINE (never-touched), and the per-frame reveal
        // loop appears to skip pristine-looking interior cells while
        // re-opening boundary ones (live: blackWatch 60-100 persistent inside
        // true sight, reblack flowing only at edges - the v0.3 checkerboard
        // mechanism). Leaving the last-computed frames keeps the cell looking
        // "touched" so reveal keeps processing it. Falsifiable via blackWatch.
        // (If blackWatch does not collapse, the suspect is Center/Edge.)
        // THE write the viewport actually shows: clear the explored bit pair.
        // AltFlags +0x12C bits 0x18 (0x8 Explored + 0x10 visible-now), exactly
        // like vanilla Reshroud. GetOcclusion shroud-mode then sees an
        // unexplored neighbour set and returns occluded frames, which DrawFog
        // blits from the shroud sheet. Re-arm Center/Edge (+0x140 &= ~0x03:
        // 0x01 CenterRevealed, 0x02 EdgeRevealed) so the cell is fully
        // genuinely-unexplored state and re-enters the vanilla reveal path on
        // return. Nothing else in either flag word is touched.
        bytes[0x12C] &= static_cast<unsigned char>(~0x18);
        bytes[0x140] &= static_cast<unsigned char>(~0x03);
        // REDRAW 2026-09-28 (OpenTS recipe): Westwood's own regrow
        // (DisplayClass::Encroach_Shadow -> Shroud_Cell) flags the cell AND
        // its neighbours for redraw and finishes with a FULL tactical redraw
        // (Flag_To_Redraw(GS_REDRAW_TACTICAL)). Raw writes with no redraw
        // leave stale pixels until scroll. CellClass::MarkForRedraw was tried
        // (1-cell, then 3x3) and changed nothing for terrain: the TS lineage
        // has no cell-terrain flag (only Redraw_Objects), so it is dropped.
        // Instead each successful write only sets g_shroudDirty; Lua calls
        // World.FlushShroudRedraw once per sweep, which posts ONE tactical
        // dirty area via TacticalClass::RegisterDirtyArea (YRpp 0x6D2790).
        g_shroudDirty = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// World.SetCellShrouded(x, y) -> boolean
// Makes one cell shrouded again, as if it had never been explored. This is
// the "shroud comes back when the scout leaves" half of dynamic fog.
// The reverse direction is intentionally NOT exposed: revealing is free
// (units do it every frame) and exposing it would be a vision-granting
// write path.
static int World_SetCellShrouded(lua_State* L) {
    LuaAPI::CrashReporter::Note("World_SetCellShrouded");
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, WriteCellShrouded(x, y) ? 1 : 0);
    return 1;
}

// World.NativeReshroud() -> boolean
// Calls the engine's own MapClass::Reshroud(CurrentPlayer) (YRpp 0x577AB0)
// once, SEH-wrapped. Proven live 2026-09-29: blackens unseen mapped ground,
// spares watched cells, no fault, effect persists; the per-frame vanilla
// reveal restores sighted ground by itself. Periodic calls + vanilla reveal
// = dynamic FOW with ZERO per-cell Lua writes (no stale pixels, no speckle,
// native cascade + redraw by construction). The Lua mod drives it on a frame
// timer; all guard/hysteresis machinery stays OFF in native mode (meters
// like blackWatch/orphan keep validating from outside).
// Single-player oriented like the rest of the write path. POD-only frame.
static int World_NativeReshroud(lua_State* L) {
    LuaAPI::CrashReporter::Note("World_NativeReshroud");
    __try {
        HouseClass* player = HouseClass::CurrentPlayer;
        if (!player) {
            lua_pushboolean(L, 0);
            return 1;
        }
        MapClass::Instance.Reshroud(player);
        lua_pushboolean(L, 1);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }
}

// World.FlushShroudRedraw() -> boolean
// Posts ONE tactical dirty area covering the viewport if any SetCellShrouded
// write happened since the last flush, then clears the flag. This is the
// Westwood regrow recipe (Encroach_Shadow ends with a full tactical redraw)
// through the YR-verified primitive TacticalClass::RegisterDirtyArea
// (YRpp 0x6D2790, documented for terrain changes). Called by Lua once per
// sweep when writes occurred; no-ops (false) when nothing was written.
// Viewport extents come from Drawing::RenderWidth/Height (YRpp globals);
// over-dirtying the sidebar strip is harmless. All engine contact inside
// __try, POD-only frame (C2712-safe).
static int World_FlushShroudRedraw(lua_State* L) {
    LuaAPI::CrashReporter::Note("World_FlushShroudRedraw");
    if (!g_shroudDirty) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_shroudDirty = false;
    __try {
        auto* tac = TacticalClass::Instance;
        if (!tac) {
            lua_pushboolean(L, 0);
            return 1;
        }
        RectangleStruct area{ 0, 0, Drawing::RenderWidth, Drawing::RenderHeight };
        tac->RegisterDirtyArea(area, false);
        lua_pushboolean(L, 1);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }
}

// ---------------------------------------------------------------------------
// NATIVE CELL RADIATION  (added for the radiation-weather POC)
// ---------------------------------------------------------------------------
// Why this is cheap, and why it is not a rendering hook.
//
// The green irradiated ground in RA2/YR is drawn by the ENGINE, not by a custom
// renderer. CellClass carries a radiation level per cell (CellClass.h:444,
// `double RadLevel`) and the tile renderer tints from it. The Radiation
// Control Center and the Prism Tower raise it through the same accessors
// exposed here (CellClass.h:249/252), and the engine decays it on its own.
// A weather effect that greens the ground therefore costs two engine calls per
// cell and touches no draw path at all.
//
// That distinction is the entire point. A render detour in this codebase has
// already produced visible artefacts (src/dynamic_fow_poc.cpp); this path
// cannot produce them, because it hands drawing back to the engine and writes
// only a scalar the engine already knows how to interpret.
//
// SAFETY, identical in shape to WriteCellShrouded above:
//   * coordinates bounds-checked against the map before any engine call;
//   * every engine contact inside __try;
//   * POD-only frame, no destructors (C2712).
//
// GAMEPLAY CAVEAT, stated because it is not this API's decision: a cell with a
// high RadLevel ALSO makes the ENGINE damage infantry standing on it. Raising
// it therefore adds damage that no Lua-side accounting attributes to anyone,
// and a mod refunding "damage it dealt" will under-refund. Callers must treat
// the level as a tunable and use GetCellRadLevel to observe what the engine
// actually holds rather than assuming.

// World.GetCellRadLevel(x, y) -> number | nil
static int World_GetCellRadLevel(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushnil(L);
        return 1;
    }
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushnumber(L, static_cast<lua_Number>(cell->RadLevel));
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushnil(L);
        return 1;
    }
}

// World.IsCellRadiated(x, y) -> boolean | nil
// The engine's own predicate, not a Lua-side threshold guess.
static int World_IsCellRadiated(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushnil(L);
        return 1;
    }
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushboolean(L, cell->IsRadiated() ? 1 : 0);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushnil(L);
        return 1;
    }
}

// World.SetCellRadLevel(x, y, delta) -> boolean
//
// delta > 0 raises the level, delta < 0 lowers it, through the engine
// accessors. Because those are the game's own functions, the renderer, the
// decay loop and the infantry-damage rule all stay consistent with a real
// Rad Site instead of drifting from it.
static int World_SetCellRadLevel(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    const double delta = static_cast<double>(luaL_checknumber(L, 3));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide || delta == 0.0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (delta > 0.0)
            cell->RadLevel_Increase(delta);
        else
            cell->RadLevel_Decrease(-delta);
        lua_pushboolean(L, 1);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushboolean(L, 0);
        return 1;
    }
}

// ---------------------------------------------------------------------------
// RadSite control
//
// Painting CellClass::RadLevel does NOT tint the map. The per-cell colour the
// renderer consumes is built by the engine's own accumulation over the enabled
// EffectObject instances hanging off a RadSite (field_0x24), and that
// accumulation is what feeds the interned colour record at cell+0x34. Raising the
// damage value alone therefore changes gameplay and leaves the ground exactly
// as it was - which is what the radiation mod reported at v0.6 ("reached only
// 16.45 and rendered nothing").
//
// So a weather zone has to be a real RadSiteClass, built through the same
// sequence the engine runs at its two audited site-creation sites, and then
// driven through the engine's own reuse path.
//
// Every address below is an audited engine entry point already reached by this
// project. No new hook is installed. Nothing is freed: a RadSite destroys itself
// when its lifetime counter expires, or at global teardown, so the mod keeps a
// small fixed set of zones and reuses them for the whole match.
// ---------------------------------------------------------------------------
namespace {

constexpr size_t    kRadSiteBytes      = 0x74;
constexpr uintptr_t kOperatorNewAddr   = 0x7C8E17;
constexpr uintptr_t kRadSiteCtorAddr   = 0x65B1E0;  // (this)
constexpr uintptr_t kRadSiteBaseAddr   = 0x65B4C0;  // (this, CellStruct*)
constexpr uintptr_t kRadSiteSpreadAddr = 0x65B4D0;  // (this, int)
constexpr uintptr_t kRadSiteLevelAddr  = 0x65B4F0;  // (this, int)
constexpr uintptr_t kRadSiteActivateAd = 0x65B580;  // (this)
constexpr uintptr_t kRadSiteDeactivAd  = 0x65BB50;  // (this)
constexpr uintptr_t kRadSiteAddAddr    = 0x65B530;  // (this, int)
constexpr uintptr_t kCellSetRadSiteAd  = 0x487C70;  // (CellClass*, void*)
constexpr uintptr_t kCellGetRadSiteAd  = 0x487C80;  // (CellClass*)

using FnNew     = void* (__cdecl*)(size_t);
using FnVoid    = void  (__thiscall*)(void*);
using FnBase    = void  (__thiscall*)(void*, const CellStruct*);
using FnInt     = void  (__thiscall*)(void*, int);
using FnCellSet = void  (__thiscall*)(CellClass*, void*);
using FnCellGet = void* (__thiscall*)(CellClass*);

} // namespace

// ---------------------------------------------------------------------------
// Minimum safe dose.
//
// RadSiteClass::Activate divides twice and the second divisor is the QUOTIENT of
// the first: at 0x65B736 it does `idiv [esp+0x20]`, then at 0x65B73F it does
// `idiv ecx` where ecx is that result. With a small dose the quotient rounds to
// zero and the engine raises INT_DIVIDE_BY_ZERO (0xC0000094) at 0x65B73F, which
// is a real crash: repeating it corrupts state and the next fault is an access
// violation.
//
// field_6C is the accumulator, set to RulesScale * level by SetRadLevel, and
// [esp+0x20] is MapClass->0x1814. So the floor is "accumulator >= divisor".
//
// Add() reaches Activate too, hence the same floor applies to retuning.
// ---------------------------------------------------------------------------
static int RadSiteSafeLevel(int requested) {
    int floorLevel = 1;
    __try {
        const int* const mi = *reinterpret_cast<int* const*>(0x8871E0);
        if (mi) {
            const int scale = *(mi + 0x1804 / 4);
            const int window = *(mi + 0x1814 / 4);
            if (window > 0) {
                // accumulator = scale*level must stay at/above the window, and we
                // keep 4x margin so the integer division cannot round down to 0.
                long long need = (static_cast<long long>(window) * 4) /
                                 (scale > 0 ? scale : 1);
                floorLevel = (need < 1) ? 1 : static_cast<int>(need);
                if (floorLevel > 4096) floorLevel = 4096;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        floorLevel = 64;   // conservative fallback
    }
    if (requested < floorLevel) return floorLevel;
    if (requested > 100000) return 100000;
    return requested;
}

// Kill switch for the native light path.
//
// The engine's lighting pass keeps per-frame budgets, a pending-cell queue and
// per-cell converter bookkeeping. Driving a hand-built light into it from the
// mod's update loop is what produced the 0x71C0E752 blitter fault, so the whole
// native path is opt-in and can be turned off without a rebuild.
static bool g_radSiteNativeOn = false;

static void RadSiteNativeFromEnv() {
    if (g_radSiteNativeOn) return;
    const char* const e = std::getenv("LUAAPI_RADSITE_NATIVE");
    g_radSiteNativeOn = (e && e[0] == '1' && e[1] == '\0');
}

// OpenTS: "This is the point the light shines from, fixed when the source is
// created." The audited consumer of the light (CellClass::Init_Light
// equivalent, 0x484180) reads the centre as two dwords at effect+0x38 and
// effect+0x3C and subtracts the cell coordinate from them.
//
// Activate() creates the light object but does NOT hand it a position we can
// rely on: a live probe read (13952, 21888) out of a zone anchored at (54,85).
// With the centre that far away the relight pass walks a huge area and the
// blitter faults at 0x71C0E752, so the position is written explicitly here,
// immediately after the light object exists and before anything consumes it.
static bool SehSetLightPos(void* fx, int x, int y) {
    bool ok = false;
    __try {
        auto* const e = reinterpret_cast<int*>(fx);
        e[0x38 / 4] = x;
        e[0x3C / 4] = y;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// Tiny SEH shells. No Lua, no destructors - see C2712 in AGENTS.md.
static bool SehRadSiteCreate(CellStruct cs, int spread, int level) {
    RadSiteNativeFromEnv();
    if (!g_radSiteNativeOn) return false;
    bool ok = false;
    const int safe = RadSiteSafeLevel(level);
    __try {
        CellClass* const cell = MapClass::Instance.GetCellAt(cs);
        if (cell) {
            void* const existing = reinterpret_cast<FnCellGet>(kCellGetRadSiteAd)(cell);
            if (existing) {
                // The engine reuses a live zone rather than building a second
                // one; Add() is that reuse path (Deactivate, retune, Activate).
                reinterpret_cast<FnInt>(kRadSiteAddAddr)(existing, safe);
            } else {
                void* const p = reinterpret_cast<FnNew>(kOperatorNewAddr)(kRadSiteBytes);
                if (p) {
                    reinterpret_cast<FnVoid>(kRadSiteCtorAddr)(p);
                    reinterpret_cast<FnBase>(kRadSiteBaseAddr)(p, &cs);
                    reinterpret_cast<FnInt>(kRadSiteSpreadAddr)(p, spread);
                    reinterpret_cast<FnInt>(kRadSiteLevelAddr)(p, safe);
                    reinterpret_cast<FnVoid>(kRadSiteActivateAd)(p);
                    // The light object only exists after Activate.
                    void* const fx = *reinterpret_cast<void**>(
                        reinterpret_cast<unsigned char*>(p) + 0x24);
                    if (fx) {
                        SehSetLightPos(fx, static_cast<int>(cs.X),
                                            static_cast<int>(cs.Y));
                    }
                    reinterpret_cast<FnCellSet>(kCellSetRadSiteAd)(cell, p);
                }
            }
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static bool SehRadSiteTune(CellStruct cs, int level) {
    RadSiteNativeFromEnv();
    if (!g_radSiteNativeOn) return false;
    bool ok = false;
    const int safe = RadSiteSafeLevel(level);
    __try {
        CellClass* const cell = MapClass::Instance.GetCellAt(cs);
        void* const p = cell ? reinterpret_cast<FnCellGet>(kCellGetRadSiteAd)(cell) : nullptr;
        if (p) {
            reinterpret_cast<FnInt>(kRadSiteAddAddr)(p, safe);
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static bool SehRadSiteEnabled(CellStruct cs, bool on) {
    RadSiteNativeFromEnv();
    if (!g_radSiteNativeOn) return false;
    bool ok = false;
    __try {
        CellClass* const cell = MapClass::Instance.GetCellAt(cs);
        void* const p = cell ? reinterpret_cast<FnCellGet>(kCellGetRadSiteAd)(cell) : nullptr;
        if (p) {
            // Deactivate/Activate leave the object alive and registered; the
            // zone can be switched back and forth without any destruction.
            if (on) reinterpret_cast<FnVoid>(kRadSiteActivateAd)(p);
            else    reinterpret_cast<FnVoid>(kRadSiteDeactivAd)(p);
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static bool SehRadSiteHasZone(CellStruct cs) {
    bool has = false;
    __try {
        CellClass* const cell = MapClass::Instance.GetCellAt(cs);
        has = cell && reinterpret_cast<FnCellGet>(kCellGetRadSiteAd)(cell) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        has = false;
    }
    return has;
}

// World.RadSiteCreate(x, y, spread, level) -> boolean
// Creates a real radiation zone anchored at (x,y), or retunes the live one if
// the cell already carries a RadSite.
static int World_RadSiteCreate(lua_State* L) {
    LuaAPI::CrashReporter::Note("World_RadSiteCreate");
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    const int spread = static_cast<int>(luaL_checkinteger(L, 3));
    const int level = static_cast<int>(luaL_checkinteger(L, 4));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide ||
        spread < 0 || level < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
    lua_pushboolean(L, SehRadSiteCreate(cs, spread, level) ? 1 : 0);
    return 1;
}

// World.RadSiteSetLevel(x, y, level) -> boolean
static int World_RadSiteSetLevel(lua_State* L) {
    LuaAPI::CrashReporter::Note("World_RadSiteSetLevel");
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    const int level = static_cast<int>(luaL_checkinteger(L, 3));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide || level < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
    lua_pushboolean(L, SehRadSiteTune(cs, level) ? 1 : 0);
    return 1;
}

// World.RadSiteSetEnabled(x, y, on) -> boolean
static int World_RadSiteSetEnabled(lua_State* L) {
    LuaAPI::CrashReporter::Note("World_RadSiteSetEnabled");
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    const bool on = lua_toboolean(L, 3) != 0;
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
    lua_pushboolean(L, SehRadSiteEnabled(cs, on) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// Direct light control.
//
// OpenTS names the four fields CellClass::Init_Light actually consumes, and the
// offsets below are the ones this project's own audit read out of gamemd.exe:
//
//   effect+0x24 RedTint    +0x28 GreenTint   +0x2C BlueTint
//   effect+0x44 Visibility (leptons)
//
// Each tint is fixed point scaled by 1000 (OpenTS NORMAL_LIGHT == 1000, and
// that is the same 0..1000 space the audited quantiser 0x555AC0 works in), and
// each falls off linearly with distance, exactly as Init_Light does:
//
//   num = (1000*vis - 1000*dist) / vis
//   red_tint += (num * RedTint) / 1000
//
// So writing the tints is what decides the colour the player sees. That is the
// lever the weather needs; the level passed to SetRadLevel only drives lifetime
// and the damage value, never the picture.
// ---------------------------------------------------------------------------
namespace {
constexpr int kFxRedTint   = 0x24;
constexpr int kFxGreenTint = 0x28;
constexpr int kFxBlueTint  = 0x2C;
constexpr int kFxVis       = 0x44;
} // namespace

static bool SehRadSiteLight(CellStruct cs, int vis, int r, int g, int b) {
    RadSiteNativeFromEnv();
    if (!g_radSiteNativeOn) return false;
    bool ok = false;
    __try {
        CellClass* const cell = MapClass::Instance.GetCellAt(cs);
        void* const zone = cell ? reinterpret_cast<FnCellGet>(kCellGetRadSiteAd)(cell) : nullptr;
        if (zone) {
            auto* const fx = *reinterpret_cast<unsigned char**>(
                reinterpret_cast<unsigned char*>(zone) + 0x24);
            if (fx) {
                auto* const e = reinterpret_cast<int*>(fx);
                e[kFxRedTint / 4]   = r;
                e[kFxGreenTint / 4] = g;
                e[kFxBlueTint / 4]  = b;
                e[kFxVis / 4]       = vis;
                ok = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// World.RadSiteSetLight(x, y, visibility, red, green, blue) -> boolean
// visibility is in leptons (256 per cell); tints are 0..1000.
static int World_RadSiteSetLight(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    const int vis = static_cast<int>(luaL_checkinteger(L, 3));
    const int r = static_cast<int>(luaL_checkinteger(L, 4));
    const int g = static_cast<int>(luaL_checkinteger(L, 5));
    const int b = static_cast<int>(luaL_checkinteger(L, 6));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (vis < 0 || vis > 65535) { lua_pushboolean(L, 0); return 1; }
    if (r < 0 || r > 1000 || g < 0 || g > 1000 || b < 0 || b > 1000) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
    lua_pushboolean(L, SehRadSiteLight(cs, vis, r, g, b) ? 1 : 0);
    return 1;
}

// World.RadSiteNativeEnabled() -> boolean
// Reports whether the native light path is actually armed. The gate is
// invisible from Lua otherwise: a disabled path just returns false, the mod
// caches that and never retries, so the log shows ground=0 with no explanation.
static int World_RadSiteNativeEnabled(lua_State* L) {
    RadSiteNativeFromEnv();
    lua_pushboolean(L, g_radSiteNativeOn ? 1 : 0);
    return 1;
}

// World.RadSiteHasZone(x, y) -> boolean
static int World_RadSiteHasZone(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
    lua_pushboolean(L, SehRadSiteHasZone(cs) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// RadSiteProbe(x, y) -> string
//
// READ-ONLY diagnostic. Walks the audited chain and reports every node so one
// run can say exactly where the visual stops. It changes nothing.
//
// The single most load-bearing value is the byte at 0x829AE4: 0x554AF0 tests it
// first and bails out entirely when it is zero, which would leave the effect
// unregistered with the manager and therefore invisible no matter what level we
// set. The rest tells us whether the channels that DO reach the effect are
// large enough to show.
// ---------------------------------------------------------------------------
namespace {
constexpr uintptr_t kGateFlagAddr   = 0x829AE4;  // byte gate in 0x554AF0
constexpr uintptr_t kFrameCounterAd = 0xA8EB78;  // global frame counter
constexpr uintptr_t kMapInstanceAd  = 0x8871E0;  // MapClass::Instance
constexpr uintptr_t kRulesObjAddr   = 0xA8B230;  // rules owner
constexpr uintptr_t kInternCountAd  = 0x87F6A8;  // colour intern table
constexpr uintptr_t kInternBufAd    = 0x87F69C;
constexpr uintptr_t kRadVecBufAd    = 0xB04BD4;  // RadSite vector buffer
constexpr uintptr_t kRadVecCountAd  = 0xB04BE0;  // RadSite vector count

// World.RadSiteList() -> string
//
// Enumerates the engine's RadSite array instead of probing one cell. A live
// match was measured at radVecCount=1 with zone=0 on the Desolator's own cell,
// so the sites exist but sit away from any cell we would think to ask about.
// This is the reference dump: for a site the ENGINE built, print the base cell,
// the spread, and the light source that tints the ground, so the mod can be
// built against measured values instead of guesses.
static int World_RadSiteList(lua_State* L) {
    char buf[1024];
    size_t used = 0;
    buf[0] = '\0';
    int n = 0;
    __try {
        auto* const rc = reinterpret_cast<const int*>(kRadVecCountAd);
        auto* const vb = reinterpret_cast<unsigned char**>(kRadVecBufAd);
        int count = rc ? *rc : 0;
        if (count < 0) count = 0;
        if (count > 8) count = 8;            // never walk past what exists
        for (int i = 0; i < count; ++i) {
            unsigned char* const site = vb[i];
            if (!site) continue;
            const int lvl = *reinterpret_cast<const int*>(site + 0x70);
            const int spr = *reinterpret_cast<const int*>(site + 0x08);
            void* const fx = *reinterpret_cast<void**>(site + 0x54);
            if (fx) {
                // Layout UNVERIFIED. Nothing here is asserted: this is the raw
                // object so a single run identifies the fields by value. The
                // prior is only the documented member set (Intensity, R/G/B
                // Tint, Visibility, Position, IsEnabled); every offset guess so
                // far in this project was wrong, so none is encoded here.
                auto* const e = reinterpret_cast<unsigned char*>(fx);
                char raw[512];
                raw[0] = '\0';
                for (int off = 0; off < 0x60; off += 4) {
                    int w = 0;
                    _snprintf_s(raw + strlen(raw), sizeof(raw) - strlen(raw),
                        _TRUNCATE, "%02X:%08X ", off,
                        *reinterpret_cast<const int*>(e + off));
                }
                const size_t room = (used < sizeof(buf)) ? (sizeof(buf) - used) : 0;
                if (room > 0) {
                    used += static_cast<size_t>(_snprintf_s(buf + used, room, _TRUNCATE,
                        " LIGHTSRC[%s]", raw));
                }
            }
            n = _snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
                " [%d] site=%p fx=%p", i, (void*)site, fx);
            if (n < 0) break;
            used += static_cast<size_t>(n);
            if (fx) {
                auto* const e = reinterpret_cast<int*>(fx);
                n = _snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
                    " tint=%d/%d/%d vis=%d pos=(%d,%d) en=%d",
                    e[kFxRedTint / 4], e[0x28 / 4], e[0x2C / 4],
                    e[kFxVis / 4], e[0x38 / 4], e[0x3C / 4],
                    (unsigned)((unsigned char*)fx)[0x48]);
                if (n > 0) used += static_cast<size_t>(n);
            }
            // The field NAMES are still unproven: reading the object head as
            // base cell returned 0x1C7B47D0, which is the object's own address
            // - byte order, not a layout. So print the raw words and let a run
            // settle which offset is which, instead of guessing again.
            {
                // Dump the WHOLE object and let the values identify the fields.
                // The exact header size is not known (the property block is 76
                // bytes, so the header is 0x74-0x4C), and every offset guess so
                // far was wrong. The values are self-identifying though:
                // Spread is small, SpreadInLeptons == Spread*256, RadLevel is
                // <= 500, and Tint is the RadColor triple with green dominant.
                char raw[512];
                raw[0] = '\0';
                for (int off = 0; off < 0x74; off += 4) {
                    const int w = *reinterpret_cast<const int*>(site + off);
                    int m = _snprintf_s(raw + strlen(raw), sizeof(raw) - strlen(raw),
                        _TRUNCATE, "%02X:%08X ", off, w);
                    if (m <= 0) break;
                }
                const size_t room = (used < sizeof(buf)) ? (sizeof(buf) - used) : 0;
                if (room > 0) {
                    used += static_cast<size_t>(_snprintf_s(buf + used, room, _TRUNCATE,
                        " raw[%s]", raw));
                }
            }
            if (n < 0) break;
            if (used >= sizeof(buf) - 1) break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strncat_s(buf, sizeof(buf), " [SEH]", _TRUNCATE);
    }
    lua_pushstring(L, buf);
    return 1;
}
using FnRaw = void* (__thiscall*)(CellClass*);
} // namespace

static int World_RadSiteProbe(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushstring(L, "probe: out of bounds");
        return 1;
    }
    char buf[768];
    buf[0] = '\0';
    CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
    __try {
        auto* const rb = reinterpret_cast<const unsigned char*>(kGateFlagAddr);
        auto* const fr = reinterpret_cast<const int*>(kFrameCounterAd);
        auto* const mi = reinterpret_cast<const int*>(kMapInstanceAd);
        auto* const ro = reinterpret_cast<const int*>(kRulesObjAddr);
        auto* const ic = reinterpret_cast<const int*>(kInternCountAd);
        auto* const rc = reinterpret_cast<const int*>(kRadVecCountAd);
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "gate829AE4=%u frame=%d mapF1=%d mapF2=%d mapMul=%g "
            "rules352C=%d 3534=%d 3538=%d 353C=%d radVecCount=%d internCount=%d",
            (unsigned)*rb, *fr, mi ? *(mi + 0x1810 / 4) : -1,
            mi ? *(mi + 0x1814 / 4) : -1,
            mi ? *(reinterpret_cast<const double*>(mi) + 0x1828 / 8) : -1.0,
            ro ? *(ro + 0x352C / 4) : -1, ro ? *(ro + 0x3534 / 4) : -1,
            ro ? *(ro + 0x3538 / 4) : -1, ro ? *(ro + 0x353C / 4) : -1,
            rc ? *rc : -1, ic ? *ic : -1);

        CellClass* const cell = MapClass::Instance.GetCellAt(cs);
        void* const zone = cell ? reinterpret_cast<FnCellGet>(kCellGetRadSiteAd)(cell) : nullptr;
        char z2[320];
        _snprintf_s(z2, sizeof(z2), _TRUNCATE, "cell=%p zone=%p", (void*)cell, zone);
        strncat_s(buf, sizeof(buf), z2, _TRUNCATE);
        if (zone) {
            auto* const z = reinterpret_cast<unsigned char*>(zone);
            char z3[256];
            _snprintf_s(z3, sizeof(z3), _TRUNCATE,
                " base=(%d,%d) spread=%d level=%d acc=%d life=%d effect=%p",
                *reinterpret_cast<short*>(z + 0x40),
                *reinterpret_cast<short*>(z + 0x42),
                *reinterpret_cast<int*>(z + 0x44),
                *reinterpret_cast<int*>(z + 0x4C),
                *reinterpret_cast<int*>(z + 0x6C),
                *reinterpret_cast<int*>(z + 0x70),
                *reinterpret_cast<void**>(z + 0x24));
            strncat_s(buf, sizeof(buf), z3, _TRUNCATE);
            void* const eff = *reinterpret_cast<void**>(z + 0x24);
            if (eff) {
                auto* const e = reinterpret_cast<unsigned char*>(eff);
                char z4[320];
                _snprintf_s(z4, sizeof(z4), _TRUNCATE,
                    " EFFECT ch=%d/%d/%d ch4=%d frmGate=%d en=%d r=%d ctr=(%d,%d) rad=%d",
                    *reinterpret_cast<int*>(e + 0x24),
                    *reinterpret_cast<int*>(e + 0x28),
                    *reinterpret_cast<int*>(e + 0x2C),
                    *reinterpret_cast<int*>(e + 0x30),
                    *reinterpret_cast<int*>(e + 0x34),
                    (unsigned)e[0x48],
                    *reinterpret_cast<int*>(e + 0x44),
                    *reinterpret_cast<short*>(e + 0x38),
                    *reinterpret_cast<short*>(e + 0x3C),
                    *reinterpret_cast<int*>(e + 0x44));
                strncat_s(buf, sizeof(buf), z4, _TRUNCATE);
                // Raw dump: the audited reads above disagree with the expected
                // layout, and the centre coordinate is the field that decides
                // whether any cell is in range at all. This settles it in one
                // run instead of another disassembly round.
                char raw[24];
                raw[0] = '\0';
                for (int off = 0; off < 0x4C; off += 4) {
                    int n = _snprintf_s(raw + strlen(raw), sizeof(raw) - strlen(raw),
                                        _TRUNCATE, "%02X%02X%02X%02X",
                                        e[off], e[off + 1], e[off + 2], e[off + 3]);
                    if (n <= 0) break;
                }
                strncat_s(buf, sizeof(buf), " RAW[", _TRUNCATE);
                strncat_s(buf, sizeof(buf), raw, _TRUNCATE);
                strncat_s(buf, sizeof(buf), "]", _TRUNCATE);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strncat_s(buf, sizeof(buf), " EXCEPTION", _TRUNCATE);
    }
    lua_pushstring(L, buf);
    return 1;
}


// True when the cell's shroud frame is -1.
//
// WARNING - this is NOT "is the cell being watched". -1 is entry 0x00 of the
// 0x7F4194 neighbour table, i.e. "no neighbour is in shroud", which is a
// statement about the surroundings rather than about this cell. A live test
// (2026-09-26) showed a dynamic-fog mod using this as its guard counting 213,
// 113, 20, 4, then 0 lit cells as it shrouded its own surroundings: a positive
// feedback loop that destroys the guard. Use `shroudCounter` from
// GetFogState for "is anybody watching this cell".
static int World_IsCellLit(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide) {
        lua_pushnil(L);
        return 1;
    }
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell) {
            lua_pushnil(L);
            return 1;
        }
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(cell);
        const signed char frame = static_cast<signed char>(bytes[kOffShroudFrame]);
        lua_pushboolean(L, frame == -1 ? 1 : 0);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lua_pushnil(L);
        return 1;
    }
}

// Read one cell's fog/shroud state. Returns false for an out-of-map coordinate
// or an unresolvable cell. Deliberately tiny and POD-only: __try/__except must
// not share a frame with objects that have destructors (C2712).
static bool ReadFogCell(int x, int y, FogCellState* out) {
    if (!out)
        return false;
    if (x < 0 || y < 0 || x >= kFogMapSide || y >= kFogMapSide)
        return false;
    __try {
        CellClass* cell = MapClass::Instance.GetCellAt(
            CellStruct{ static_cast<short>(x), static_cast<short>(y) });
        if (!cell)
            return false;
        const DWORD alt = static_cast<DWORD>(cell->AltFlags);
        const DWORD flg = static_cast<DWORD>(cell->Flags);
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(cell);
        out->shrouded       = (alt & 0x08u) == 0;
        out->visible        = (alt & 0x10u) != 0;
        out->centerRevealed = (flg & 0x01u) != 0;
        out->edgeRevealed   = (flg & 0x02u) != 0;
        out->flagToShroud   = (flg & 0x20u) != 0;
        out->fogged         = (flg & 0x40u) != 0;
        out->shroudFrame    = static_cast<int>(static_cast<signed char>(bytes[kOffShroudFrame]));
        out->fogFrame       = static_cast<int>(static_cast<signed char>(bytes[kOffFogFrame]));
        // +0x130 / +0x134 - authoritative "is anybody watching this cell"
        out->shroudCounter  = static_cast<int>(
            *reinterpret_cast<const int*>(bytes + 0x130));
        out->gapsCovering   = static_cast<int>(
            *reinterpret_cast<const int*>(bytes + 0x134));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// World.GetFogState(x, y) -> table | nil
//   { shrouded, visible, centerRevealed, edgeRevealed, flagToShroud, fogged,
//     shroudFrame, fogFrame }
// nil when (x,y) is outside the map. READ-ONLY: see the block comment above.
//
// STATUS: experimental / raw state inspection, NOT a curated fog-of-war API.
// It was built to MEASURE the engine, and the measurement contradicted part of
// this function's own original documentation:
//   * `visible` is NOT a reliable "currently in a sight radius" (see block
//     comment; the bit is sticky and the combination explored+not-visible was
//     never observed in 9380 live calls).
//   * `flagToShroud` / `fogged` offsets are disputed against YRpp and were
//     false in every live observation.
// `shrouded` and `shroudFrame` are the two fields to rely on.
// Does NOT let you distinguish "remembered" from "on the current fog/visibility
// gradient" - that needs owner-aware or renderer-side data this binding lacks.
static int World_GetFogState(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));

    FogCellState st{};
    if (!ReadFogCell(x, y, &st)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 8);
    lua_pushboolean(L, st.shrouded);
    lua_setfield(L, -2, "shrouded");
    lua_pushboolean(L, st.visible);
    lua_setfield(L, -2, "visible");
    lua_pushboolean(L, st.centerRevealed);
    lua_setfield(L, -2, "centerRevealed");
    lua_pushboolean(L, st.edgeRevealed);
    lua_setfield(L, -2, "edgeRevealed");
    lua_pushboolean(L, st.flagToShroud);
    lua_setfield(L, -2, "flagToShroud");
    lua_pushboolean(L, st.fogged);
    lua_setfield(L, -2, "fogged");
    lua_pushinteger(L, st.shroudFrame);
    lua_setfield(L, -2, "shroudFrame");
    lua_pushinteger(L, st.fogFrame);
    lua_setfield(L, -2, "fogFrame");
    // Sight-source counter. THIS is the authoritative "is anybody watching this
    // cell right now" signal, and the only one a dynamic-fog implementation
    // should use for that decision. > 0 while any unit's sight covers the cell.
    lua_pushinteger(L, st.shroudCounter);
    lua_setfield(L, -2, "shroudCounter");
    lua_pushinteger(L, st.gapsCovering);
    lua_setfield(L, -2, "gapsCovering");
    return 1;
}

// World.IsLocationShrouded(x, y) -> boolean | nil
// Thin boolean form of GetFogState, named after the engine function it mirrors.
static int World_IsLocationShrouded(lua_State* L) {
    const int x = static_cast<int>(luaL_checkinteger(L, 1));
    const int y = static_cast<int>(luaL_checkinteger(L, 2));

    FogCellState st{};
    if (!ReadFogCell(x, y, &st)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, st.shrouded);
    return 1;
}

// World.GetFogRegion(x, y, width, height) -> string | nil, ''
// One byte per cell, row-major from (x,y). Packed bits:
//   0x01 shrouded   0x02 visible    0x04 centerRevealed
//   0x08 edgeRevealed  0x10 flagToShroud  0x20 fogged
// The bulk path an overlay actually wants: a viewport is ~100x100, and 10k
// separate Lua calls per redraw is not viable. A string is used rather than a
// table because it is one allocation instead of w*h table slots.
// Returns nil on a bad argument or a region that leaves the map; returns an
// empty string for a zero-sized region.
static int World_GetFogRegion(lua_State* L) {
    const int x0 = static_cast<int>(luaL_checkinteger(L, 1));
    const int y0 = static_cast<int>(luaL_checkinteger(L, 2));
    const int w  = static_cast<int>(luaL_checkinteger(L, 3));
    const int h  = static_cast<int>(luaL_checkinteger(L, 4));

    if (w <= 0 || h <= 0) {
        lua_pushliteral(L, "");
        return 1;
    }
    if (x0 < 0 || y0 < 0 || w > kFogMapSide || h > kFogMapSide)
        goto bad_region;
    if (static_cast<long long>(w) * static_cast<long long>(h) > kFogRegionMaxCells)
        goto bad_region;
    if (x0 + w > kFogMapSide || y0 + h > kFogMapSide)
        goto bad_region;

    {
        std::string buf(static_cast<size_t>(w) * static_cast<size_t>(h), '\0');
        for (int dy = 0; dy < h; ++dy) {
            for (int dx = 0; dx < w; ++dx) {
                FogCellState st{};
                unsigned char b = 0;
                if (ReadFogCell(x0 + dx, y0 + dy, &st)) {
                    if (st.shrouded)       b |= kFogBitShrouded;
                    if (st.visible)        b |= kFogBitVisible;
                    if (st.centerRevealed) b |= kFogBitCenter;
                    if (st.edgeRevealed)   b |= kFogBitEdge;
                    if (st.flagToShroud)   b |= kFogBitToShroud;
                    if (st.fogged)         b |= kFogBitFogged;
                }
                buf[static_cast<size_t>(dy) * static_cast<size_t>(w) + static_cast<size_t>(dx)] =
                    static_cast<char>(b);
            }
        }
        lua_pushlstring(L, buf.data(), buf.size());
    }
    return 1;

bad_region:
    lua_pushnil(L);
    return 1;
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
    lua_pushcfunction(L, World_GetAircraft);
    lua_setfield(L, -2, "GetAircraft");
    lua_pushcfunction(L, World_GetAllUnits);
    lua_setfield(L, -2, "GetAllUnits");
    lua_pushcfunction(L, game_GetWaypoint);
    lua_setfield(L, -2, "GetWaypoint");
    lua_pushcfunction(L, game_GetUnitsInRadius);
    lua_setfield(L, -2, "GetUnitsInRadius");
    lua_pushcfunction(L, World_GetSelectedUnits);
    lua_setfield(L, -2, "GetSelectedUnits");
    lua_pushcfunction(L, World_GetSelectedTechnos);
    lua_setfield(L, -2, "GetSelectedTechnos");
    lua_pushcfunction(L, World_GetAITeams);
    lua_setfield(L, -2, "GetAITeams");
    // Fog-of-war / shroud, READ-ONLY (FOW track 2026-09-26).
    lua_pushcfunction(L, World_GetFogState);
    lua_setfield(L, -2, "GetFogState");
    lua_pushcfunction(L, World_IsLocationShrouded);
    lua_setfield(L, -2, "IsLocationShrouded");
    lua_pushcfunction(L, World_GetFogRegion);
    lua_setfield(L, -2, "GetFogRegion");
    // Dynamic-FOW write path (RE-SHROUD only). See the block comment above the
    // definition. Registered here so Lua can drive it; never called by C++.
    lua_pushcfunction(L, World_IsCellLit);
    lua_setfield(L, -2, "IsCellLit");
    lua_pushcfunction(L, World_SetCellShrouded);
    lua_setfield(L, -2, "SetCellShrouded");
    lua_pushcfunction(L, World_FlushShroudRedraw);
    lua_setfield(L, -2, "FlushShroudRedraw");
    lua_pushcfunction(L, World_NativeReshroud);
    lua_setfield(L, -2, "NativeReshroud");


    lua_pushcfunction(L, World_GetCellRadLevel);
    lua_setfield(L, -2, "GetCellRadLevel");

    lua_pushcfunction(L, World_IsCellRadiated);
    lua_setfield(L, -2, "IsCellRadiated");

    lua_pushcfunction(L, World_SetCellRadLevel);
    lua_setfield(L, -2, "SetCellRadLevel");
    lua_pushcfunction(L, World_RadSiteCreate);
    lua_setfield(L, -2, "RadSiteCreate");
    lua_pushcfunction(L, World_RadSiteSetLevel);
    lua_setfield(L, -2, "RadSiteSetLevel");
    lua_pushcfunction(L, World_RadSiteSetEnabled);
    lua_setfield(L, -2, "RadSiteSetEnabled");
    lua_pushcfunction(L, World_RadSiteHasZone);
    lua_setfield(L, -2, "RadSiteHasZone");
    lua_pushcfunction(L, World_RadSiteSetLight);
    lua_setfield(L, -2, "RadSiteSetLight");
    lua_pushcfunction(L, World_RadSiteNativeEnabled);
    lua_setfield(L, -2, "RadSiteNativeEnabled");
    lua_pushcfunction(L, World_RadSiteProbe);
    lua_setfield(L, -2, "RadSiteProbe");
    lua_pushcfunction(L, World_RadSiteList);
    lua_setfield(L, -2, "RadSiteList");
    lua_setglobal(L, "World");

    // Global "Input" namespace
    lua_newtable(L);
    lua_pushcfunction(L, Input_WasKeyPressed);
    lua_setfield(L, -2, "WasKeyPressed");
    lua_setglobal(L, "Input");

    // Global "game" namespace
    lua_newtable(L);
    lua_pushcfunction(L, game_GetWaypoint);
    lua_setfield(L, -2, "GetWaypoint");
    lua_pushcfunction(L, game_GetUnitsInRadius);
    lua_setfield(L, -2, "GetUnitsInRadius");
    lua_setglobal(L, "game");
}

} // namespace LuaAPI
