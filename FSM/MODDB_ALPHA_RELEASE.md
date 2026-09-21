# ModDB Alpha Release Gate

Objective gate for publishing LuaAPI as an Alpha on ModDB: the minimum
condition set after which publication stops being misleading. No
production polish required; experimental features allowed. No code was
changed to write this document — it judges the tree as it stands.

## Purpose

Move LuaAPI from its current Alpha-state working tree to a real public
Alpha without promising unverified capabilities as ready. Every section
below is a checklist, not an essay. Items reference the audit in
`FSM/FEASIBILITY_TRIAGE.md` ("Audit") where the evidence lives.

## What Alpha Means

- Experimental features may ship, clearly labeled as experimental.
- The public build must NOT: promise API that does not exist; describe
  unverified behavior as guaranteed; hide a known critical crash;
  require undocumented dev-only steps; ship docs that contradict source.
- Harness PASS ≠ live claim — the Alpha page must use the same evidence
  grades as `FSM/VERIFICATION.md` (no grade inflation for marketing).

## Technical Gate

- [ ] Injection verified on both supported processes (`gamemd.exe`,
  `gamemd-spawn.exe`) from a clean checkout + documented build
  (MSVC, Win32, submodules initialized).
- [ ] Hook inventory matches documentation (MainLoop, LoadString,
  Bullet-Detonate, GetPrimaryWeapon, DrawAsVXL; ActiveClickWith
  documented as disabled, not as a feature).
- [ ] Lua VM/session lifecycle documented as implemented: single VM per
  process, `init.lua` once, per-match state is each mod's own
  responsibility (CA pattern as reference).
- [ ] Stale house-userdata cache across matches resolved OR explicitly
  disclosed with a workaround (restart the game between matches).
  See Critical Blocker 1.
- [ ] No new `0xC0000005` on the recommended launch path in repeated
  clean-machine runs (see Blocker 4).
- [ ] Default active mod list contains no inert mods (see Blocker 3).

## Documentation Gate

- [ ] `API.md` corrected where it contradicts source (all doc-only fixes,
  no code): document implemented `Attack`/`GetMission` (+ any further
  gaps found in review); correct the `OnPreDamage` contract to the
  implemented behavior (global lookup; no live damage invocation —
  see Audit); correct `SetHealthRatio` scale to implementation.
  → **DONE 2026-09-20**: Attack/GetMission documented; OnPreDamage marked
  NOT WIRED; SetHealthRatio corrected; undocumented extras added as an
  "Implemented Extras" section (UNVERIFIED LIVE grades);
  `docs/TUTORIAL.md` stale claims fixed same day (global callback forms,
  OnPreDamage not-wired, OnUnitDestroyed placeholder).
- [ ] `PROJECT/CAPABILITIES.md` recipes brought in line with the
  current engine (no `game_RegisterEvent`, no table-method damage
  callbacks, no `PrintMessage` color argument) or clearly marked
  historical.
  → **PARTIALLY DONE 2026-09-20**: stale recipes already caveated (Case
  Studies 1/2) + evidence-authority note added; Case Study 3 re-graded
  (table-method OnScenarioStart never dispatched → damaged_fleet inert);
  remaining recipes not individually re-audited.
- [ ] `PROJECT/ROADMAP.md` reconciled: v1.0 "Production Release" is
  marked done while the project operates as Alpha — either the claim
  or the status must change before outsiders read it.
  → **DONE 2026-09-20**: ROADMAP header marks v1.0 a historical tag;
  `README.md` honesty pass same day (Alpha stage, OnPreDamage not-wired,
  global-callback model, gate pointers).
- [ ] `README.md` Installation/Quick-Start sections reviewed end-to-end
  (they exist; correctness unreviewed).
  → **REMAINING OPEN** (the honesty pass corrected claims; the
  step-by-step install instructions themselves still need an
  end-to-end correctness review).
- [ ] `OnUnitDestroyed` never-dispatched status documented as-is (no
  destruction payloads promised, no placeholder call exists); savegame-lifecycle caveats stated
  (`OnScenarioStart` does not fire on load).
- [ ] Every public capability claim links to harness/log evidence or is
  labeled experimental.

## Showcase Gate

Minimum material: 1 flagship + 2–4 small showcases + "Why LuaAPI?"
+ screenshots/video + repo link + install instructions. Ten mods are
not required. Grades used: implemented / verified / live-tested /
showcase-ready (distinct levels).

- [ ] Flagship: **Command Authority** — implemented ✓ / verified
  (44+36+30 harness) ✓ / live-tested (two analyzed sessions) ✓ →
  SHOWCASE-READY. With honest scope notes (MP gate harness-only;
  Neutral-exclusion silent-by-construction).
- [ ] Small 1: **barrel_elevation_diag** (M16) — implemented /
  changelog-verified / live heartbeat → SHOWCASE-READY (visual,
  simulation-neutral).
- [ ] Small 2: **target_reselect** (M14) — implemented / live-observed
  (census + victim lines); no dedicated harness → ship as
  experimental with that label.
- [ ] Small 3 (conditional): **multi_turret_battleship** — Gate-10 log
  proof on record, but the mod is absent from the source tree (stale copy
  only under `build/Release/scripts/mods/`) and out of the default stack →
  requires re-creation + re-verification on the current build before
  showcasing; otherwise stays out.
- [ ] Excluded: **bounty_hunter** (inert — table-method callback never
  dispatched; must not be presented as a working feature),
  **smart_ai** (works mechanically; frame-backwards restart guard present
  since the capture-guard MVP, but rally order churn unthrottled per unit
  and live effect of the guard untriggered; fix first, showcase
  later), `god_mode` and other dormant mods (stale API usage).
