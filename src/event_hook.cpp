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
// Константы (подтверждённый адрес UnitClass::Active_Click_With для 1.001).
// ---------------------------------------------------------------------------
constexpr uintptr_t kActiveClickWithAddr = 0x00738890; // UnitClass::Active_Click_With

// ActionType. Значение Attack=0x0E из переписки; если оно окажется неверным,
// безусловный диагностический лог в Hooked_ActiveClickWith покажет реальное число.
constexpr int kActionAttack = 0x0E;

using ActiveClickWith_t = void(__fastcall*)(void*, int, void*);
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
// Хук: UnitClass::Active_Click_With(ActionType action, ObjectClass* pTarget).
// Срабатывает при ЛЮБОМ клике игрока по юниту (движение/атака/захват и т.д.).
// ---------------------------------------------------------------------------
void __fastcall Hooked_ActiveClickWith(void* pThis, void* /*edx*/, int action, void* pTarget) {
    // ==== ДИАГНОСТИКА СРАБАТЫВАНИЯ ХУКА ====
    // Безусловный лог: каждый клик по юниту Должен дать строку здесь.
    // Если её нет — адрес 0x738890 неверный для этой сборки (см. шаг 7: FootClass 0x4D74E0).
    unsigned int actionU = static_cast<unsigned int>(action);
    LUA_LOG_INFO("[EventHook] ActiveClickWith called, action=0x{:X}", actionU);
    // =======================================

    // Приказ атаки: action == Attack(0x0E), цель — живое техно, корабль — спаунер.
    if (actionU == static_cast<unsigned int>(kActionAttack)) {
        UnitClass* pUnit = static_cast<UnitClass*>(pThis); // Дредноут/Авианосец — UnitClass
        TechnoClass* pShip = static_cast<TechnoClass*>(pUnit);
        TechnoClass* pTargetTechno = ObjectToTechno(pTarget);

        if (IsSpawnerShip(pShip) && IsLiveTechno(pTargetTechno)) {
            g_PlayerTargetOverride[pShip] = TargetClass(static_cast<AbstractClass*>(pTarget));
            LUA_LOG_INFO("[EventHook] player Attack order via ActiveClickWith: '{}' -> '{}' cached",
                         SafeTechnoName(pShip), SafeTechnoName(pTargetTechno));
        }
    } else {
        // Двигаемся / другой приказ — не атака. Логируем только для понятности, без спама.
    }

    // Всегда вызываем оригинал — клик обрабатывается движком как обычно.
    if (g_pOriginalActiveClickWith) {
        g_pOriginalActiveClickWith(pThis, action, pTarget);
    }
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
    for (auto it = g_PlayerTargetOverride.begin(); it != g_PlayerTargetOverride.end();) {
        TechnoClass* pShip = it->first;
        TargetClass cachedTarget = it->second;

        // 1. Корабль-владелец мёртв/невалиден — удаляем запись.
        if (!IsSpawnerShip(pShip)) {
            it = g_PlayerTargetOverride.erase(it);
            continue;
        }

        // 2. Кэшированная цель мертва/невалидна — удаляем запись.
        TechnoClass* pCached = TargetClassToTechno(cachedTarget);
        if (!IsLiveTechno(pCached)) {
            it = g_PlayerTargetOverride.erase(it);
            continue;
        }

        // 3. Окно Rearm/Guard: движок пытается перецелиться. Возвращаем на cached.
        AbstractClass* pCurrent = nullptr;
        __try { pCurrent = pShip->Target; }
        __except (EXCEPTION_EXECUTE_HANDLER) { pCurrent = nullptr; }

        if (pCurrent != pCached) {
            __try {
                pShip->Target = pCached;
                pShip->QueueMission(Mission::Attack, false);
                pShip->NextMission();
                LUA_LOG_INFO("[EventHook] '{}' drift detected -> forcing back to '{}'",
                             SafeTechnoName(pShip), SafeTechnoName(pCached));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        ++it;
    }
}

void ClearAll() {
    g_PlayerTargetOverride.clear();
}

size_t OverrideCount() {
    return g_PlayerTargetOverride.size();
}

} // namespace EventHook
} // namespace LuaAPI
