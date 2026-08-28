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
// Константы (подтверждены в PROJECT/EventClass_MEGAMISSION_findings.md).
// EventClass (#pragma pack(1)):  Type(u8)@+0, IsExecuted(u8)@+1, HouseIndex(i8)@+2,
// Frame(u32)@+3, DataBuffer[104]@+7  (sizeof==111).
// MEGAMISSION: Whom(TargetClass)@+7, Mission(u8)@+12, Target(TargetClass)@+14.
// ---------------------------------------------------------------------------
constexpr uintptr_t kExecuteDoListAddr = 0x0064CC68;

constexpr unsigned char kEventTypeMegaMission = 0x04; // EventType::MegaMission
constexpr unsigned char kOff_Type    = 0;
constexpr unsigned char kOff_Whom    = 7;
constexpr unsigned char kOff_Mission = 12;
constexpr unsigned char kOff_Target  = 14;
constexpr unsigned char kMissionAttack = 1;           // Mission::Attack

using ExecuteDoList_t = void(__fastcall*)(void*);
ExecuteDoList_t g_pOriginalExecuteDoList = nullptr;
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

// Чтение события MegaMission из 'this' с SEH. Тип проверяется по байту @+0.
static bool ReadMegaMission(void* pEvent, unsigned char* outMission,
                            TargetClass* outWhom, TargetClass* outTarget) {
    if (!pEvent || !outMission || !outWhom || !outTarget) return false;
    __try {
        unsigned char* base = static_cast<unsigned char*>(pEvent);
        if (base[kOff_Type] != kEventTypeMegaMission) return false;
        // Whom и Target — смежные 5-байтные TargetClass в датабуфере (pack(1)).
        memcpy(outWhom, base + kOff_Whom, sizeof(TargetClass));
        memcpy(outTarget, base + kOff_Target, sizeof(TargetClass));
        *outMission = base[kOff_Mission];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
// Хук: перехватываем событие ДО обработки движком.
// ---------------------------------------------------------------------------
void __fastcall Hooked_ExecuteDoList(void* ecx_this, void* /*edx*/) {
    unsigned char mission = 0;
    TargetClass whom{};
    TargetClass target{};

    if (ReadMegaMission(ecx_this, &mission, &whom, &target) && mission == kMissionAttack) {
        TechnoClass* pShip = TargetClassToTechno(whom);
        TechnoClass* pTarget = TargetClassToTechno(target);

        if (IsSpawnerShip(pShip) && IsLiveTechno(pTarget)) {
            g_PlayerTargetOverride[pShip] = target;
            LUA_LOG_INFO("[EventHook] player Attack order: '{}' -> '{}' cached",
                         SafeTechnoName(pShip), SafeTechnoName(pTarget));
        }
    }

    // Событие движком обрабатывается всегда, независимо от нашего перехвата.
    if (g_pOriginalExecuteDoList) {
        g_pOriginalExecuteDoList(ecx_this);
    }
}

bool Install() {
    if (g_installed || g_pOriginalExecuteDoList) return true;

    MH_STATUS st = MH_CreateHook(
        reinterpret_cast<LPVOID>(kExecuteDoListAddr),
        reinterpret_cast<LPVOID>(&Hooked_ExecuteDoList),
        reinterpret_cast<LPVOID*>(&g_pOriginalExecuteDoList));
    if (st != MH_OK) {
        LUA_LOG_WARN("[EventHook] MH_CreateHook(0x{:X}) -> {}", kExecuteDoListAddr, static_cast<int>(st));
        return false;
    }

    st = MH_EnableHook(reinterpret_cast<LPVOID>(kExecuteDoListAddr));
    if (st != MH_OK) {
        LUA_LOG_WARN("[EventHook] MH_EnableHook(0x{:X}) -> {}", kExecuteDoListAddr, static_cast<int>(st));
        return false;
    }

    g_installed = true;
    LUA_LOG_INFO("[EventHook] installed on EventClass::Execute_DoList @ 0x{:X}", kExecuteDoListAddr);
    return true;
}

void Uninstall() {
    if (!g_installed) return;
    MH_DisableHook(reinterpret_cast<LPVOID>(kExecuteDoListAddr));
    MH_RemoveHook(reinterpret_cast<LPVOID>(kExecuteDoListAddr));
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
