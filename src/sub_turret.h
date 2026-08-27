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
    int rot = 12;
    int rofTimer = 0;
    int baseRof = 30;
    TechnoClass* target = nullptr;
};

class SubTurretManager {
public:
    static SubTurretManager& Instance();

    bool AddTurret(TechnoClass* pTechno, int section, int offX, int offY, int offZ, int rot, int rof);
    std::vector<SubTurretData>* GetTurrets(TechnoClass* pTechno);
    void RemoveTechno(TechnoClass* pTechno);
    void InvalidateTargetGlobally(TechnoClass* pDeadTarget);
    void ClearAll();

    void UpdateAll();
    bool FireTurret(TechnoClass* pTechno, size_t turretIndex, TechnoClass* pTarget);
    bool AssignSplitTargets(TechnoClass* pTechno, const std::vector<TechnoClass*>& targets);
    bool FireSplitSalvo(TechnoClass* pTechno);
    void DrawSubTurrets(TechnoClass* pTechno, Point2D* pLocation, RectangleStruct* pBounds);

    void InitDrawHook();

private:
    std::unordered_map<TechnoClass*, std::vector<SubTurretData>> m_turrets;
    std::vector<TechnoClass*> m_pendingRemovals;
    bool m_isUpdating = false;
};

} // namespace LuaAPI
