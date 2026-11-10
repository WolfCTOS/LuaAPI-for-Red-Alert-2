# scripts/mods — каталог модулей (состояние на 2026-11-10)

Активный набор задаётся `scripts/active_mods.txt` и читается загрузчиком
в `scripts/init.lua`. Текущий load order: `bounty_hunter`, `smart_ai`,
`radiation`. Этот файл описывает состояние репозитория на указанную дату;
старые snapshots не являются подтверждением текущей конфигурации.

В каталоге также остаются экспериментальные и диагностические модули,
которые не загружаются по умолчанию.

## 1. target_reselect — AA Dodge (v1.1.0, NiTeMind) — INACTIVE

Защита экономики жертвы: харвестер/перегонка игрока под атакой,
рядом своё ПВО — мод отгоняет текущих ИИ-атакующих на менее
защищённую цель штатным `unit:Attack(alt)` и читает `GetTarget()`
обратно. v1.1.0: route-risk gate для джетов (DEEP_AA → WAIT, без приказа).
Командует только identity-проверенными врагами игрока; жертва только
читается. Харнес `tools/tmp/target_reselect_test.lua` 18/18, live-proof M14.1.

## 2. bounty_hunter — Bounty Hunter (source header v3) — ACTIVE EXPERIMENTAL

Выбирает одну вражескую UnitClass-машину/корабль, ставит draw-only метку,
отслеживает цель через ID/HP polling и выплачивает награду при уничтожении.
В текущем исходнике есть lifetime, отмена при захвате, накопление атрибуции
урона и подпись награды на юните-киллере.

Доступный harness помечен как v2 и не является проверкой текущего v3.
В M1_PROGRESS есть live CLAIMED/payout observation, но привязку к точной
версии исходника/build ещё нужно подтвердить. Полная v3-верификация открыта.

## 3. smart_ai — Smart AI Commander — ACTIVE EXPERIMENTAL

Текущий исходник содержит несколько слоёв поведения и диагностики.
Старые описания из прежних каталогов и HOW_TO_USE не считать полным
актуальным inventory. Для текущего состава сверяться с
`smart_ai/main.lua` и `smart_ai/milestones/M1_PROGRESS.md`.

В milestones зафиксированы harness-результаты и несколько user-run live
sessions. Проверка относится к конкретным подсистемам и сессиям; она не
означает, что каждое поведение текущего исходника live-verified.

## 4. command_authority — Command Authority (v0.3.0, LuaAPI Research) — INACTIVE

CP-дуэль поверх RTS: очки за киллы/урон/директивы HUNT (+8) / DEFEND (+8);
траты Z Reinforce (на фронт, с валидацией точки) / X Repair / C Blitz /
V Sabotage. Директор-ИИ тратит только на repair/reinforce + анонсированную
реталиацию (5 с). При 2+ human-домах силы блокируются (детерминизм).
Харнесы 44+36+30 PASS; код-верифицирован, live-pending. Сейчас НЕ в
`active_mods.txt`.

## 5. barrel_elevation_diag — Barrel Elevation Diagnostic (v1.0.0, LuaAPI Research) — INACTIVE

M16-диагностика: включает нативный global AUTO (`SetBarrelPitchAutoAll`)
и раз в секунду пишет heartbeat (счётчик `GetBarrelPitchAutoCount` +
сэмплы); каждые 2 с крутит HVA-кадры турелей (path C probe). Только
отрисовка, на симуляцию не влияет, CnCNet-safe. Нативные биндинги
скомпилированы в Release; smoke (load + тик) OK. Живое визуальное
подтверждение — pending.

## 6. heli_repair_test — Helicopter Repair Diagnostic (v1.0.0, LuaAPI Research) — INACTIVE

M16 Gate 2B прототип: ищет воздушный SCHP, дёргает Deploy/Undeploy,
логирует состояния (`IsOnFloor`/`IsInAir`/`CanDeployNow`/миссия/HP).
Smoke (load + тик) OK; поведение требует живого SCHP в матче —
полевых доказательств нет. Только диагностика.

## 7. radiation — Radiation Weather Hazard — ACTIVE EXPERIMENTAL\n\nРежим радиационной погоды с предупреждением, активной фазой и восстановлением.\nПовреждает открытую пехоту и удерживает состояние заражённой земли.\nПовторные live runs и отсутствие ошибок задокументированы в\n`radiation/FINDINGS.md`. Требование глобального зелёного покрытия карты\nостаётся невыполненным.\n\n## 8. god_mode — УДАЛЁН 2026-09-21

Был сломанным стабом (несуществующий API `game_GetLocalPlayer` /
`house_AddCredits` / `game_RegisterEvent` / `target:GetHouse()`,
table-`OnPreDamage`, `OnRegister` загрузчик не зовёт; `Update(60)` падал).
Удалён по запросу; запись сохранена как история, восстановления нет.
