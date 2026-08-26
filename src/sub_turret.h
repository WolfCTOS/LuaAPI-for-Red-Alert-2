#pragma once
#include <YRPP.h>
#include <vector>
#include <unordered_map>
#include <cmath>

namespace LuaAPI {

struct SubTurretData {
    int voxelSection = 1;        // Индекс секции в .vxl (1, 2, 3...)
    CoordStruct offset{0, 0, 0}; // 3D-оффсет в лептонах
    int facing = 0;              // Текущий угол (0..255)
    int targetFacing = 0;        // Целевой угол на цель (0..255)
    int rot = 8;                 // Скорость поворота
    int rofTimer = 0;            // Кулдаун
    int baseRof = 45;            // Базовая перезарядка
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
    void DrawSubTurrets(TechnoClass* pTechno, Point2D* pLocation, RectangleStruct* pBounds);

    void InitDrawHook();

private:
    std::unordered_map<TechnoClass*, std::vector<SubTurretData>> m_turrets;
};

} // namespace LuaAPI
