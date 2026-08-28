#include "event_hook.h"
#include "sub_turret.h"

#include <LuaAPI/logger.hpp>
#include <MinHook.h>

namespace LuaAPI {
namespace EventHook {

// TargetClass::As_Techno() (0x6E6F20) — не-const и портит this (m_RTTI мог меняться),
// поэтому всегда передаём копию. Читаем через raw offset из датабуфера события.
static TechnoClass* TargetClassToTechno(TargetClass t) {
    TechnoClass* p = nullptr;
    __try { p = t.As_Techno(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { p = nullptr; }
    return p;
}

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

std::unordered_map<TechnoClass*, TargetClass> g_PlayerTargetOverride;

// ---------------------------------------------------------------------------
// SEH-защищённая валидация техно-объекта (жив, валидный RTTI, не в лимбе).
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
// вне SEH). Возвращает статический буфер, валидный до следующего вызова.
static const char* SafeTechnoName(TechnoClass* p) {
    static thread_local char buf[64];
    if (!p) return "nil";
    __try {
        __try {
            const char* id = p->GetType()->get_ID();
            if (id) { snprintf(buf, sizeof(buf), "%s", id); return buf; }
        } __except (EXCEPTION_EXECUTE_HANDLER) { }
        __try { snprintf(buf, sizeof(buf), "RTTI=%d", static_cast<int>(p->WhatAmI())); }
        __except (EXCEPTION_EXECUTE_HANDLER) { snprintf(buf, sizeof(buf), "?"); }
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
        // Дредноут (DMISL) / Авианосец (HORNET) — команда спавна ракет.
        return pTechno->SpawnManager != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Хук: FootClass::Active_Click_With(ActionType action, ObjectClass* pTarget).
// Срабатывает при ЛЮБОМ клике игрока по юниту (движение/атака/захват и т.д.).
// ---------------------------------------------------------------------------
void __fastcall Hooked_ActiveClickWith(FootClass* pThis, void* /*edx*/, int action, void* pTarget) {
    // ==== ДИАГНОСТИКА СРАБАТЫВАНИЯ ХУКА ====
    // Безусловный лог: каждый клик по юниту Должен дать строку здесь.
    // Если её нет — адрес 0x4D74E0 неверный для этой сборки (см. шаг 6: DisplayClass 0x4AE750).
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
    // ==== ДИАГНОСТИКА КРАША (шаг 1) ====
    // ПРОВЕРКА: не вызываем оригинал, чтобы понять, крашится ли игра из-за него.
    LUA_LOG_CRITICAL("ActiveClickWith: will NOT call original, this=0x{:X}, action=0x{:X}",
                     reinterpret_cast<uintptr_t>(pThis), actionU);

    // Приказ атаки: action == Attack(0x5), цель — живое техно, корабль — спаунер.
    if (actionU == static_cast<unsigned int>(kActionAttack)) {
        TechnoClass* pShip = static_cast<TechnoClass*>(pThis);
        TechnoClass* pTargetTechno = ObjectToTechno(pTarget);

        if (IsSpawnerShip(pShip) && IsLiveTechno(pTargetTechno)) {
            g_PlayerTargetOverride[pShip] = TargetClass(static_cast<AbstractClass*>(pTarget));
            LUA_LOG_INFO("[EventHook] player Attack order via ActiveClickWith: '{}' -> '{}' cached",
                         SafeTechnoName(pShip), SafeTechnoName(pTargetTechno));
        }
    }

    // ВЫЗОВ ОРИГИНАЛА ЗАКОММЕНТИРОВАН (диагностика). Если краш уйдёт — проблема в оригинале.
    // if (g_pOriginalActiveClickWith) {
    //     g_pOriginalActiveClickWith(pThis, action, pTarget);
    // }
    LUA_LOG_CRITICAL("ActiveClickWith: original skipped");
}

bool Install() {
    if (g_installed || g_pOriginalActiveClickWith) return true;

    // Протекция страницы не обязательна: MinHook сам обрабатывает RWX при патче,
    // но добавим PAGE_EXECUTE_READWRITE для надёжности (как в других хуках проекта).
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
        TargetClass cachedTarget = it->second;

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
        TechnoClass* pCached = TargetClassToTechno(cachedTarget);
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
                         (pCurrent != pCached) ? 1 : 0);
        if (pCurrent != pCached) {
            LUA_LOG_CRITICAL("Update: forcing target for ship=0x{:X}, target=0x{:X}",
                             reinterpret_cast<uintptr_t>(pShip),
                             reinterpret_cast<uintptr_t>(pCached));
            __try {
                pShip->Target = pCached;
                pShip->QueueMission(Mission::Attack, false);
                pShip->NextMission();
                LUA_LOG_CRITICAL("Update: forced target done");
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
