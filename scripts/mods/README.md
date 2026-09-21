# scripts/mods — каталог модов (рабочее дерево, 2026-09-21)

Активный набор задаётся файлом `scripts/active_mods.txt` (сейчас:
`target_reselect`, `bounty_hunter`, `smart_ai`).
Загрузчик (`scripts/init.lua`) грузит **только** перечисленные там моды.

Сравнение с GitHub (`origin/main`, commit `ef0ce25`):
коммиты совпадают, все отличия — незакоммиченные правки дерева.
На GitHub лежат 6 модов: `delayed_explosion`, `miner_safety`,
`smart_ai`, `squad_speed_sync`, `target_reselect`, `tesla_overload`.
Локально удалены 4 (`delayed_explosion`, `miner_safety`,
`squad_speed_sync`, `tesla_overload`), добавлены 4
(`barrel_elevation_diag`, `bounty_hunter`, `command_authority`,
`heli_repair_test`); изменены `smart_ai`, `target_reselect`.
(`god_mode` удалён 2026-09-21: сломанный стаб, см. п.7.)

## 1. target_reselect — AA Dodge (v1.1.0, NiTeMind) — ACTIVE, РАБОТАЕТ

Защита экономики жертвы: харвестер/перегонка игрока под атакой,
рядом своё ПВО — мод отгоняет текущих ИИ-атакующих на менее
защищённую цель штатным `unit:Attack(alt)` и читает `GetTarget()`
обратно. v1.1.0: route-risk gate для джетов (DEEP_AA → WAIT, без приказа).
Командует только identity-проверенными врагами игрока; жертва только
читается. Харнес `tools/tmp/target_reselect_test.lua` 18/18, live-proof M14.1.

## 2. bounty_hunter — Bounty Hunter (v2.0.0, NiTeMind) — ACTIVE, РАБОТАЕТ

Раз в цикл выбирает ОДИН вражеский боевой юнит (вес rookie 10 /
veteran 25 / elite 40), метит `MarkBounty` (draw-only) и платит
за фраг награду от живого `GetCost` (x1.50 / x1.75 / x2.00) дому
ближайшего hostile-юнита. Захват цели = снятие без награды.
Движковых приказов юнитам не отдаёт. Харнес 35/35, live-сессии есть;
по креш-форензике прямая вина рендера НЕ подтверждена.

## 3. smart_ai — Smart AI Commander (v1.0.0, NiTeMind) — ACTIVE, РАБОТАЕТ

Четыре поведения, каждые 30 кадров, только свои юниты ИИ-дома:
1) flank-breach rally — здание < 85% HP → idle резервы (> 6 клеток)
получают `MoveTo` + `Hunt` к пролому (KNOWN SUSPECT: Hunt может
затирать MoveTo — нужен живой лог с координатами); 2) capture-guard
— дорогой idle юнит (APOC/HTNK), одинокий (> 4 клеток от центроида
своих) и в <= 9 клетках от mind-контролёра (MIND/YURIPR/YURI),
отъезжает `MoveTo` к своим (по переходу + рефреш 600 кадров; занятые
не дёргаются); 3) escort officer — каждая своя V3 получает до 2
ближайших idle-охранников (харвестеры/MCV/2-я V3 исключены,
занятые не дёргаются; destination memory + рефреш 300); 4) garrison
officer (EXPERIMENTAL) — idle пехота в <= 12 клетках от своих
BUNKER/NAPILL/GAPILL идёт на клетку здания (BUNKER-ID живьём не
подтверждён — директива пишет тип в лог для цензуса).
Координация: порядок разведка → Officer → Commander; rally
пропускает назначенных Officer-ом, эскорт отступает в секторе
активной бреши (отпускает охрану в rally с лог-строкой).
Владение — строго identity (`owner == aiHouse`); Neutral/Special/
Civilian исключены (фикс 2026-09-21 против команд игрокам при
зеркальной стране). Спавна/экономики/захвата нет. Харнесы 28/28 +
23/23 (включая mirror-country регрессию T10–T12 и координацию
T8–T10). Офицеры живьём: UNVERIFIED.

## 4. command_authority — Command Authority (v0.3.0, LuaAPI Research) — INACTIVE, РАБОТАЕТ

CP-дуэль поверх RTS: очки за киллы/урон/директивы HUNT (+8) / DEFEND (+8);
траты Z Reinforce (на фронт, с валидацией точки) / X Repair / C Blitz /
V Sabotage. Директор-ИИ тратит только на repair/reinforce + анонсированную
реталиацию (5 с). При 2+ human-домах силы блокируются (детерминизм).
Харнесы 44+36+30 PASS; код-верифицирован, live-pending. Сейчас НЕ в
`active_mods.txt`.

## 5. barrel_elevation_diag — Barrel Elevation Diagnostic (v1.0.0, LuaAPI Research) — INACTIVE, РАБОТАЕТ (live-pending)

M16-диагностика: включает нативный global AUTO (`SetBarrelPitchAutoAll`)
и раз в секунду пишет heartbeat (счётчик `GetBarrelPitchAutoCount` +
сэмплы); каждые 2 с крутит HVA-кадры турелей (path C probe). Только
отрисовка, на симуляцию не влияет, CnCNet-safe. Нативные биндинги
скомпилированы в Release; smoke (load + тик) OK. Живое визуальное
подтверждение — pending.

## 6. heli_repair_test — Helicopter Repair Diagnostic (v1.0.0, LuaAPI Research) — INACTIVE, РАБОТАЕТ ЧАСТИЧНО

M16 Gate 2B прототип: ищет воздушный SCHP, дёргает Deploy/Undeploy,
логирует состояния (`IsOnFloor`/`IsInAir`/`CanDeployNow`/миссия/HP).
Smoke (load + тик) OK; поведение требует живого SCHP в матче —
полевых доказательств нет. Только диагностика.

## 7. god_mode — УДАЛЁН 2026-09-21

Был сломанным стабом (несуществующий API `game_GetLocalPlayer` /
`house_AddCredits` / `game_RegisterEvent` / `target:GetHouse()`,
table-`OnPreDamage`, `OnRegister` загрузчик не зовёт; `Update(60)` падал).
Удалён по запросу; запись сохранена как история, восстановления нет.
