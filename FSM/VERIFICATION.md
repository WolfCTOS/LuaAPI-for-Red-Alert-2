# FSM — Verification Ledger

Evidence levels are never mixed. Column meanings:

- **Harness** — deterministic mock-Lua checks (`tools/tmp/`). Proves mod
  logic, never engine behavior.
- **Static** — source inspection (exact files/lines). Proves construction,
  never runtime effect.
- **Runtime** — live `gamemd` sessions with log evidence. Session bounds
  and decision trees live in `docs/research/`.
- **User-observed** — eyeball reports. Weakest grade for ownership claims;
  reconciled against instrumented data, never averaged with it.
- **Limitations** — what the above explicitly does NOT cover.

## Ledger

### Loader global-callback order (M1 — `OnScenarioStart` shadowing)
| Level | Status |
|---|---|
| Harness | `tools/tmp/loader_scenario_start_test.lua`: 6/6 PASS on fixed `scripts/init.lua` (mod file-scope handler survives; `OnTick` dispatches `Update`); 4/6 on pre-fix loader with exactly the M1 survival asserts failing (sensitivity proven) |
| Static | defaults-before-require in `scripts/init.lua`; C++ frame-1 lookup/pcall unchanged (`src/lua_engine.cpp`); probe mod `scripts/mods/scenario_start_probe/` (not in default stack) |
| Runtime | PENDING — protocol: enable probe, launch, `[PROBE] OnScenarioStart fired at frame 1` once per match + Update heartbeat, incl. second in-process match |
| User-observed | none |
| Limitations | `OnUnitDestroyed` default moved too but never dispatched (Blocked regardless); multi-mod global contention stays last-write-wins |

### War Reporter (ARCHIVED)
| Level | Status |
|---|---|
| Harness | NONE (no artifacts) |
| Static | NONE (no source) |
| Runtime | NONE |
| User-observed | historical complaint only (telemetry without decisions) |
| Limitations | implementation details unrecoverable; kept as negative result |

### War Clock (REMOVED)
| Level | Status |
|---|---|
| Harness | HISTORICAL ONLY — `tools/tmp/war_clock_test.lua` survives and
describes schedule exactness/symmetry/HUD, but it `loadfile`s
`scripts/mods/war_clock/main.lua`, which is absent → NOT re-runnable
today. Its asserts attach to a lost source, not to current code. |
| Static | mod source absent; harness source present |
| Runtime | NONE in tree |
| User-observed | playtest feedback against random EMP (quoted in CA header) |
| Limitations | mechanic reconstructed from harness, not from implementation |

### Command Authority — economy/director/powers/directives/MP gate
| Level | Status |
|---|---|
| Harness | `command_authority_test.lua`: 44/44 PASS (economy math,
Director restraint + repair, retaliation doctrine + timing, HUNT/DEFEND
success/fail/expire, frontline positions, refusal rules, MP lock) |
| Static | `scripts/mods/command_authority/main.lua` (764 lines post-fix);
call sites and guards reviewed per fix |
| Runtime | two live sessions frame-analyzed (see RCA docs + log-review
notes): CP/kill/damage awards, `DIRECTOR:` actions, directives,
retaliation timing, Z-spawn correlation |
| User-observed | active live play (complaints drove both fixes; post-fix
placement session observed with `adjusted` tags) |
| Limitations | MP 2-client behavior harness-only; survival/directive edges in
long matches partially sampled; first-spawn Neutral funding detail
unconfirmed (moot post-Fix-A) |

### Fix A — Neutral/Special exclusion
| Level | Status |
|---|---|
| Harness | `command_authority_neutral_test.lua`: 36/36 PASS (seeding,
survival, killer-gate, fallback, spend-gate, retaliation schedule +
normal path) |
| Static | `NON_COMBATANT` + 5 application sites reviewed; alliance/cache
semantics preserved; `powerRepair` untouched |
| Runtime | SILENT BY CONSTRUCTION — neither the C++ `SpawnUnit` log nor CA
messages attribute an owner, so exclusion has no log signature.
Recorded as code-verified / live-pending. Absence of Neutral-owned
squatters is the expected (non-)signal. |
| User-observed | pending a real match |
| Limitations | execute-guard for stale retaliation records is
unreachable-by-construction post-fix (inspection-verified only, by
design — the schedule gate blocks creation) |