- [ ] "Why LuaAPI?" — one short page: runtime gameplay layer over RA2,
  C++ safety + Lua decisions, what INI/Ares-style config cannot do
  (persistent match-aware systems with player decisions — CA as the
  exhibit; no "better than Ares/Phobos" claims).
- [ ] Screenshots and at least one short gameplay video (none exist in
  the tree — must be produced, including a visible CA moment such as
  an `adjusted` reinforcement or a directive cycle).
- [ ] Public repo link published and referenced from the ModDB page.

## Packaging Gate

- [ ] Upload contains ONLY distributable files (explicit allowlist):
  `LuaAPI.dll`, `injector.exe`, `scripts/`, user docs. Game assets
  (`.mix`, `.exe` game binaries, maps) and dev artifacts (build dirs,
  logs, `tools/tmp` harnesses) are EXCLUDED — by rule, not by review
  (copyright + size).
- [ ] Archive installs by documented steps only (copy next to a YR
  1.001 install, run injector/prescribed launcher path).
- [ ] Working tree committed/hygienic before packaging (currently dirty
  with game assets untracked — release must not be cut from this
  state by accident).

## Installation Gate

- [ ] Clean-machine install test (YR 1.001 + CnCNet-spawn path):
  inject → match starts → CA announcement appears → powers work.
- [ ] Documented: requirements (game version, Windows, MSVC
  redistributable if needed), submodule/build prerequisites for
  source users, the recommended launch path, and what to do when
  injection misses (bounded-wait behavior).
- [ ] No undocumented dev-only steps (hotkeys beyond gameplay,
  env vars, manual DLL placement beyond the documented layout).

## Known Limitations

(Ship in the release notes verbatim in spirit — these are
implementation facts, not bugs to hide.)

- No live incoming-damage interception (`OnPreDamage` never invoked;
  see Audit conflict note).
- `OnUnitDestroyed` is never dispatched; no destruction payloads.
- No radar/fog control, no alliance switching, no superweapon API, no
  production-complete events (queue counts only).
- Lua state is per-process: mods without restart guards misbehave on
  2nd+ match in one process (CA is guarded; others vary).
- Multiplayer: powers lock with 2+ humans by design; 2-client behavior
  otherwise unverified.
- Toolchain: MSVC-only, Win32, submodule init required.
- Balance of CA systems is experimental and untuned for competitive play.

## Critical Blockers

A blocker means: publishing without resolving it risks broken
behavior, uninstallable builds, or seriously misled users. Unpleasant
is not enough — each item below meets the bar.

1. **Stale house cache across matches** (Audit: `g_houseCache`
   persists; `ClearHouseCache` reachable only via dead
   `ResetSession`). Class: use-after-free on match 2+ in one process.
   Resolve by code (revive reset path or per-match invalidation) or
   downgrade with a tested workaround (mandatory restart between
   matches, documented + enforced if possible). No multi-match crash
   has been isolated to this — stated so it is not overclaimed either.
2. **Docs-vs-source contradictions** (Audit list). A public Alpha
   whose reference promises a damage pipeline that does not exist is
   misleading by definition. All fixes are documentation-only.
3. **Inert `bounty_hunter` in the default stack.** It documents and
   HUD-promises nothing (it is silent), but it IS presented in-tree as
   the combat-economy demo while doing nothing. Decide pre-release:
   drop from defaults (config) or fix the mod (code, needs approval).
   → **RESOLVED 2026-09-20** (Gate 1 item 2): removed from
   `scripts/active_mods.txt`; kept in tree as a historical reference mod
   with an inert-status header; repair is a candidate future cleanup
   (`PROJECT/DECISIONS.md`).
4. **Intermittent `0xC0000005` on non-recommended launch paths**
   (root UNKNOWN; one occurrence without LuaAPI — Audit). Retest on
   the recommended path repeatedly; disclose whatever remains; if it
   reproduces on the recommended path it stays critical.

## Release Risks

- SmartAI/target_reselect cross-match staleness (fixes known:
  CA-style restart guards; needs approval + harness).
- Savegame-load behavior of stateful mods (untested across the stack).
- Windows-version coverage is one machine; 32-bit-era engine quirks
  on Win10/11 beyond the tested setup are unknown.
- `SetHealthRatio` scale decision pending (open separate issue;
  CA repair path affected).
- ModDB content rules vs any accidental game-asset inclusion
  (mitigated by Packaging Gate allowlist).
- Tower-vs-true-Arabs and factory-contact follow-ups remain open
  research items (no release impact; must not be presented as closed).

## Post-Release

- `OnPreDamage` wiring decision (wire it or officially park reactive
  armor; either way update docs + Iron Curtain feasibility).
- `ResetSession` revival / per-match VM hygiene.
- Restart guards for SmartAI/target_reselect; `bounty_hunter` fate.
- `SetHealthRatio` scale + CA repair path.
- ROADMAP reconciliation as a standing docs task.
- Demo video pipeline (record every showcase change).
- Next experiments per `FSM/FEASIBILITY_TRIAGE.md` (QueueUnit
  micro-probe → Aegis/Sector prototypes).

## Final Checklist

- [ ] All Technical Gate boxes checked with log/build evidence.
- [ ] All Documentation Gate boxes checked (diff-reviewed, no
  source contradictions remain).
- [ ] Showcase Gate material exists on disk (mods verified,
  screenshots + video produced, Why-page written).
- [ ] Packaging allowlist enforced; archive installs on a clean machine.
- [ ] Known Limitations published alongside the download.
- [ ] Zero open Critical Blockers (each either resolved or formally
  downgraded with a tested workaround + disclosure).
- [ ] Evidence grades in all public text match `FSM/VERIFICATION.md`
  (no harness→live inflation).
