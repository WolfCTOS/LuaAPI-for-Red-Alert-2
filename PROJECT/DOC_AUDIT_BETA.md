# Beta Documentation Audit (pre-packaging)

> **Date:** 2026-09-22 · **Type:** documentation audit,Static evidence.
> No gameplay/API/lifecycle code changed or added. Gate 1.3, M1/M2,
> Smart AI untouched. No Stable upgrades beyond recorded evidence.
> **Scope checked:** ~90 Lua-exposed functions (all `API.md` entries vs
> binding tables + implementations), full Callback Model, loader
> (`scripts/init.lua`), `README.md` install/usage, `docs/TUTORIAL.md`
> end-to-end, `PROJECT/CAPABILITIES.md` recipes, stale-claim sweep,
> freeze-audit classification review.

## A. PASS (confirmed correct, no change)

- All `unit:`/`house:` colon headers (metatable methods) and all
  `House.`/`World.`/`Engine.`/`Game.`/`Input.`/`WeaponOverride.` dot
  headers/forms (plain functions). Post-M3 sweep: no remaining
  colon-on-plain-function headers (only correct `house:` uses + one
  historical-removal quote).
- Namespaces, names, param counts/defaults, return shapes for:
  Techno reads/orders, House economy/spawn, World queries,
  `PrintMessage`, `WasKeyPressed`, `WeaponOverride`, `AI.QueueUnit/
  CountUnit` arg order, barrel-pitch clamp (`[-90,90]`, source),
  `TakeDamage` fallback chain + remaining-HP returns, `Scatter`
  optional coords, `MarkBounty` defaults, `SetHealthRatio` 0–100
  (code divides by 100; `API.md` already documents this + the
  fractional-input warning).
- Callback honesty: `OnPreDamage` NOT WIRED, `OnUnitDestroyed` never
  dispatched, savegame caveats, `AI.QueueUnit` BLOCKED, `GetWaypoint`
  real contract (post-M2), `OnTick` loader-owned (post-M4),
  `OnScenarioStart` global pattern (post-M1).
- Removal banners: sub-turret/M10 (`API.md`, `README.md`,
  `TUTORIAL.md`, `ROADMAP.md`, Case Study 4 bannered REMOVED with
  history framing — recipe code correctly scoped as record).
- `ENGINEERING_LESSONS.md` §9 (ActiveClickWith), lifecycle guards,
  lepton/64-bit and logical-frame rules: consistent with source.

## B. FIXED (during this audit)

- **F1 — tutorial taught a non-firing pattern.**
  `docs/TUTORIAL.md` SpawnUnit example used `MyFirstMod.OnScenarioStart`
  (table-method form, never dispatched), contradicting its own §212
  warning. Fixed to global `function OnScenarioStart()` + added an
  explicit never-fires note.
- **F2 — optional warhead shown as required.**
  Quick Reference `unit:TakeDamage(amount, warhead)` →
  `unit:TakeDamage(amount, [warhead])` (impl: `luaL_optstring`).
- **F3 — "ratio" implied 0–1 fraction.**
  Quick Reference `unit:SetHealthRatio(ratio)` → `(percent)` + 0–100
  note (a `1.0` input means ~1%, not 100%).
- **F4 — log path unstated (new-modder Q9).**
  `README.md` Quick Start + `TUTORIAL.md` setup now state `LuaAPI.log`
  is written next to `LuaAPI.dll`.
- **F5 — freeze-audit classification corrections** (evidence-based, no
  Stable additions): `OnScenarioStart` Blocked→Experimental (M1
  user-observed); `GetWaypoint` Blocked→Experimental (M2
  user-observed); `SetBountyDrawMode` stale "strip before Beta"
  recommendation (F6 cycling already removed); `WasKeyPressed`
  "fix runtime-pending" qualifier → Gate 1.3 PASS (user-observed).

## C. DOCUMENTATION GAPS (modder needs, not currently findable)

- **G1 — clean-machine install correctness unreviewed.**
  The step-by-step path (extract → injector → match → log) reads
  plausibly and all paths/names check out statically, but no
  clean-environment run exists. Explicitly out of scope here
  (Step 6/7 phases).
