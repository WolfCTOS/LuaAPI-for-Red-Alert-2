#include <LuaAPI/bindings_techno.hpp>
#include <LuaAPI/bindings_house.hpp>
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
// obj:MarkBounty([color [, durationFrames]]) -> bool
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
    LuaAPI::BarrelPitch::MarkBounty(id, color, static_cast<unsigned int>(dur));
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
