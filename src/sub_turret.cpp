#include "sub_turret.h"

#include <LuaAPI/logger.hpp>
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

// Безопасное получение строкового ID цели для подробного логирования.
// Никогда не разыменовывает ненадёжные указатели за пределами __try.
static const char* SafeTechnoId(AbstractClass* pObj) {
    if (!pObj) return "nil";
    __try {
        auto what = pObj->WhatAmI();
        if (what == AbstractType::Building ||
            what == AbstractType::Unit ||
            what == AbstractType::Infantry ||
            what == AbstractType::Aircraft) {
            return static_cast<TechnoClass*>(pObj)->GetType()->get_ID();
        }
        return "cell/other";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
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

    // Обе ракеты/истребителя залпа.
    auto* node0 = pSpawn->SpawnedNodes.GetItem(0);
    auto* node1 = pSpawn->SpawnedNodes.GetItem(1);
    if (!node0 || !node1 || !node0->Unit || !node1->Unit) return;
    if (!IsValidTechno(node0->Unit) || !IsValidTechno(node1->Unit)) return;

    // Перехватываем ракету №2 СТРОГО на старте из шахты (TakeOff). В полёте (Attacking)
    // не вмешиваемся, чтобы не бороться с нативным приказом.
    if (node1->Status != SpawnNodeStatus::TakeOff) {
        return;
    }

    AircraftClass* pMissile = node1->Unit;

    // ONE-SHOT DECOUPLING LATCH: ракета уже отвязана от SpawnManager
    // (pMissile->SpawnOwner == nullptr) — значит мы уже перенаправили её в этом залпе.
    // Повторно её НЕ трогаем, иначе каждая итерация TakeOff заново дёргает
    // RocketLocomotor (опасно для предрассчитанной сплайн-траектории) и спамит лог.
    // SpawnManager ставит SpawnOwner при создании снаряда, поэтому это надёжный
    // однократный признак "уже отвязана".
    if (pMissile->SpawnOwner == nullptr) {
        return;
    }

    // Главная цель: у первого уже летящего узла берём его собственную цель,
    // а при её отсутствии — цель самого корабля (куда игрок отдал приказ атаки).
    AbstractClass* primaryTarget = node0->Unit->Target ? node0->Unit->Target : pTechno->Target;
    if (!primaryTarget) return;

    // Идемпотентность: ракета уже перенаправлена (её цель отличается от главной) —
    // СРАЗУ выходим, не пересчитывая вторичную цель и не сбрасывая состояние локомотора.
    // Статус TakeOff длится десятки кадров, поэтому без этого гарда мы бы спамили и
    // перещёлкивали летящую ракету ~50 раз в секунду.
    if (pMissile->Target && pMissile->Target != primaryTarget) {
        return;
    }

    CoordStruct targetPos;
    __try {
        targetPos = primaryTarget->GetCoords();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    constexpr long long kSplitSearchRadius = 12LL * 256;     // радиус поиска 12 клеток
    constexpr long long kSplitSearchRadiusSq = kSplitSearchRadius * kSplitSearchRadius;

    // Ищем ближайшего соседнего врага вокруг главной цели.
    // ВАЖНО: координаты в лептонах; квадрат разности на больших картах превышает 2^31,
    // поэтому считаем в 64-битном — иначе переполнение signed int даёт отрицательный `d`
    // и поиск цели ломается (см. постмортем "Integer Overflow in Distance Math").
    TechnoClass* secondaryTarget = nullptr;
    long long bestDistSq = kSplitSearchRadiusSq;
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

        long long dx = static_cast<long long>(cPos.X) - targetPos.X;
        long long dy = static_cast<long long>(cPos.Y) - targetPos.Y;
        long long d = dx * dx + dy * dy;
        if (d <= kSplitSearchRadiusSq && d < bestDistSq) {
            bestDistSq = d;
            secondaryTarget = candidate;
        }
    }

    if (!secondaryTarget) {
        return;
    }

    // Доп. защита от повторов (на случай смены вторичной цели между кадрами).
    if (pMissile->Target == secondaryTarget) {
        return;
    }

    // ОТВЯЗКА ракеты №2 от родительского SpawnManager: нативный SpawnManagerClass::AI()
    // каждый кадр принудительно перезаписывает Destination/Target всех запущенных ракет
    // обратно на главную цель (Owner->Target), из-за чего наш выбор второй цели тут же
    // сбрасывается движком на первое здание. Поэтому отвязываем ракету и отдаём ей
    // независимый боевой приказ.
    // ПРИМЕЧАНИЕ: методов Assign_Target / Assign_Destination в этой сборке НЕТ — их
    // нативными эквивалентами являются TechnoClass::SetTarget (vtable 0x6FCDB0) и
    // TechnoClass::SetDestination; принудительный пересчёт миссии — QueueMission.
    __try {
        // 1. Отвязываем ракету от родительского SpawnManager, чтобы он не сбрасывал её координаты!
        pMissile->SpawnOwner = nullptr;
        node1->Unit = nullptr;                   // менеджер больше не управляет этой ракетой
        // Слот без юнита: Dead (respawning) — безопасное "вакантное" состояние,
        // чтобы AI менеджера не пытался запустить нулевой юнит (Idle подразумевает
        // припаркованный юнит и рискован бы).
        node1->Status = SpawnNodeStatus::Dead;

        // 2. Назначаем вторую цель и отдаём независимый боевой приказ.
        CoordStruct targetCoords = secondaryTarget->GetCoords();
        pMissile->Target = secondaryTarget;
        pMissile->SetTarget(secondaryTarget);
        pMissile->SetDestination(secondaryTarget, true);

        // 3. Принудительный пересчёт полётного шага AircraftClass: QueueMission +
        //    NextMission() заставляют движок сбросить старый полётный шаг и проложить
        //    новый курс к цели. (Поля NavList в этой сборке НЕТ — нативный эквивалент
        //    сброса навигации — перезапуск миссии атаки через NextMission, а не
        //    NavList.Clear()/AddItem.)
        pMissile->QueueMission(Mission::Attack, false);
        pMissile->NextMission();

        // 4. Принудительно передаём локомотору физические 3D-координаты второй цели.
        //    Иначе RocketLocomotor продолжает лететь по старой параболе к главной цели.
        //    (Метода Fly_To в этой сборке нет — нативный эквивалент —
        //    ILocomotion/LocomotionClass::Force_Immediate_Destination, 0x55AC00.)
        static_cast<FootClass*>(pMissile)->Locomotor->Force_Immediate_Destination(targetCoords);

        LUA_LOG_INFO("[SplitMissile] NavList reset -> Missile #2 homing to '{}' at ({}, {})!",
                     SafeTechnoId(secondaryTarget),
                     targetCoords.X / 256, targetCoords.Y / 256);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LUA_LOG_WARN("[SplitMissile] '{}' SEH while decoupling missile#2",
                     SafeTechnoId(pTechno));
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
    m_primaryAttackTarget.erase(pTechno);

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
    // Чистим кэш главных целей: мёртвый таргет или мёртвый корабль-владелец.
    for (auto cit = m_primaryAttackTarget.begin(); cit != m_primaryAttackTarget.end();) {
        if (cit->first == pDeadTarget || cit->second == pDeadTarget) {
            cit = m_primaryAttackTarget.erase(cit);
        } else {
            ++cit;
        }
    }
}

void SubTurretManager::ClearAll() {
    m_turrets.clear();
    m_pendingRemovals.clear();
    m_primaryAttackTarget.clear();
    m_isUpdating = false;
}

// Персистентная фиксация главной цели атаки (Dreadnought / Aircraft-carrier).
// Нативный AI после расхода боезапаса (Rearm/Guard) перебирает ближайшую цель и может
// переключить корабль на соседнее здание. Мы храним явно заданную игроком цель и, пока
// она жива, возвращаем корабль на неё. Записи в кэш делаются ВНЕ __try, чтобы C++-исключения
// (аллокация unordered_map) не пересекали SEH-блоки.
void SubTurretManager::ManagePrimaryAttackTarget(TechnoClass* pTechno) {
    if (!pTechno) return;

    // Для spawner-юнитов (Дредноут/Авианосец) НЕ используем QueueMission(Attack),
    // потому что это вызывает краш в нативной логике SpawnManager.
    // Вместо этого только удерживаем pTechno->Target.
    bool isSpawner = false;
    __try {
        isSpawner = (pTechno->SpawnManager != nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    // --- Защищённое чтение миссии и текущей цели ---
    Mission mission = Mission::None;
    AbstractClass* pCurrent = nullptr;
    __try {
        mission = pTechno->CurrentMission;
        pCurrent = pTechno->Target;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    auto it = m_primaryAttackTarget.find(pTechno);
    TechnoClass* cached = (it != m_primaryAttackTarget.end()) ? it->second : nullptr;

    // 1. Корабль в состоянии атаки: принимаем его текущую цель как явный приказ игрока
    //    (сигнал «игрок отдал новый приказ атаки») и обновляем кэш.
    if (mission == Mission::Attack) {
        TechnoClass* asTechno = nullptr;
        if (pCurrent) {
            int what = -1;
            __try { what = static_cast<int>(pCurrent->WhatAmI()); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return; }
            if (what == static_cast<int>(AbstractType::Building) ||
                what == static_cast<int>(AbstractType::Unit) ||
                what == static_cast<int>(AbstractType::Infantry) ||
                what == static_cast<int>(AbstractType::Aircraft)) {
                asTechno = static_cast<TechnoClass*>(pCurrent);
            }
        }
        if (asTechno && IsValidTechno(asTechno) && asTechno != cached) {
            m_primaryAttackTarget[pTechno] = asTechno;
            LUA_LOG_INFO("[PrimaryTarget] '{}' accepted new order -> '{}'",
                         SafeTechnoId(pTechno), SafeTechnoId(asTechno));
        }
        return;
    }

    // 2. Возврат на закэшированную цель — только в Guard/Sleep (то самое окно Rearm,
    //    когда нативный AI пытается перецелиться). Прочие миссии не трогаем.
    if (mission != Mission::Guard && mission != Mission::Sleep) {
        return;
    }

    // 3. Кэш пуст — форсить нечего.
    if (!cached) return;

    // 4. Закэшированная цель мертва/невалидна — очищаем запись; юнит вправе сам выбрать цель.
    if (!IsValidTechno(cached)) {
        m_primaryAttackTarget.erase(pTechno);
        return;
    }

    // 5. Корабль и так смотрит на закэшированную цель — ничего не делаем.
    if (pCurrent == cached) return;

    // 6. Живая закэшированная цель, а корабль смотрит на другую — принудительно возвращаем.
    __try {
        pTechno->Target = cached;

        if (!isSpawner) {
            // Обычные юниты: QueueMission + NextMission для возврата на цель
            pTechno->QueueMission(Mission::Attack, false);
            pTechno->NextMission();
            LUA_LOG_INFO("[PrimaryTarget] '{}' Rearm/Guard drift detected -> forcing back to '{}'",
                         SafeTechnoId(pTechno), SafeTechnoId(cached));
        } else {
            // Spawner-юниты (DRED/HORNET): только удерживаем Target, без QueueMission
            // (QueueMission вызывает краш в нативной логике SpawnManager для зданий)
            LUA_LOG_INFO("[PrimaryTarget] '{}' (spawner) holding target '{}'",
                         SafeTechnoId(pTechno), SafeTechnoId(cached));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
        if (!IsValidTechno(pTechno)) {
            m_primaryAttackTarget.erase(pTechno);   // корабль уничтожен — чистим кэш
            continue;
        }

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

        // 3. Персистентная фиксация главной цели атаки (возврат после Rearm/Guard).
        ManagePrimaryAttackTarget(pTechno);
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

    // Dynamic Salvo Convergence: собираем до двух живых вражеских целей (A и B).
    // IsValidTechno() SEH-защищён и проверяет IsAlive/Health>0/!InLimbo, поэтому
    // виртуальные вызовы и поля мёртвых/висячих указателей не трогаем (Trap #2).
    TechnoClass* targetA = nullptr;
    TechnoClass* targetB = nullptr;
    for (TechnoClass* cand : targets) {
        if (!cand) continue;
        if (!IsValidTechno(cand)) continue;
        if (cand->Owner == pTechno->Owner) continue;
        if (!targetA) targetA = cand;
        else if (!targetB) targetB = cand;
        else break;
    }

    // Если цель A уничтожена/пуста, обе башни сходятся на B, и наоборот.
    if (!targetA && !targetB) {
        for (auto& turret : *turrets) {
            turret.target = nullptr;
        }
        return false;
    }

    for (size_t i = 0; i < turrets->size(); ++i) {
        TechnoClass* assign = nullptr;
        if (i == 0) {
            assign = targetA ? targetA : targetB;          // Башня 0 -> A (или B, если A мёртв)
        } else if (i == 1) {
            assign = targetB ? targetB : targetA;          // Башня 1 -> B (или A, если B мёртв)
        } else {
            assign = targetA ? targetA : targetB;          // доп. башни сходятся на первой живой цели
        }
        (*turrets)[i].target = assign;
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
        // Dynamic Salvo Convergence: берём первую ЖИВУЮ цель из распределённых башен.
        // Если TargetA погибла до/в момент пуска, корабль наводится на живую TargetB —
        // залп физически не уходит на мёртвое здание.
        TechnoClass* validTarget = nullptr;
        for (size_t i = 0; i < turrets->size(); ++i) {
            auto& turret = (*turrets)[i];
            if (turret.target && IsValidTechno(turret.target)) {
                validTarget = turret.target;
                break;
            }
        }
        if (!validTarget) return false;

        __try {
            pTechno->SetTarget(validTarget);
            pTechno->SetDestination(validTarget, true);
            pTechno->QueueMission(Mission::Attack, true);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
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
