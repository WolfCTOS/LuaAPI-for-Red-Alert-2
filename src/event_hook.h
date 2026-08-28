#pragma once
#include <YRPP.h>
#include <TargetClass.h>
#include <unordered_map>

namespace LuaAPI {

// Один hook на EventClass::Execute_DoList (gamemd 1.001 @ 0x64CC68).
//
// Назначение: отличить ЯВНЫЙ приказ атаки игрока (событие EventType::MegaMission
// + Mission::Attack) от автоматического переключения цели движком, и принудительно
// удерживать корабль-спаунер на цели, которую выбрал игрок.
//
// Модуль изолирован от SubTurretManager::ManagePrimaryAttackTarget — у каждого
// своя логика и свой кэш, делаем только общий перехват окна Rearm/Guard.
namespace EventHook {

// Устанавливает хук (MH_CreateHook + MH_EnableHook). Вызывать ПОСЛЕ MH_Initialize()
// (например, из InstallGameHook). Возвращает true при успехе.
bool Install();

// Снимает хук (MH_DisableHook + MH_RemoveHook). Для полноты.
void Uninstall();

// Обработчик, вызываемый на каждый входящий/отложенный event из Execute_DoList.
// Спавн событий в RA2 уже синхронизирован, поэтому перехват здесь безопасен для
// мультиплеера (мы только читаем событие и, при совпадении, запоминаем цель).
void __fastcall Hooked_ExecuteDoList(void* ecx_this, void* /*edx*/);

// Вызывается каждый кадр из OnGameFrame. Итерация кэша переопределений.
// - принудительно возвращает корабль на закэшированную цель, если движок увёл его
// - очищает записи, если корабль-владелец ИЛИ цель мёртвы
// - при пустом кэше ничего не делает (движок сам управляет)
void Update();

// Очистка всего кэша (загрузка сохранения/скирмиша, ResetSession).
void ClearAll();

// Быстрая проверка: является ли указатель кораблём-спауэнером (SpawnManager).
// Используется чтобы ограничить перехват только нашими кораблями (Дредноут/Авианосец).
bool IsSpawnerShip(TechnoClass* pTechno);

// Диагностика: число активных переопределений.
size_t OverrideCount();

// Кэш явных целей игрока: корабль -> сохранённая цель события.
// Читается в Update(), заполняется в Hooked_ExecuteDoList().
extern std::unordered_map<TechnoClass*, TargetClass> g_PlayerTargetOverride;

} // namespace EventHook
} // namespace LuaAPI
