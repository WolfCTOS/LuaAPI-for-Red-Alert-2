#include <LuaAPI/bindings_house.hpp>
#include <LuaAPI/logger.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// YRpp uses an unqualified 'byte' type but does not define it itself.
using byte = unsigned char;

// YRpp game classes
#include <YRPP.h>

namespace LuaAPI {

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

} // anonymous namespace

int PushHouse(lua_State* L, HouseClass* pHouse) {
    if (!pHouse)
        return 0;
    NewHouse(L, pHouse);
    return 1;
}

namespace {

// House.GetPlayer() -> house | nil
int House_GetPlayer(lua_State* L) {
    HouseClass* pHouse = HouseClass::CurrentPlayer;
    if (!pHouse)
        return 0; // nil

    NewHouse(L, pHouse);
    return 1;
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

    NewHouse(L, pHouse);
    return 1;
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
}

} // namespace LuaAPI
