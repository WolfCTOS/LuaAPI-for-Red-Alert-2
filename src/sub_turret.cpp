#include "sub_turret.h"

namespace LuaAPI {

static bool IsValidTechno(TechnoClass* ptr) {
    if (!ptr) return false;

    __try {
        // Защита от поврежденной/обнуленной таблицы виртуальных функций (VTable)
        auto what = ptr->WhatAmI();
        if (what != AbstractType::Building &&
            what != AbstractType::Unit &&
            what != AbstractType::Infantry &&
            what != AbstractType::Aircraft) {
            return false;
        }

        if (!ptr->IsAlive || ptr->Health <= 0 || ptr->InLimbo) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Если объект уже разрушен в памяти и vtable стёрта — безопасно возвращаем false
        return false;
    }
}

// SEH-защищённый расчёт наведения. Доступ к координатам цели и углам обёрнут в __try,
// чтобы висячий указатель (порванная vtable) не приводил к Access Violation.
// Функция использует только POD-типы, поэтому SEH применяется без проблем unwinding.
static bool SafeComputeAim(TechnoClass* pTarget, const CoordStruct& myPos,
                           int currentFacing, int* pDesiredFacing, int* pDiff) {
    if (!pTarget || !pDesiredFacing || !pDiff) return false;
    __try {
        CoordStruct targetPos = pTarget->GetCoords();
        double dx = static_cast<double>(targetPos.X - myPos.X);
        double dy = static_cast<double>(targetPos.Y - myPos.Y);
        double angleRad = std::atan2(dy, dx);
        int desiredFacing = static_cast<int>((angleRad / (2.0 * 3.1415926535)) * 256.0) & 0xFF;
        *pDesiredFacing = desiredFacing;
        *pDiff = (desiredFacing - currentFacing) & 0xFF;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

SubTurretManager& SubTurretManager::Instance() {
    static SubTurretManager s_instance;
    return s_instance;
}

void SubTurretManager::InitDrawHook() {
}

bool SubTurretManager::AddTurret(TechnoClass* pTechno, int section, int offX, int offY, int offZ, int rot, int rof) {
    if (!IsValidTechno(pTechno)) return false;

    SubTurretData data;
    data.voxelSection = section;
    data.offset = CoordStruct{offX, offY, offZ};
    data.facing = 0;
    data.targetFacing = 0;
    data.rot = (rot > 0) ? rot : 12;
    data.baseRof = (rof > 0) ? rof : 30;
    data.rofTimer = 0;
    data.target = nullptr;

    m_turrets[pTechno].push_back(data);
    return true;
}

std::vector<SubTurretData>* SubTurretManager::GetTurrets(TechnoClass* pTechno) {
    if (!pTechno) return nullptr;
    auto it = m_turrets.find(pTechno);
    return (it != m_turrets.end()) ? &it->second : nullptr;
}

void SubTurretManager::RemoveTechno(TechnoClass* pTechno) {
    if (!pTechno) return;
    
    // Мгновенная глобальная инвалидация целей
    InvalidateTargetGlobally(pTechno);

    if (m_isUpdating) {
        m_pendingRemovals.push_back(pTechno);
    } else {
        m_turrets.erase(pTechno);
    }
}

void SubTurretManager::InvalidateTargetGlobally(TechnoClass* pDeadTarget) {
    if (!pDeadTarget) return;
    for (auto& pair : m_turrets) {
        for (auto& turret : pair.second) {
            if (turret.target == pDeadTarget) {
                turret.target = nullptr;
            }
        }
    }
}

void SubTurretManager::ClearAll() {
    m_turrets.clear();
    m_pendingRemovals.clear();
    m_isUpdating = false;
}

void SubTurretManager::UpdateAll() {
    m_isUpdating = true;

    // Башни создаются ТОЛЬКО из Lua (AddSubTurret / auto-attach через мод).
    // Здесь движок лишь обслуживает уже зарегистрированные башни.
    std::vector<TechnoClass*> activeUnits;
    activeUnits.reserve(m_turrets.size());
    for (const auto& pair : m_turrets) {
        activeUnits.push_back(pair.first);
    }

    for (TechnoClass* pTechno : activeUnits) {
        if (!IsValidTechno(pTechno)) continue;

        auto* turrets = GetTurrets(pTechno);
        if (!turrets) continue;

        CoordStruct myPos = pTechno->GetCoords();

        for (size_t tIdx = 0; tIdx < turrets->size(); ++tIdx) {
            auto& turret = (*turrets)[tIdx];

            // 1. Кулдаун перезарядки
            if (turret.rofTimer > 0) {
                turret.rofTimer--;
            }

            // 2. Плавное вращение facing в сторону targetFacing со скоростью rot.
            //    Выбор цели и стрельба выполняются ИСКЛЮЧИТЕЛЬНО из Lua.
            if (turret.target && IsValidTechno(turret.target)) {
                int desiredFacing = 0;
                int diff = 0;
                if (SafeComputeAim(turret.target, myPos, turret.facing, &desiredFacing, &diff)) {
                    turret.targetFacing = desiredFacing;

                    if (diff != 0) {
                        if (diff <= 128) {
                            turret.facing = (turret.facing + (std::min)(turret.rot, diff)) & 0xFF;
                        } else {
                            turret.facing = (turret.facing - (std::min)(turret.rot, 256 - diff)) & 0xFF;
                        }
                    }
                } else {
                    // Цель повреждена/висячий указатель — перестаём её преследовать
                    turret.target = nullptr;
                }
            }
        }
    }

    m_isUpdating = false;

    if (!m_pendingRemovals.empty()) {
        for (TechnoClass* deadObj : m_pendingRemovals) {
            m_turrets.erase(deadObj);
        }
        m_pendingRemovals.clear();
    }
}

void SubTurretManager::DrawSubTurrets(TechnoClass* pTechno, Point2D* pLocation, RectangleStruct* pBounds) {
}

bool SubTurretManager::FireTurret(TechnoClass* pTechno, size_t turretIndex, TechnoClass* pTarget) {
    if (!IsValidTechno(pTechno) || !IsValidTechno(pTarget)) return false;

    auto* turrets = GetTurrets(pTechno);
    if (!turrets || turretIndex >= turrets->size()) return false;

    auto& turret = (*turrets)[turretIndex];
    if (turret.rofTimer > 0) return false;

    WarheadTypeClass* pWH = WarheadTypeClass::Find("AP");
    if (!pWH && WarheadTypeClass::Array.Count > 0) {
        pWH = WarheadTypeClass::Array.GetItem(0);
    }
    if (!pWH) return false;

    int damage = 50;
    turret.rofTimer = turret.baseRof;

    __try {
        // Проверяем результат выстрела: если цель погибла (NowDead/Dead) — МГНОВЕННЫЙ BAIL!
        DamageState result = pTarget->ReceiveDamage(&damage, 0, pWH, pTechno, false, false, pTechno->Owner);
        
        // Если цель уничтожена (Health <= 0 или DamageState == Dead) — мгновенно инвалидируем её у ВСЕХ турелей!
        if (result == DamageState::NowDead || !IsValidTechno(pTarget)) {
            InvalidateTargetGlobally(pTarget);
            return true; // Мгновенный выход, не трогаем память!
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InvalidateTargetGlobally(pTarget);
        return false;
    }

    return true;
}

bool SubTurretManager::AssignSplitTargets(TechnoClass* pTechno, const std::vector<TechnoClass*>& targets) {
    if (!IsValidTechno(pTechno) || targets.empty()) return false;

    auto* turrets = GetTurrets(pTechno);
    if (!turrets || turrets->empty()) return false;

    // Распределяем цели по свободным башням (1 цель на 1 башню)
    for (size_t i = 0; i < turrets->size(); ++i) {
        if (i < targets.size() && IsValidTechno(targets[i]) && targets[i]->Owner != pTechno->Owner) {
            (*turrets)[i].target = targets[i];
        } else {
            (*turrets)[i].target = nullptr;
        }
    }
    return true;
}

bool SubTurretManager::FireSplitSalvo(TechnoClass* pTechno) {
    if (!IsValidTechno(pTechno)) return false;

    auto* turrets = GetTurrets(pTechno);
    if (!turrets || turrets->empty()) return false;

    bool anyFired = false;
    for (size_t i = 0; i < turrets->size(); ++i) {
        auto& turret = (*turrets)[i];
        if (turret.target && IsValidTechno(turret.target)) {
            // Принудительный пуск башни по своей назначенной цели
            if (FireTurret(pTechno, i, turret.target)) {
                anyFired = true;
            }
        }
    }
    return anyFired;
}

} // namespace LuaAPI
