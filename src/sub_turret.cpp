#include "sub_turret.h"

namespace LuaAPI {

// Локальная RTTI-проверка валидности указателя на юнит
static bool IsValidTechno(TechnoClass* ptr) {
    if (!ptr) return false;
    auto what = ptr->WhatAmI();
    if (what != AbstractType::Building &&
        what != AbstractType::Unit &&
        what != AbstractType::Infantry &&
        what != AbstractType::Aircraft) {
        return false;
    }
    return ptr->IsAlive && ptr->Health > 0;
}

SubTurretManager& SubTurretManager::Instance() {
    static SubTurretManager s_instance;
    return s_instance;
}

bool SubTurretManager::AddTurret(TechnoClass* pTechno, int section, int offX, int offY, int offZ, int rot, int rof) {
    if (!IsValidTechno(pTechno)) return false;

    SubTurretData data;
    data.voxelSection = section;
    data.offset = CoordStruct{offX, offY, offZ};
    data.facing = 0;
    data.targetFacing = 0;
    data.rot = (rot > 0) ? rot : 8;
    data.baseRof = (rof > 0) ? rof : 45;
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
    if (pTechno) {
        m_turrets.erase(pTechno);
    }
}

void SubTurretManager::ClearAll() {
    m_turrets.clear();
}

void SubTurretManager::UpdateAll() {
    for (auto it = m_turrets.begin(); it != m_turrets.end(); ) {
        TechnoClass* pTechno = it->first;
        if (!IsValidTechno(pTechno)) {
            it = m_turrets.erase(it);
            continue;
        }

        for (auto& turret : it->second) {
            if (turret.rofTimer > 0) {
                turret.rofTimer--;
            }

            if (turret.target && IsValidTechno(turret.target)) {
                CoordStruct myPos = pTechno->GetCoords();
                CoordStruct targetPos = turret.target->GetCoords();

                double dx = static_cast<double>(targetPos.X - myPos.X);
                double dy = static_cast<double>(targetPos.Y - myPos.Y);

                double angleRad = std::atan2(dy, dx);
                int desiredFacing = static_cast<int>((angleRad / (2.0 * 3.1415926535)) * 256.0) & 0xFF;
                turret.targetFacing = desiredFacing;

                int diff = (turret.targetFacing - turret.facing) & 0xFF;
                if (diff != 0) {
                    if (diff <= 128) {
                        turret.facing = (turret.facing + (std::min)(turret.rot, diff)) & 0xFF;
                    } else {
                        turret.facing = (turret.facing - (std::min)(turret.rot, 256 - diff)) & 0xFF;
                    }
                }
            }
        }
        ++it;
    }
}

bool SubTurretManager::FireTurret(TechnoClass* pTechno, size_t turretIndex, TechnoClass* pTarget) {
    if (!IsValidTechno(pTechno) || !IsValidTechno(pTarget)) return false;

    auto* turrets = GetTurrets(pTechno);
    if (!turrets || turretIndex >= turrets->size()) return false;

    auto& turret = (*turrets)[turretIndex];
    if (turret.rofTimer > 0) return false;

    int damage = 40;
    pTarget->ReceiveDamage(&damage, 0, nullptr, nullptr, false, false, pTechno->Owner);

    turret.rofTimer = turret.baseRof;
    return true;
}

} // namespace LuaAPI
