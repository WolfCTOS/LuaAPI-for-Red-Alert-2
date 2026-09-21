# QueueUnit Gate

## Purpose

Decide whether `AI.QueueUnit` works as a real Lua production command
for Counter-Composition Director — i.e. the full chain Lua → request
→ engine accepts → unit enters production → unit is produced →
discoverable via World API with correct owner/type. No new bindings
were added; the probe used only existing API. The temp probe mod was
removed after the run; the default mod stack is restored.

## Source Evidence

- `AI.QueueUnit(house, typeId)` / `AI.CountUnit(house, typeId)`
  (`src/bindings_production.cpp:33-124`): argument-checked house userdata
  + `TechnoTypeClass::Find`; loops `FactoryClass::Array` for the
  house's factories; `DemandProduction(pType, pHouse, true)`, first
  accepting factory wins; boolean return. `CountUnit` sums
  `CountTotal(pType)` over the same factories.
- Native: `FactoryClass::DemandProduction(pType, pOwner, shouldQueue)`
  / `CountTotal(pType)` (`third_party/YRpp/FactoryClass.h:41,82`).
- What source does NOT define: accept/reject semantics when busy or
  broke, queue depth/ordering, multi-factory arbitration, production
  timing, what `CountTotal` counts exactly, or any faction/type
  validation. Those live in the binary — the precise unknowns this
  gate probed.

## Existing API

All exercised calls pre-exist: `AI.QueueUnit/CountUnit`,
`House.GetCount/GetByIndex`, `house:GetName/IsHuman/GetCredits`,
`World.GetBuildings/GetAllUnits`, `unit:IsAlive/GetId/GetTypeName/
GetKind/GetOwner/GetPosition/GetMission`, `Engine.PrintMessage`.
`World.GetBuildings` was nil-guarded (absent in older harnesses).

## Harness

Deliberately none: mock factories cannot prove engine production
semantics — a mock harness would only re-prove Lua decision math
already covered by CA patterns. The gate went straight to live,
which is the only probative level for this question.

## Live Test

- Setup: headless CnCNet-spawn match (`injector --attach` + Syringe,
  `spawn.ini` → Dannath Revisited), houses Africans (AI) / Neutral /
  Special / YuriCountry (human-flagged slot). Temp mod
  `queueunit_probe` appended last to `active_mods.txt`, removed after.
- Factory found: Soviet `NAHAND` barracks, Africans-owned, at 104,95,
  frame 900. No orders issued, no combat interference (2–4 cheap
  units max footprint).
- Requests (frame 900): infantry `INIT` → `true`; vehicle `LTNK` →
  `true`; immediate `INIT` repeat → `true`. Baselines:
  `CountUnit(INIT)=1`, `CountUnit(LTNK)=0`, house credits 98530.
- Watch: 201 polls × 60 frames (~3.4 min, to frame 12901):
  `CountUnit` 0/0 throughout, zero new alive units of requested
  type+owner (ID-scan appearance detector, type+owner matched),
  credits drifting 98k→88k (AI's own spending — funds never a blocker).
- Failures: no appearances; the single initial count of 1 vanished
  without a matching unit; no errors in log (probe paths executed —
  WATCH lines every poll).

## Evidence Level

- SOURCE VERIFIED (bindings + signatures + exact semantics above).
- HARNESS: n/a by design (non-probative here).
- LIVE VERIFICATION: CONDUCTED TWICE — v1 (mismatched types) and v2
  (faction-authentic, below). `LIVE VERIFICATION: NOT COMPLETED`
  for the production pipeline as a usable command; the v2 negative
  is clean enough to close the gate against it (see Status).

## Live Test (v2, faction-authentic)

Same infrastructure, improved temp probe (removed after the run):
waited for a mature base (frame ≥ 12000), derived requested types
from the test house's LIVE fielded army (most common infantry-kind
+ vehicle-kind TypeNames), censused AI production in-window.

- Setup: Africans (Soviet AI) mature base — `NAHAND` + `NAWEAP`
  structures; live army included `E2` infantry and `HTNK` vehicles.
- Requests: `E2` → `true`; `HTNK` → `true`. Baselines recorded
  (`CountUnit`, credits ~87k, ample).
- AI-production census (every 600f): factory output visibly working
  in-window (`HTNK` counts churning 26–30, plus `HARV`, `FLAKT`,
  `SLAV`, `HTK`, `DTRUCK` activity).
- Watch (~200 polls × 60f): `CountUnit` steady 0/0 after one
  transient blip; **zero `E2` appearances** (the clean channel —
  AI builds no E2 itself, so any appearance would be attributable).
