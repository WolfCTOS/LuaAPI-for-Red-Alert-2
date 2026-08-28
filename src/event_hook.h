#pragma once
#include <YRPP.h>
#include <TargetClass.h>
#include <unordered_map>

namespace LuaAPI {

// Хук на FootClass::Active_Click_With (gamemd 1.001 @ 0x4D74E0).
//
// Назначение: перехватить ЯВНЫЙ клик атаки игрока по кораблю-спаунеру
// (Дредноут/Авианосец наследуются от FootClass) и принудительно удерживать его на
// цели, которую выбрал игрок, пока движок не попытается перецелиться в окне
// Rearm/Guard.
//
// Модуль изолирован от SubTurretManager::ManagePrimaryAttackTarget — у каждого
// своя логика и свой кэш, делаем только общий перехват окна Rearm/Guard.
namespace EventHook {

// Устанавливает хук (MH_CreateHook + MH_EnableHook). Вызывать ПОСЛЕ MH_Initialize()
// (например, из InstallGameHook). Возвращает true при успехе.
bool Install();

// Снимает хук (MH_DisableHook + MH_RemoveHook). Для полноты.
void Uninstall();

// Обработчик FootClass::Active_Click_With(ActionType, ObjectClass*).
// Срабатывает при КЛИКЕ игрока по юниту (в т.ч. атака). Детур объявлен __fastcall:
// на x86 this приходит в ECX, что совпадает с __thiscall оригинала.
void __fastcall Hooked_ActiveClickWith(FootClass* pThis, void* /*edx*/,
                                       int action, void* pTarget);

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

// Кэш явных целей игрока: корабль -> сохранённая цель клика атаки.
// Читается в Update(), заполняется в Hooked_ActiveClickWith().
extern std::unordered_map<TechnoClass*, TargetClass> g_PlayerTargetOverride;

} // namespace EventHook
} // namespace LuaAPI
