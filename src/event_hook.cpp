#include "event_hook.h"
#include "sub_turret.h"

#include <LuaAPI/logger.hpp>
#include <MinHook.h>

#include <cstring>
#include <cstdio>

namespace LuaAPI {
namespace EventHook {

// ---------------------------------------------------------------------------
// Константы (адрес FootClass::Active_Click_With для 1.001).
// Дредноут/Авианосец наследуются от FootClass (движимые юниты), поэтому этот
// перехват срабатывает на кликах по ним.
// ---------------------------------------------------------------------------
constexpr uintptr_t kActiveClickWithAddr = 0x004D74E0; // FootClass::Active_Click_With

// ActionType. Значение Attack подтверждено логом: при клике атаки приходит action=0x5.
// (0x0E из переписки оказалось неверным.)
constexpr int kActionAttack = 0x5;

using ActiveClickWith_t = void(__fastcall*)(FootClass*, int, void*);
ActiveClickWith_t g_pOriginalActiveClickWith = nullptr;
bool g_installed = false;

std::unordered_map<TechnoClass*, AbstractClass*> g_PlayerTargetOverride;

// ---------------------------------------------------------------------------
// СЕХ-защищённая валидация техно-объекта (жив, валидный RTTI, не в лимбе).
// ---------------------------------------------------------------------------
static bool IsLiveTechno(TechnoClass* p) {
    if (!p) return false;
    __try {
        auto what = p->WhatAmI();
        if (what != AbstractType::Building &&
            what != AbstractType::Unit &&
            what != AbstractType::Infantry &&
            what != AbstractType::Aircraft) {
            return false;
        }
        return p->IsAlive && p->Health > 0 && !p->InLimbo;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Безопасное строковое имя техно-объекта для логов (никогда не трогает vtable
// вне SEH). Возвращает указатель на буфер, валидный до следующего вызова.
// Используется РОТАЦИЯ из 4 буферов, чтобы два вызова в одном выражении
// (например "->" -> " -> ") возвращали РАЗНЫЕ строки, а не один общий буфер:
// иначе в LUA_LOG_INFO оба '{}' показывают одно и то же значение ("DRED -> DRED").
static const char* SafeTechnoName(TechnoClass* p) {
    static thread_local char bufs[4][64];
    static thread_local int idx = 0;
    char* buf = bufs[idx];
    idx = (idx + 1) & 3;

    if (!p) return "nil";
    __try {
        __try {
            const char* id = p->GetType()->get_ID();
            if (id) { snprintf(buf, 64, "%s", id); return buf; }
        } __except (EXCEPTION_EXECUTE_HANDLER) { }
        __try { snprintf(buf, 64, "RTTI=%d", static_cast<int>(p->WhatAmI())); }
        __except (EXCEPTION_EXECUTE_HANDLER) { snprintf(buf, 64, "?"); }
        return buf;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// Приводит ObjectClass* клика к TechnoClass*, если это техно. Резолвит только RTTI
// и ничего не разыменовывает за пределами SEH.
static TechnoClass* ObjectToTechno(void* pObject) {
    if (!pObject) return nullptr;
    TechnoClass* pTech = nullptr;
    __try {
        auto* pAbs = static_cast<AbstractClass*>(pObject);
        int what = static_cast<int>(pAbs->WhatAmI());
        if (what == static_cast<int>(AbstractType::Building) ||
            what == static_cast<int>(AbstractType::Unit) ||
            what == static_cast<int>(AbstractType::Infantry) ||
            what == static_cast<int>(AbstractType::Aircraft)) {
            pTech = static_cast<TechnoClass*>(pAbs);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pTech = nullptr;
    }
    return pTech;
}

// Возвращает имя RTTI-класса по WhatAmI() (безопасно, только дефолтные значения).
static const char* RttiName(int what) {
    switch (what) {
    case static_cast<int>(AbstractType::Building): return "Building";
    case static_cast<int>(AbstractType::Unit):     return "Unit";
    case static_cast<int>(AbstractType::Infantry): return "Infantry";
    case static_cast<int>(AbstractType::Aircraft): return "Aircraft";
    case static_cast<int>(AbstractType::Cell):     return "Cell";
    default:                                       return "Other";
    }
}

bool IsSpawnerShip(TechnoClass* pTechno) {
    if (!IsLiveTechno(pTechno)) return false;
    __try {
        // Дредноут (DREAD, он же DMISL в логе) / Авианосец (HORNET) — определение
        // по типу юнита, а не по SpawnManager (он может отсутствовать или быть
        // неинициализирован). DREAD — реальный TypeID Дредноута в RA2/YR.
        const char* id = pTechno->GetType()->get_ID();
        if (!id) return false;
        if (strcmp(id, "DRED") == 0 || strcmp(id, "DMISL") == 0 || strcmp(id, "HORNET") == 0) {
            return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall Hooked_ActiveClickWith(FootClass* pThis, void* /*edx*/, int action, void* pTarget) {
    // ==== ДИАГНОСТИКА СРАБАТЫВАНИЯ ХУКА ====
    unsigned int actionU = static_cast<unsigned int>(action);
    int thisRtti = -1;
    const char* thisClass = "?";
    __try {
        thisRtti = static_cast<int>(pThis->WhatAmI());
        thisClass = RttiName(thisRtti);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        thisRtti = -1;
        thisClass = "?";
    }
    LUA_LOG_INFO("[EventHook] ActiveClickWith called, this=0x{:X} [{}], action=0x{:X}",
                 reinterpret_cast<uintptr_t>(pThis), thisClass, actionU);

    // Приказ атаки: action == Attack(0x5), цель — живое техно, корабль — спаунер.
    if (actionU == static_cast<unsigned int>(kActionAttack)) {
        TechnoClass* pShip = static_cast<TechnoClass*>(pThis);
        TechnoClass* pTargetTechno = ObjectToTechno(pTarget);

        // ==== ДИАГНОСТИКА: реальный TypeID юнита ====
        __try {
            if (pShip && pShip->WhatAmI() == AbstractType::Unit) {
                auto* pType = pShip->GetType();
                if (pType) {
                    LUA_LOG_CRITICAL("ActiveClickWith: unit TypeID='{}'", pType->get_ID());
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LUA_LOG_CRITICAL("ActiveClickWith: failed to read TypeID (SEH)");
        }

        // ==== ДИАГНОСТИКА КЭШИРОВАНИЯ ====
        LUA_LOG_CRITICAL("ActiveClickWith: action==Attack, pShip=0x{:X}, isSpawner={}, pTarget=0x{:X}, isLive={}",
                         reinterpret_cast<uintptr_t>(pShip),
                         IsSpawnerShip(pShip) ? 1 : 0,
                         reinterpret_cast<uintptr_t>(pTargetTechno),
                         IsLiveTechno(pTargetTechno) ? 1 : 0);

        // Проверяем, является ли цель зданием (spawner-атака по зданию крашит движок).
        bool isBuildingTarget = false;
        if (pTargetTechno) {
            __try {
                auto what = pTargetTechno->WhatAmI();
                isBuildingTarget = (what == AbstractType::Building);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Спауэр-атака: кэшируем цель (позже используем для перенаправления ракет).
        if (IsSpawnerShip(pShip) && IsLiveTechno(pTargetTechno)) {
            g_PlayerTargetOverride[pShip] = static_cast<AbstractClass*>(pTarget);

            if (isBuildingTarget) {
                LUA_LOG_WARN("[EventHook] spawner+building attack: setting target and calling original ONCE");
                pShip->Target = pTargetTechno;  // Устанавливаем Target ПЕРЕД вызовом оригинала
                LUA_FLUSH_LOG();
                // Вызываем оригинал ОДИН раз для инициализации SpawnManager
                if (g_pOriginalActiveClickWith) {
                    g_pOriginalActiveClickWith(pThis, action, pTarget);
                }
                LUA_LOG_WARN("[EventHook] original returned, target will be held via UpdateAll");
                LUA_FLUSH_LOG();
                return;  // НЕ вызываем оригинал второй раз
            }

            // Для не-зданий: кэш + установка Target + вызов оригинала
            pShip->Target = pTargetTechno;
            LUA_LOG_INFO("[EventHook] player Attack order via ActiveClickWith: '{}' -> '{}' cached",
                         SafeTechnoName(pShip), SafeTechnoName(pTargetTechno));
        }
    }

    if (g_pOriginalActiveClickWith) {
        LUA_FLUSH_LOG();
        g_pOriginalActiveClickWith(pThis, action, pTarget);
    }
}

bool Install() {
    if (g_installed || g_pOriginalActiveClickWith) return true;

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(kActiveClickWithAddr), 64,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LUA_LOG_WARN("[EventHook] VirtualProtect(ActiveClickWith 0x{:X}) failed (error {})",
                     kActiveClickWithAddr, GetLastError());
    }

    MH_STATUS st = MH_CreateHook(
        reinterpret_cast<LPVOID>(kActiveClickWithAddr),
        reinterpret_cast<LPVOID>(&Hooked_ActiveClickWith),
        reinterpret_cast<LPVOID*>(&g_pOriginalActiveClickWith));
    if (st != MH_OK) {
        LUA_LOG_WARN("[EventHook] MH_CreateHook(ActiveClickWith 0x{:X}) -> {}",
                     kActiveClickWithAddr, static_cast<int>(st));
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kActiveClickWithAddr));
    if (st != MH_OK) {
        LUA_LOG_WARN("[EventHook] MH_EnableHook(ActiveClickWith 0x{:X}) -> {}",
                     kActiveClickWithAddr, static_cast<int>(st));
        return false;
    }

    g_installed = true;
    LUA_LOG_INFO("[EventHook] installed on UnitClass::Active_Click_With @ 0x{:X}",
                 kActiveClickWithAddr);
    return true;
}

void Uninstall() {
    if (!g_installed) return;
    MH_DisableHook(reinterpret_cast<LPVOID>(kActiveClickWithAddr));
    MH_RemoveHook(reinterpret_cast<LPVOID>(kActiveClickWithAddr));
    g_installed = false;
    g_PlayerTargetOverride.clear();
}

void Update() {
    LUA_LOG_CRITICAL("Update: begin, overrideCount={}", g_PlayerTargetOverride.size());

    for (auto it = g_PlayerTargetOverride.begin(); it != g_PlayerTargetOverride.end();) {
        TechnoClass* pShip = it->first;
        AbstractClass* cachedTarget = it->second;

        // 1. Корабль-владелец мёртв/невалиден — удаляем запись.
        LUA_LOG_CRITICAL("Update: entry ship=0x{:X}, isSpawner={}",
                         reinterpret_cast<uintptr_t>(pShip),
                         IsSpawnerShip(pShip) ? 1 : 0);
        if (!IsSpawnerShip(pShip)) {
            LUA_LOG_CRITICAL("Update: erasing entry (ship not spawner) ship=0x{:X}",
                             reinterpret_cast<uintptr_t>(pShip));
            it = g_PlayerTargetOverride.erase(it);
            continue;
        }

        // 2. Кэшированная цель мертва/невалидна — удаляем запись.
        TechnoClass* pCached = static_cast<TechnoClass*>(cachedTarget);
        LUA_LOG_CRITICAL("Update: cached=0x{:X}, isLive={}",
                         reinterpret_cast<uintptr_t>(pCached),
                         IsLiveTechno(pCached) ? 1 : 0);
        if (!IsLiveTechno(pCached)) {
            LUA_LOG_CRITICAL("Update: erasing entry (target dead) ship=0x{:X}",
                             reinterpret_cast<uintptr_t>(pShip));
            it = g_PlayerTargetOverride.erase(it);
            continue;
        }

        // 3. Окно Rearm/Guard: движок пытается перецелиться. Возвращаем на cached.
        AbstractClass* pCurrent = nullptr;
        __try { pCurrent = pShip->Target; }
        __except (EXCEPTION_EXECUTE_HANDLER) { pCurrent = nullptr; }

        LUA_LOG_CRITICAL("Update: ship=0x{:X}, currentTarget=0x{:X}, cached=0x{:X}, drift={}",
                         reinterpret_cast<uintptr_t>(pShip),
                         reinterpret_cast<uintptr_t>(pCurrent),
                         reinterpret_cast<uintptr_t>(pCached),
                         (pCurrent != cachedTarget) ? 1 : 0);
        if (pCurrent != cachedTarget) {
            LUA_LOG_CRITICAL("Update: forcing target for ship=0x{:X}, target=0x{:X}",
                             reinterpret_cast<uintptr_t>(pShip),
                             reinterpret_cast<uintptr_t>(cachedTarget));
            __try {
                pShip->Target = cachedTarget;
                LUA_LOG_CRITICAL("Update: forced target only (no QueueMission)");
                LUA_LOG_INFO("[EventHook] '{}' drift detected -> forcing back to '{}'",
                             SafeTechnoName(pShip), SafeTechnoName(pCached));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                LUA_LOG_CRITICAL("Update: forced target FAILED (SEH caught)");
            }
        }
        ++it;
    }

    LUA_LOG_CRITICAL("Update: end");
}

void ClearAll() {
    g_PlayerTargetOverride.clear();
}

size_t OverrideCount() {
    return g_PlayerTargetOverride.size();
}

} // namespace EventHook
} // namespace LuaAPI