### Placement validation (frontline displacement)
| Level | Status |
|---|---|
| Harness | `command_authority_frontline_test.lua`: 30/30 PASS, incl.
bit-exact precomputed coordinates (safe, eco/army displacement,
normal-frontline preservation, centroid fallback, symmetric Human) |
| Static | `FRONTLINE_*` block + restructured `powerReinforce` reviewed;
determinism argument (ID-order, IEEE-double, no RNG/clock) |
| Runtime | PARTIAL: 12× `at the front, adjusted` tags in the live session
proves the path fires; sampled adjusted spawns all placed 2/2.
Full tactical effect (no more eco-stacking) is user-observed pending. |
| User-observed | original Harvester-stacking complaint (pre-fix); post-fix
effect pending |
| Limitations | thresholds (5/2/3-in-5/shift-8) are engineering judgments,
unmeasured live; OOB displacement degrades to silent no-op without
charge (safe, unannounced); `with your forces` message covers two
outcomes (no-lp vs failed revalidation) |

### Targeting RCA (SpawnUnit / ownership-gating)
| Level | Status |
|---|---|
| Harness | n/a (behavioral engine question; harness mocks can't render it) |
| Static | full `SpawnUnit` engine-call inventory
(`src/bindings_house.cpp:177-206,304-396`); hook inventory proving no
targeting interference (ActiveClick disabled, EventHook spawner-only);
`HouseClass::IsNeutral = MultiplayPassive` (`third_party/YRpp/
HouseClass.h:746`); `CanAutoTargetObject`/`TryAutoTargetObject`
addresses recorded, internals unobservable |
| Runtime | 60+ instrumented samples, zero exceptions: Arabs-owned spawned
engaged ≤15–45f / dead ≤210f; Neutral-owned `NONE` at 45f+ at 1–4
cells, attrition over 300–600f; manual `Attack` accepted in all
samples; spawned self-acquire ≤45f |
| User-observed | initial complaint (reconciled: Neutral signature) +
later Arabs-ignored report (reconciled: most probably Neutral
misidentification; zero instrumented counter-samples; discriminating
test specified, none produced) |
| Limitations | no factory-in-contact samples; no weapon-tower-in-range
samples (`DiscoveredBy`/threat internals UNOBSERVABLE WITH CURRENT
API); tower-vs-TRUE-Arabs remains the single most valuable
follow-up IF contradictory evidence ever appears |

### Damage-interception pipeline (`OnPreDamage` live) — OPEN DISCREPANCY
| Level | Status |
|---|---|
| Harness | none runnable (archive consumers use `game_RegisterEvent`,
which has no binding, + table-method callbacks the engine never
dispatches) |
| Static | REFUTED-as-wired in current source: global reference collected
(`src/lua_engine.cpp:994-1026`), **no invocation site repo-wide**,
no `ReceiveDamage` hook installed; `game_RegisterEvent` binding
absent. Available history (`git log -S`) shows only the milestone-6
introduction commit — no removal record recoverable. |
| Runtime | none possible through this path today |
| User-observed | n/a |
| Limitations | conflicts with `PROJECT/ROADMAP.md` Milestone 4 [x] and
`API.md`/`PROJECT/CAPABILITIES.md` interception language — flagged
here, not silently resolved. If a build wires it, this row and
`FSM/IRON_CURTAIN.md` checks 3–5 must be revised. |

### `SetHealthRatio` percent-scale (open separate issue)
| Level | Status |
|---|---|
| Harness | none (deliberately untouched) |
| Static | binding divides by 100 (`src/bindings_techno.cpp:1127-1131`):
effective scale is 0–100, contradicting `API.md` 0.35-style examples;
CA passes `1.0` (≈1% HP). Code finding only. |
| Runtime | no confirming log lines found; UNVERIFIED live |
| User-observed | none |
| Limitations | left alone per scope decision; needs its own RCA before any fix |

### QueueUnit production gate (Counter-Composition dependency)
| Level | Status |
|---|---|
| Harness | n/a by design (mock factories cannot prove engine production) |
| Static | `AI.QueueUnit/CountUnit` bindings + `DemandProduction`/
`CountTotal` signatures reviewed; acceptance semantics unknown past
the boolean |
| Runtime | two live headless sessions: v1 proved acceptance-without-
validation on mismatched types; v2 (faction-authentic `E2`/`HTNK`,
mature base, working factory in-window per AI-production census):
accepted, zero attributable output (`E2` clean-negative; `HTNK`
self-polluted by AI mass production). Full record:
`FSM/QUEUEUNIT_GATE.md` |
| User-observed | n/a |
| Limitations | sink-vs-AI-scrub indistinguishable live (equivalent for
planning); overturn test specified (attributed APOC-class appearance,
not run); verdict `BLOCKED` for the production dependency — decision
layer unaffected |

### SmartAI capture-aware valuables guard (MVP)
| Level | Status |
|---|---|
| Harness | `tools/tmp/smartai_capture_test.lua`: 15/15 PASS (no-threat
preservation, type-specificity incl. close-Rhino negative,
with-group exemption, churn-free repetition, flip observation w/o
decisions, restart reset, flank-rally regression, engaged-unit and
out-of-envelope exemptions) |
| Static | new guard section in `scripts/mods/smart_ai/main.lua`
(`VALUABLE_TYPES` default `APOC`+`HTNK`, `CAPTURE_THREATS`
`MIND`/`YURIPR`/`YURI` with provenance comments, transition-only
orders + 600f refresh, flip observation, frame-backwards restart
guard); rally code untouched; no new bindings; vanilla capture
unimplemented by design |
| Runtime | PARTIAL (11-min headless Dannath match, SmartAI enabled):
Scenario A PASS (zero pullback lines, match normal, rally intact);
flip observer fired ×4 live (`HTNK`/`HTK`/`HTNK`/`E2`
Africans→YuriCountry); `YURIPR` confirmed live in census.
Scenario B (pullback decision) NOT triggered — no lone idle valuable
co-visible with a capturer in-window. NOT claimed verified. |
| User-observed | original Nuclear-Truck-vs-Yuri concern (external/custom
context — `Nuclear Truck`/`Livya` IDs absent repo-wide); live
behavior change pending a triggering conjunction |
| Limitations | threat proximity (≤9) may under-detect long-range Prime
control; capturer identity on flips unknowable (no attacker payload
— recorded finding, no binding proposed); no ForceGroup exists in
SmartAI (stability question moot); zero guard orders issued live
(zero churn by construction); `Nuclear Truck` support = adding its
real TypeID to `VALUABLE_TYPES` when supplied |

### M16 barrel pitch / framework / other repo capabilities
- M16 Engine draw API: per `PROJECT/CHANGELOG.md` (cited, not re-verified
  here; simulation-neutral by design).
- Framework (`scripts/framework/`, M14): "framework logic verified,
  in-game runtime pending" per `PROJECT/CAPABILITIES.md` (cited).
- Iron Curtain concept: all levels NONE — concept only
  (`FSM/IRON_CURTAIN.md`).

### Unit-count attribution ("20–30 Rhino" match observation, 2026-09-20)
| Level | Status |
|---|---|
| Harness | n/a (attribution question, not a mod-logic question) |
| Static | SmartAI has NO spawn/production/credit capabilities — repo-wide
  grep of `scripts/mods/smart_ai/main.lua` finds zero `SpawnUnit` /
  `QueueUnit` / `AddCredits` (verified 2026-09-20). Command Authority's
  Director reinforcement path (`powerReinforce`) spawns pairs of
  player-faction tanks at `lastKillPos`-derived frontline points — the
  only writer in the active stack with that capability.
  `barrel_elevation_diag` is draw-path only; `bounty_hunter` is inert.
| Runtime | log-review of the reported match: Rhino counts/spacing/
  positions consistent with CA Director reinforcement pairs; no
  SmartAI-produced unit counter-evidence (SmartAI issues orders only —
  move/attack, never create).
| User-observed | original "20–30 Rhino" report (external to this repo's
  session logs) — reconciled against capability inventory above.
| Limitations | per-unit ID-level spawn attribution (spawn line ↔ unit id
  correlation) was not run; conclusion rests on capability exclusivity
  (only CA can spawn) + count/spacing pattern. If a future stack adds
  another spawning mod, this row must be re-derived.
| **Attribution** | **observed Rhino flood = Command Authority Director
  reinforcement pairs. NOT SmartAI, NOT normal factory production.**
  Standing rule: do not attribute observed behavior to SmartAI (or any
  single mod) without a capability check like this one.

### Launch-path stability (the `0xC0000005` question) — Gate 1 item 4
| Level | Status |
|---|---|
| Harness | n/a (process-lifecycle question, not mod logic) |
| Static | crash reproductions are non-repo artifacts: `syringe.log.session1832.bak`
  / `session1834.bak` in the tree root record two direct-Syringe-launch
  access violations (`0xC0000005 at 0x72D0C4E2` / `0x72D9C4E2`, exit code
  C0000005); the third reproduction is recorded in
  `docs/decisions/SUPPLY_TANK_AMMO_GATE1_PLAN.md` — which also records that
  **one crash occurred with no LuaAPI injected** (crash not proven
  LuaAPI-caused; root cause UNKNOWN).|
| Runtime | **until 2026-09-20, zero crashes were on record** on the
  recommended CnCNet-spawner path (M14.1 live, Gate 14.8's four sessions,
  CA sessions, QueueUnit v1/v2, M16's five sessions; 18k-frame benchmark
  0 crashes; clean-exit session ~305 s, exit `0x00000000`). **2026-09-22:
  FIRST crash on the recommended path** — CnCNet → SyringeEx →
  `gamemd-spawn.exe` (+Ares/Phobos/spawner) → ~3 min match → write-AV
  `0xC0000005 at 0x007BA745` (then `0x007BC806`), exit C0000005.
  Lua layer clean to the end (no errors; last log 12:30:35, crash
  12:30:37). Prime suspect correlated-not-proven: bounty-overlay draw
  (`EBX == surfP` of live mark id `1047757`); minidump 0 bytes (dumper
  failure). Item 4 REOPENED per its own rule. **2026-09-22 p.m.: fix
  (Composite-only + once-per-frame guard) user-verified live — clean
  session, no crash, one rectangle. Item 4 re-CLOSED (scoped); session
  details not formally recorded.**
| User-observed | none beyond the Syringe reproductions above.
| Limitations | absence of recorded crashes on the recommended path is
  evidence of stability ON THAT PATH ONLY; it is NOT a disproval of the
  Syringe-path crash (root still UNKNOWN) and not a guarantee under all
  maps/mods/hardware. Formal repeated-test protocol (N clean-machine runs,
  fixed checklist per `FSM/MODDB_ALPHA_RELEASE.md` Installation Gate) has
  NOT been executed — that remains a Gate-3 release requirement.
| **Status** | **Criterion 1.4 CLOSED as scoped** ("retested on the
  recommended launch path; result documented"): the recommended
  CnCNet-spawner path shows zero crash evidence across the documented
  session record; direct-Syringe launches remain intermittently crashy
  (root UNKNOWN, not proven LuaAPI-caused), disclosed in
  `FSM/FEASIBILITY_TRIAGE.md` and now here. Reopen only if a crash
  reproduces on the recommended path. Syringe-path root-cause hunt stays a
  recorded OPEN unknown (not a Gate-1 item). |
