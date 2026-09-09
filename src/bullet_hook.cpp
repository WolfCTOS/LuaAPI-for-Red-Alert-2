#include "bullet_hook.h"

#include <TechnoClass.h>
#include <FootClass.h>
#include <BulletClass.h>
#include <MinHook.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <windows.h>

#include <LuaAPI/logger.hpp>

namespace LuaAPI::BulletHook {

namespace {

constexpr uintptr_t kBulletDetonateAddr = 0x004690B0;

using BulletDetonateFn =
    void (__thiscall*)(BulletClass*, const CoordStruct&);

BulletDetonateFn g_originalDetonate = nullptr;

std::unordered_set<BulletClass*> g_registered;

std::vector<ImpactEvent> g_impacts;

bool IsRegistered(BulletClass* bullet)
{
    if (!bullet)
        return false;

    return g_registered.find(bullet) != g_registered.end();
}

void __fastcall Hooked_BulletDetonate(
    BulletClass* bullet,
    void* /*edx*/,
    const CoordStruct& coords)
{
    const bool tracked = IsRegistered(bullet);

    if (g_originalDetonate) {
        g_originalDetonate(
            bullet,
            coords);
    }

    if (!tracked)
        return;

    g_registered.erase(bullet);

    ImpactEvent event{};
    event.bullet = bullet;
    event.x = coords.X;
    event.y = coords.Y;
    event.z = coords.Z;

    g_impacts.push_back(event);
}

} // namespace

void Register(BulletClass* bullet)
{
    if (!bullet)
        return;

    g_registered.insert(bullet);
}

void Clear()
{
    g_registered.clear();
    g_impacts.clear();
}

int DrainImpacts(
    ImpactEvent* out,
    int maxCount)
{
    if (!out || maxCount <= 0)
        return 0;

    const int count =
        static_cast<int>(
            std::min<std::size_t>(
                g_impacts.size(),
                static_cast<std::size_t>(maxCount)));

    for (int i = 0; i < count; ++i) {
        out[i] = g_impacts[i];
    }

    g_impacts.erase(
        g_impacts.begin(),
        g_impacts.begin() + count);

    return count;
}

bool Install()
{
    DWORD oldProtect = 0;

    if (!VirtualProtect(
            reinterpret_cast<LPVOID>(
                kBulletDetonateAddr),
            64,
            PAGE_EXECUTE_READWRITE,
            &oldProtect)) {

        LUA_LOG_WARN(
            "[BulletHook] VirtualProtect(Detonate 0x{:X}) failed (error {})",
            static_cast<unsigned>(
                kBulletDetonateAddr),
            GetLastError());
    }

    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                kBulletDetonateAddr),
            reinterpret_cast<LPVOID>(
                &Hooked_BulletDetonate),
            reinterpret_cast<LPVOID*>(
                &g_originalDetonate));

    LUA_LOG_INFO(
        "[BulletHook] MH_CreateHook(Detonate @ 0x{:X}) -> {} ({})",
        static_cast<unsigned>(
            kBulletDetonateAddr),
        MH_StatusToString(status),
        static_cast<int>(status));

    if (status != MH_OK) {
        LUA_LOG_WARN(
            "[BulletHook] Detonate hook was NOT installed");

        return false;
    }

    status =
        MH_EnableHook(
            reinterpret_cast<LPVOID>(
                kBulletDetonateAddr));

    LUA_LOG_INFO(
        "[BulletHook] MH_EnableHook(Detonate) -> {} ({})",
        MH_StatusToString(status),
        static_cast<int>(status));

    if (status != MH_OK) {
        LUA_LOG_WARN(
            "[BulletHook] Detonate hook could not be enabled");

        return false;
    }

    LUA_LOG_INFO(
        "[BulletHook] Detonate hook installed successfully");

    return true;
}

} // namespace LuaAPI::BulletHook