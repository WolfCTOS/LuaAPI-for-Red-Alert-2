# 🗺️ LuaAPI for Red Alert 2: Yuri's Revenge — Architecture Roadmap

## 📍 Project Lifecycle Overview
- **Phase 1: Proof of Concept & MVP (Milestones 1–5)** -> [x] DONE / VERIFIED
- **Phase 2: Alpha-1 - Core Lifecycle & Safety (Milestone 6)** -> [x] DONE / VERIFIED
- **Phase 3: Alpha-2 - Spatial Queries & Extended Events (Milestone 7)** -> [x] DONE / VERIFIED
- **Phase 4: Beta - Feature Freeze & Stress Hardening (Milestone 8)** -> [ ] IN PROGRESS (Gate 8.1 Passed)
- **Phase 5: Production Release v1.0 (Milestone 9)** -> [ ] PLANNED

---

## 🏆 Detailed Milestones, Gates & Architectural Rationale

---

### [x] Milestone 1–3: Core Engine Hooking & Runtime Sandbox (MVP)
> **Why Milestone 1–3:** Прежде чем добавлять геймплейные фичи, необходимо доказать техническую возможность внедрения современного рантайма Lua 5.4 в закрытый 32-битный бинарник `gamemd.exe` 2001 года без падения FPS и нарушения оригинального цикла ассемблерного кода.

- [x] **Gate 1.1: Hook MainLoop at `0x55D360`**
  - *Why:* Адрес `0x55D360` — каноническая точка кадрового тика RA2. Если хук здесь медленный или нестабильный, весь проект обречен на провал. Доказан нулевой оверхед.
- [x] **Gate 1.2: Lua 5.4 VM with isolated `pcall` execution**
  - *Why:* В нативном C++ любая синтаксическая ошибка в скрипте моментально крашит игру. Изоляция через `pcall` гарантирует, что ошибки логируются, а `gamemd.exe` продолжает работать.
- [x] **Gate 1.3: Basic TechnoClass Property Manipulation**
  - *Why:* Доказывает работоспособность двустороннего моста памяти — безопасное чтение и мутацию нативных структур движка из Lua.

---

### [x] Milestone 4: Inbound Events (Sub-Frame Reactive Control)
> **Why Milestone 4:** Покадровый опрос (`Update(frame)`) принципиально опаздывает на 1 кадр. Для создания силовых щитов, поглощения урона и кастомной брони требуется реактивный перехват событий *до* того, как движок применит математику повреждений.

- [x] **Gate 4.1: `OnPreDamage` Engine Interception Hook**
  - *Why:* Перехватывает точку входа в расчет урона (`ReceiveDamage`), позволяя скриптам перехватить атаку в реальном времени.
- [x] **Gate 4.2: Damage Modification & Cancellation Pipeline**
  - *Why:* Позволяет скрипту вернуть измененное число (например, поглотить 50% урона) или `0` (полный иммунитет), изменяя поведение физики без правок в INI.
- [x] **Gate 4.3: End-to-End Validation via `shield_overload`**
  - *Why:* Практическое доказательство работы математики поглощения урона в реальных боевых условиях.

---

### [x] Milestone 5: Multiplayer Determinism & External Benchmarking
> **Why Milestone 5:** Сетевой код Yuri's Revenge (CnCNet) работает по принципу детерминированного локального шага (Lockstep). Любое расхождение в генерации случайных чисел или задержки потока вызывают мгновенный Out of Sync (OOS) и вылет матча.

- [x] **Gate 5.1: Headless CnCNet Spawner Mode (`--withcncnet`)**
  - *Why:* Позволяет инжектору пассивно подключаться к `gamemd.exe`, запущенному через `YRLauncher.exe`, не ломая аргументы командной строки и сетевые сокеты.
- [x] **Gate 5.2: Synchronized Frame-Seeded RNG (Seed 12345)**
  - *Why:* Замена нестабильного `os.clock()` / `os.time()` на детерминированный сид от номера кадра гарантирует 100% идентичность вычислений на всех ПК игроков.
- [x] **Gate 5.3: Hardware ETW Benchmark via Intel PresentMon**
  - *Why:* Исключает субъективные оценки. Аппаратный замер доказал эталонные **60.04 Avg FPS / 55.11 1% Lows** (0.00% оверхеда).

---

### [x] Milestone 6: Alpha-1 — Lifecycle Hardening & RTTI Safety
> **Why Milestone 6:** Главный бич моддинга C&C на протяжении 20 лет — ошибка `0xC0000005` (Access Violation) при обращении к уничтоженным объектам. Этот майлстоун превратил любительский инжектор в защищенный SDK.

