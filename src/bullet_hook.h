#pragma once

class BulletClass;

namespace LuaAPI::BulletHook {

struct ImpactEvent {
    BulletClass* bullet;
    int x;
    int y;
    int z;
};

// Устанавливает native hook на BulletClass::Detonate.
bool Install();

// Очищает состояние hook между игровыми сессиями.
void Clear();

// Регистрирует BulletClass*, созданный через LuaAPI::FireProjectile.
void Register(BulletClass* bullet);

// Забирает impact-события, накопленные native hook.
// Возвращает количество событий.
int DrainImpacts(ImpactEvent* out, int maxCount);

} // namespace LuaAPI::BulletHook