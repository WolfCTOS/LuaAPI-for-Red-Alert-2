#include "sub_turret.h"

#include <SpawnManagerClass.h>
#include <AircraftClass.h>

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

// ==== Разделение залпа для НАСТОЯЩИХ физических ракет (SpawnManager) ====
// Дредноут (DMISL) и Авианосец (HORNET) через SpawnManagerClass запускают реальные
// летающие снаряды/истребители. Ракета №1 летит в главную цель, а ракета №2
// перенаправляется на ближайшего соседнего врага вокруг главной цели (радиус 12 клеток).
static void ProcessSpawnedMissiles(TechnoClass* pTechno) {
    if (!pTechno || !IsValidTechno(pTechno)) return;

    // Проверяем, есть ли у корабля менеджер ракет (Дредноут / Авианосец).
    SpawnManagerClass* pSpawn = pTechno->SpawnManager;
    if (!pSpawn || pSpawn->SpawnedNodes.Count < 2) return;

    // Главная цель: у первого уже летящего узла берём его собственную цель,
    // а при её отсутствии — цель самого корабля (куда игрок отдал приказ атаки).
    SpawnControl* node0 = pSpawn->SpawnedNodes.GetItem(0);
    if (!node0 || !node0->Unit || !IsValidTechno(node0->Unit)) return;

    AbstractClass* primaryTarget = node0->Unit->Target ? node0->Unit->Target : pTechno->Target;
    if (!primaryTarget) return;

    CoordStruct targetPos;
    __try {
        targetPos = primaryTarget->GetCoords();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    constexpr int kSplitSearchRadius = 12 * 256;       // радиус поиска 12 клеток
    constexpr int kSplitSearchRadiusSq = kSplitSearchRadius * kSplitSearchRadius;

    // Ищем ближайшего соседнего врага вокруг главной цели.
    TechnoClass* secondaryTarget = nullptr;
    int bestDistSq = kSplitSearchRadiusSq;
    for (int i = 0; i < TechnoClass::Array.Count; ++i) {
        TechnoClass* candidate = TechnoClass::Array.GetItem(i);
        if (!candidate || !IsValidTechno(candidate)) continue;
        if (candidate == primaryTarget || candidate == pTechno) continue;
        if (candidate->Owner == pTechno->Owner) continue;

        CoordStruct cPos;
        bool ok = false;
        __try { cPos = candidate->GetCoords(); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (!ok) continue;

        int dx = cPos.X - targetPos.X;
        int dy = cPos.Y - targetPos.Y;
        int d = dx * dx + dy * dy;
        if (d <= kSplitSearchRadiusSq && d < bestDistSq) {
            bestDistSq = d;
            secondaryTarget = candidate;
        }
    }
    if (!secondaryTarget) return;

    // Ракета №1 летит в главную цель; ракета №2 (node[1]), уже в воздухе, перенаправляется.
    SpawnControl* node1 = pSpawn->SpawnedNodes.GetItem(1);
    if (node1 && node1->Unit && IsValidTechno(node1->Unit)) {
        AircraftClass* pMissile = node1->Unit;

        // Вторая ракета, находящаяся в полёте (взлёт/атака) на цель.
        if (node1->Status == SpawnNodeStatus::TakeOff ||
            node1->Status == SpawnNodeStatus::Attacking) {
            __try {
                // Перенаправляем только если снаряд всё ещё летит в главную цель.
                // ВАЖНО: обновляем цель у самого юнита (в этой сборке у SpawnControl
                // нет поля Target — цель узла хранится в node->Unit->Target), иначе
                // нативный SpawnManagerClass::Update() перезапишет её обратно на primary.
                if (pMissile->Target != secondaryTarget) {
                    pMissile->Target = secondaryTarget;
                    pMissile->SetTarget(secondaryTarget);
                    pMissile->SetDestination(secondaryTarget, true);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
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
    data.baseRof = (rof > 0) ? rof : 90;
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

    // 1. Обслуживание суб-турелей (создаются ИСКЛЮЧИТЕЛЬНО из Lua):
    //    только кулдаун и плавное вращение. Выбор цели и стрельба — из Lua.
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

            // Кулдаун перезарядки
            if (turret.rofTimer > 0) {
                turret.rofTimer--;
            }

            // Плавное вращение facing в сторону targetFacing со скоростью rot.
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

    // 2. Перехват запущенных физических ракет/истребителей всех кораблей со SpawnManager.
    for (int i = 0; i < TechnoClass::Array.Count; ++i) {
        TechnoClass* pObj = TechnoClass::Array.GetItem(i);
        if (pObj && IsValidTechno(pObj) && pObj->SpawnManager) {
            ProcessSpawnedMissiles(pObj);
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

    turret.rofTimer = turret.baseRof;

    // Корабль с ракетным спавном (Дредноут/Авианосец): НЕ вызываем ReceiveDamage и не
    // наносим мгновенный урон вручную — иначе получим ДВОЙНОЙ урон (double damage).
    // Урон наносят только сами летающие ракеты (DMISL/HORNET) при физическом падении
    // на цель. Здесь лишь выставляем кулдаун; нативный боевой приказ выдаёт
    // FireSplitSalvo(), а перехват разделения ракет делает ProcessSpawnedMissiles().
    if (pTechno->SpawnManager) {
        return true;
    }

    // Обычная башня без ракетного спавна: мгновенный урон по готовности.
    WarheadTypeClass* pWH = WarheadTypeClass::Find("AP");
    if (!pWH && WarheadTypeClass::Array.Count > 0) {
        pWH = WarheadTypeClass::Array.GetItem(0);
    }
    if (!pWH) return false;

    int damage = 50;

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

    // Дредноут / Авианосец (ракетный спавн): вместо скрытого урона через ReceiveDamage
    // отдаём РОДНОЙ нативный боевой приказ атаки. Корабль открывает люки шахт и физически
    // запускает залп ракет (DMISL / HORNET) со звуком и летящими снарядами.
    if (pTechno->SpawnManager) {
        for (size_t i = 0; i < turrets->size(); ++i) {
            auto& turret = (*turrets)[i];
            if (turret.target && IsValidTechno(turret.target)) {
                __try {
                    pTechno->SetTarget(turret.target);
                    pTechno->SetDestination(turret.target, true);
                    pTechno->QueueMission(Mission::Attack, true);
                    return true;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    return false;
                }
            }
        }
        return false;
    }

    // Обычные башни без ракетного спавна: принудительный пуск по назначенным целям.
    bool anyFired = false;
    for (size_t i = 0; i < turrets->size(); ++i) {
        auto& turret = (*turrets)[i];
        if (turret.target && IsValidTechno(turret.target)) {
            if (FireTurret(pTechno, i, turret.target)) {
                anyFired = true;
            }
        }
    }
    return anyFired;
}

} // namespace LuaAPI