- [x] **Gate 6.1: `ResetSession()` on Map Load, Restart, and Exit**
  - *Why:* Очищает реестр коллбэков и память Lua при выходе в меню или рестарте, предотвращая утечки памяти и вызовы висячих ссылок прошлой сессии.
- [x] **Gate 6.2: `ValidateTechno()` RTTI (`WhatAmI()`) & Lifecycle Flags**
  - *Why:* Проверяет валидность памяти пулов RA2 перед передачей в Lua. Если объект мертв — возвращает `nil, error` вместо фатального краша игры.
- [x] **Gate 6.3: Core Economy & HUD Message APIs**
  - *Why:* Дает моддерам базовые глаголы взаимодействия с игроком (`house_AddCredits`, `game_PrintMessage`) без необходимости ковырять память.
- [x] **Gate 6.4: Validation via `bounty_hunter` & `v0.1.0-alpha` Release**
  - *Why:* Предоставление первого готового бинарного пакета для внешних тестеров сообщества.

---

### [x] Milestone 7: Alpha-2 — Spatial Map API & Extended Events
> **Why Milestone 7:** Прямой ответ на запрос моддинг-сообщества. Устраняет необходимость использовать 20-летние костыли триггеров FinalAlert2 (невидимую артиллерию для дыма, фейковые здания на вейпоинтах).

- [x] **Gate 7.1: Scenario Start Hook (`OnScenarioStart`)**
  - *Why:* Первый кадр после загрузки карты. Позволяет бесшумно и чисто настраивать стартовые армии, поврежденные флоты и катсцены без звуковых багов и ложных сирен EVA.
- [x] **Gate 7.2: Destruction Event Hook (`OnUnitDestroyed`)**
  - *Why:* Фундамент для систем наград за убийства, скриптов эвакуации, фаз боссов и триггеров победы/поражения.
- [x] **Gate 7.3: Spatial Queries (`game_GetWaypoint`, `game_GetUnitsInRadius`)**
  - *Why:* Позволяет привязывать логику к вейпоинтам карты и сканировать область радиуса без хардкода сырых пиксельных координат.
- [x] **Gate 7.4: Showcase Validation via `damaged_fleet`**
  - *Why:* Демонстрирует мгновенную установку 35% HP и прикрепление частиц `DamageSmokeSys` на старте миссии.

---

### [ ] Milestone 8: Beta — Feature Freeze & Hardening (CURRENT)
> **Why Milestone 8:** Переход от добавления фич к промышленной стабилизации. Гарантирует, что движок выдерживает экстремальные нагрузки, API зафиксирован, а документация исчерпывающая.

- [x] **Gate 8.1: Long-Run Combat Stress Test (18k Frames / 5-min Battle)**
  - *Why:* Доказывает отсутствие утечек памяти и деградации сборщика мусора (GC). Успешно пройден: 17 993 кадра, 7 Brutal ботов, **56.24 FPS 1% Low**, 0 вылетов.
- [x] **Gate 8.2: Comprehensive API Reference Manual (`API.md`)**
  - *Why:* Сторонние моддеры не могут разрабатывать моды вслепую. Необходим полный справочник всех функций, сигнатур, типов и сниппетов.
- [ ] **Gate 8.3: CnCNet ModBase Native Integration**
  - *Why:* Обеспечивает возможность внедрения `LuaAPI.dll` в крупные модпаки (DTA, Mental Omega) через прозрачный запуск без всплывающих окон.
- [ ] **Gate 8.4: Feature Freeze & API Stabilization**
  - *Why:* Фиксация сигнатур всех функций Lua, гарантирующая, что моды, созданные сегодня, не сломаются в будущих версиях движка.

---

### [ ] Milestone 9: Production Release v1.0
> **Why Milestone 9:** Финальный публичный выход платформы в мировое сообщество C&C с готовыми инструментами, инжектором и документацией.

- [ ] **Gate 9.1: Public Release Package (v1.0.0 Stable ZIP)**
  - *Why:* Публикация готового архива с бинарниками, лаунчером и стартовыми модами на GitHub Releases и ModDB.
- [ ] **Gate 9.2: Community Showcase Announcement (Haven & PPM)**
  - *Why:* Официальная презентация платформы моддерам с демонстрацией решения проблемы `0xC0000005` и бенчмарками производительности.