- Vehicle channel polluted by design flaw (v2's own): requesting the
  AI's most common vehicle (`HTNK`) is indistinguishable from its own
  mass production — dozens of new Africans `HTNK` appeared and none
  can be attributed to the request. Documented, not used as evidence
  either way.
- Net v2 finding: faction-authentic infantry, accepted, from an
  idle-ish barracks (AI infantry output ~zero in-window), ample
  funds, zero output in 3+ min. Remaining explanations —
  accept-then-sink vs AI scrubbing foreign queue items — are
  indistinguishable live, but BOTH equal pipeline failure for any
  consumer: the observable contract "request X → X appears" does not
  hold either way.

## Results

1. Production structure available — PASS (`NAHAND` found live).
2. Lua calls existing production API — PASS (no new bindings).
3. Specific unit requested — PASS (`INIT`, `LTNK`).
4. Request accepted — PASS (`true` × 3, incl. repeat).
5. Count/state changes — PARTIAL (one transient `1`, then steady `0`;
   unattributable: ours or the AI's own queue).
6. Unit actually appears — FAIL (none in ~3.4 min).
7. Discoverable via World API — UNTESTED (nothing appeared; detector
   ran 201 live polls without errors — negative control only).
8. Owner/type correct — UNTESTED (nothing appeared).
- Busy: untested (early-match request, factory likely idle — which
  cuts against the busy-queue excuse, but factory operational state
  itself was unverified).
- Funds: ample throughout — not a blocker here.
- Repeat: accepted identically — no additional signal.

v2 deltas (faction-authentic, mature base): requests `E2` + `HTNK`
(both `true`); `CountUnit` steady 0/0; AI-production census proves
the factory output works in-window (its own `HTNK` churn); zero
`E2` appearances (clean channel); `HTNK` appearances unattributable
(AI mass-produces it — design pollution, excluded from evidence).
Verdict for items 6–8 stands (FAIL / UNTESTED / UNTESTED); item 5
stays PARTIAL; the mismatch caveat is closed.

## Limitations

- v1 flaw (faction mismatch) is SUPERSEDED by v2 (authentic types) —
  retained above only as method history, not as an open question.
- v2 vehicle channel: self-polluted (most-common type == AI's own
  mass production) — excluded from evidence in both directions.
- Single house/faction/timing per run (two runs total); appearance
  detector has no positive control (nothing ever appeared to catch);
  `CountTotal` semantics remain opaque (transient blips only).
- Overturn test (not run, optional): a rare-but-authentic type the AI
  never builds (e.g. `APOC`, funds permitting, Battle Lab
  permitting) with full attribution. A single attributed appearance
  would reopen this gate. Nothing in v1/v2 suggests it would succeed,
  so no further runs are scheduled on this question.

## Counter-Composition Status

`BLOCKED` — for the production dependency (not for the idea's
decision layer, which stays harness-ready). Two live runs agree:
requests are accepted (`true`, incl. repeats) with zero attributable
output — v1 under mismatch (acceptance validates nothing), v2 under
authentic types from an idle-ish barracks with ample funds and a
working factory in-window. Whether the mechanism is accept-then-sink
or AI scrubbing of foreign queue items is unobservable live and,
for planning purposes, equivalent: the contract a Director needs
(request X → X appears) does not hold. Reopen only via the specified
overturn test with a positive result.
(`FSM/FEASIBILITY_TRIAGE.md` Counter-Composition section updated
accordingly; Aegis/Sector untouched.)

## Documentation Changes

Made in this task (minimal, style-preserving, no code touched):

- `API.md`: added `unit:Attack(target)` and `unit:GetMission()`
  (implemented, previously undocumented); corrected
  `unit:SetHealthRatio` to percent scale + no return value;
  rewrote Callback Model (mod-table framing → global lookup;
  `OnPreDamage` marked NOT WIRED with intended-contract wording;
  `OnScenarioStart` frame-1 dispatch; `OnUnitDestroyed` documented
  as `(nil, nil)` placeholder with nil-guard recipe).
- `PROJECT/CAPABILITIES.md`: Case Study 1/2 headers corrected
  (contract-documented vs live; inert-mod note); recipes converted
  to global form with staleness notes; `PrintMessage` color args
  removed (binding is text-only).
- `PROJECT/ROADMAP.md`: Gates 4.1–4.3 annotated with the source
  finding (no invocation site — more precise than "races"; M13
  pointer kept; marks untouched); Gate 9.2 annotated (v1.0 mark vs
  current Alpha operation + ModDB gate pointer).
- `FSM/FEASIBILITY_TRIAGE.md`: Counter-Composition section only
  (live micro-gate box + revised status; v2 run promoted the status
  to `BLOCKED` for the production dependency).

## Remaining Release Blockers

Against `FSM/MODDB_ALPHA_RELEASE.md` (unchanged by this task except
as noted):

- Stale house cache / dead `ResetSession` — untouched (still open).
- Docs-vs-source — PARTIALLY CLOSED by this task (Attack/GetMission/
  SetHealthRatio/OnPreDamage recipes/ROADMAP M4+M9 notes). Remaining:
  `README` install review, `TUTORIAL.md` sweep, undocumented extras
  (`WeaponExists`, HUD-mute, `WeaponOverride` Set/Get/Clear).
- Inert `bounty_hunter` in defaults — untouched (still open).
- `0xC0000005` retest on recommended path — untouched (still open).
- New: none introduced (probe mod removed, mod stack restored,
  no gameplay/C++/API changes made).
