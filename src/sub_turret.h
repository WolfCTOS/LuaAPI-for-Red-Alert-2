#pragma once
#include <YRPP.h>
#include <vector>
#include <unordered_map>
#include <cmath>

namespace LuaAPI {

struct SubTurretData {
    int voxelSection = 1;
    CoordStruct offset{0, 0, 0};
    int facing = 0;
    int targetFacing = 0;
    int rot = 8;
    int rofTimer = 0;
    int baseRof = 45;
    TechnoClass* target = nullptr;
};

class SubTurretManager {
public:
    static SubTurretManager& Instance();

    bool AddTurret(TechnoClass* pTechno, int section, int offX, int offY, int offZ, int rot, int rof);
    std::vector<SubTurretData>* GetTurrets(TechnoClass* pTechno);
    void RemoveTechno(TechnoClass* pTechno);
    void ClearAll();

    void UpdateAll();
    bool FireTurret(TechnoClass* pTechno, size_t turretIndex, TechnoClass* pTarget);

private:
    std::unordered_map<TechnoClass*, std::vector<SubTurretData>> m_turrets;
};

} // namespace LuaAPI