- **G2 — `AI.CountUnit` unknown-type error undocumented.**
  Impl raises `luaL_error` on unknown types instead of returning 0.
  Minor; API stays Blocked-adjacent/Unverified — deliberately not
  expanded.
- **G3 — savegame-restore matrix untested.**
  Beyond the `OnScenarioStart` caveat, per-mod state restore across
  loads is unverified stack-wide (carried limitation, already
  disclosed in `README.md`).
- **G4 — no error-visibility contract for `Update`.**
  Related to E1: docs promise log-based debugging without stating
  which errors reach the log.

## D. API CLASSIFICATION ISSUES (corrected in F5; none outstanding)

No API is over-promised after F5. Stable list unchanged (≈30 +
`Update`). `OnScenarioStart`/`GetWaypoint` sit in Experimental at
user-observed grade — NOT Stable — until log-verified runs. Nothing
harness-only, stub, or internal leaks into the Beta promise.

## E. IMPLEMENTATION BLOCKERS (recorded, NOT fixed — out of scope)

- **E1 — per-mod `Update` errors are swallowed.**
  `scripts/init.lua` `OnTick` does `local ok, err = pcall(mod.Update,
  frame)` and discards `err` (timing only). C++-side pcalls
  (`OnTick`, `OnScenarioStart`, `OnDebugCommand`) all log their
  errors — but a broken mod's `Update` fails SILENTLY, contradicting
  the documented debug workflow ("inspect `LuaAPI.log` for script
  errors"). Exact minimal fix (not applied):
  `if not ok then print(...) end` routing `err` to the log.
  Beta-relevant (modder self-debugging) — recommend as the next
  stabilization fix before packaging.

> **Resolution 2026-09-22 (E1 FIX IMPLEMENTED, RUNTIME PENDING —
> history preserved):** `OnTick` now logs
> `[LuaAPI] Mod '<name>' Update error: <msg>` via the existing
> `print`→`LuaAPI.log` path on first occurrence per distinct message
> (dedup guards the per-frame log-flood trap); pcall isolation
> unchanged, healthy mods dispatch normally.
> Harness `tools/tmp/loader_update_error_test.lua`: 10/10 (named
> error, healthy-mod continuation, dedup, re-log on change;
> pre-fix loader fails exactly the error asserts). Live-game error
> surfacing still needs a runtime run — E1 stays open until then.
>
> Live-session note 2026-09-22 (`LuaAPI.log` 18:28–18:38, ~10 min,
> 313+ `Update` calls per mod across 5 healthy mods, zero error lines,
> M1/M2 probe markers nominal): the E1-modified `OnTick` loop ran
> cleanly at volume — no-regression support only. No failing mod was
> present, so the `if not ok` branch never executed live; the
> error-path criteria remain without runtime evidence. E1 Runtime
> still PENDING.
>
> **Resolution (E1: PASS, log-verified + user-observed):** live session
> `LuaAPI.log` 19:17–19:25 with `e1_error_probe` enabled shows exactly
> one `[LuaAPI] Mod 'e1_error_probe' Update error: E1_RUNTIME_PROBE`
> line (19:17:16) across hundreds of throwing frames — dedup proven;
> mod name + message verbatim. Healthy continuation: bounty CLAIMED
> payout and M14.1 census/timing lines through 19:25:45, clean session
> end, no crash. Probe id removed from `active_mods.txt` afterwards
> (probe file kept). E1 CLOSED.

## F. BETA-READY DOCUMENTATION

**YES, with the recorded caveats:** after F1–F5, the documentation
describes the LuaAPI that exists — Stable surface log-verified,
Experimental shelf honestly graded, Blocked list explicit, lifecycle
and savegame limits stated, install path statically coherent. What
remains is NOT documentation work: E1 (one-line loader fix), G1
(clean-machine run), and the external-modder test (later phases).
Packaging may proceed once E1 is dispositioned; nothing in this audit
blocks it on documentation grounds.
