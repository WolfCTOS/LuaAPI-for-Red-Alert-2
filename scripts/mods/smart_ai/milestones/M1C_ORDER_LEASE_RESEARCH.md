# M1-C — Order Leases → DU-1 Research (specification only)

> **Status:** RESEARCH. Nothing implemented. No runtime changes. No SmartAI
> behavior changes. This document feeds DU-1; it does NOT implement it.
> Evidence grades: SOURCE VERIFIED (code/YRpp) / LIVE OBSERVED (log + ids +
> frames) / ENGINE-KNOWLEDGE (medium, YR community model) / UNKNOWN.

## 0. Lease model under study (model only — NOT in runtime)

```text
SmartAI issues order
        ↓
lease starts (frame, unit, order, target, conditions)
        ↓
order remains valid while lease conditions hold
        ↓
read-back / state check (existing scan cycles only)
        ↓
one of:
  - order completed      (target dead / destination reached / mission done)
  - lease expired        (frame budget spent, no re-issue by default)
  - target invalid       (dead/gone/captured)
  - external overwrite   (observed state contradicts lease, target alive)
  - explicit SmartAI cancellation (a newer SmartAI decision replaces it)
```

Rule: observation alone ("unit changed behavior") NEVER proves overwrite.
Required minimum: frame + unit id + issued order + observed state. Cause
(kill vs retask) needs one more fact: commanded-target liveness at the
divergence scan (see §6, §9).

## 1. What Vanilla AI does (order lifecycle)

- **Missions are per-class virtuals**: `Mission_Attack/Move/Guard/Hunt/
  Harvest/Enter/...` (`YRpp/MissionClass.h:66-78`); `FootClass` adds
  `MegaMission` (`FootClass.h:182`). SOURCE VERIFIED (headers).
- **Teams run scripts stepwise**: `TeamClass::{CurrentScript, StepCompleted,
  TargetNotAssigned, AssignMissionTarget(), Target}` (`TeamClass.h:45-107`).
  A team advances script lines, assigns mission targets, regroups/recruits
  (`NeedsReGrouping`, `CanRecruitUnit`). SOURCE VERIFIED (headers).
- **Re-issue is normal engine behavior**: documented project finding —
  vanilla reissues harvest/movement orders every frame, which is why
  `unit:Stop()` alone cannot hold enemy miners (`ROADMAP.md` M12 Gate 12.3
  notes). Previously LIVE OBSERVED (cited record).
- **Guard auto-acquire + wave retargeting**: engine-knowledge (MEDIUM) —
  Guard-mission units engage in-range enemies on their own; attack waves
  re-pick targets as the battle evolves. No repo-internal proof; consistent
  with all live sessions.
- **No Lua-visible AI internals**: zero C++ references to
  Team/TaskForce/Script/AITrigger/Production bindings (grep-verified
  2026-09-23); `Active_Click_With` interception is disabled
  (`ENGINEERING_LESSONS.md` §9). Vanilla decisions are unwired by design.

## 2. Current SmartAI order lifecycle (all 8 sites, `smart_ai/main.lua`)

Issue (`orderMove` :231 / `orderAttack` :236, pcall-guarded) →
immediate read-back **only for tier-1 Attack** (`readbackTargetId`, GetTarget
→ id, :266-272) → refresh caps per layer (150–600f) → divergence watch
**only for tier-1 defenders** (compares commanded vs reported target, logs
transitions, never re-orders) → hygiene release on death/disappear/capture.
There is NO completion detection, NO lease object, NO re-issue logic
(except cap-gated refreshes), NO target tracking outside tier-1, NO order
source tagging. Rally issues `MoveTo`+`Hunt` back-to-back (known suspect:
second mission wins — a SELF-overwrite by construction, live effect open).

Observable today (existing API only): issued order (own `[SMARTAI][ORDER]`
lines with frame/site/unit/target), current target (`GetTarget` →
`bindings_techno.cpp:1098`), mission state (`GetMission/IsIdle/IsAttacking`),
frame (`Update(frame)`), owner (`GetOwner`). NOT observable: who issued the
current order, mission-change events, order-queue contents, re-task notices.

## 3. Known order sources (this stack)

1. **Vanilla AI** — everything (§1): waves, scripts, Guard, re-issue.
2. **SmartAI** — 8 sites: GUARD_PULLBACK, POINT_DEFENSE (`Attack`),
   MARCH_RECALL, ESCORT (+REFRESH), GARRISON, RALLY_BREACH (`MoveTo+Hunt`),
   IDLE_RECALL. All id+frame logged (`[SMARTAI][ORDER]`).
3. **target_reselect** — `Attack`/`MoveTo` on AI attackers of player
   harvesters + `GetTarget` read-back, 150f anti-spam cooldown
   (`HOW_TO_USE.txt:26-40`). Own tags `[TARGET]`/`[M14.1]`. Live 2026-09-10:
   8 redirects accepted, zero errors — acceptance proven, persistence
   explicitly Need-to-test (`ROADMAP.md` M14).
4. **bounty_hunter** — none (credits only).
5. **Framework Task/UnitController** — exist, zero live consumers.
6. **Human player** — own house only; no conflict surface with AI-house orders.

## 4. Proven overwrite cases (evidence bar honored)

- **P1 — Rally self-overwrite (structural, SOURCE VERIFIED):** `MoveTo`
  immediately followed by `Hunt` (:730-731); the engine keeps the last
  mission. Mechanism proven by construction; live effect (reserve hunts
  from place) still open. Not vanilla — own collision.
- **P2 — Vanilla harvest re-issue beats `Stop` (cited record):**
  `ROADMAP.md` M12 notes. Pattern proven historically; not re-proven here.
- **P3 — Divergence WITH plausible natural cause (LIVE OBSERVED 21:48 run):**
  defender 1052709 commanded E1#1052466 → actual E1#1052464 (adjacent id,
  same type): consistent with kill-then-reacquire = NATURAL COMPLETION,
  not overwrite. Counter-case showing why divergence alone proves nothing.
- **P4 — March-recall 5s repeats, HTNK/TTNK (LIVE OBSERVED 16:04 run):**
  consistent with ping-pong, slow march, or death (one TTNK was bounty-
  claimed mid-window). Per the §0 rule: NOT classified as overwrite.
- **P5 — Accepted ≠ held (M14.1 live 2026-09-10):** 8/8 `Attack` accepted
  with matching read-back; "vanilla does not immediately re-select" stayed
  Need-to-test. Read-back proves execution, never persistence.

## 5. Unknowns / instrumentation gaps

- Commanded target alive or dead at divergence scan → cause unknown for
  actuals 1051251/1051252/1051888/1056898 (divergence lines lack
  actual-owner/type AND liveness correlation).
- V3#1052370 post-rally drift to (74,88): vanilla wave pickup vs arrival
  vs re-task — indistinguishable today.
- Mission at divergence (Guard vs Attack vs Move) is readable (`GetMission`)
  but not yet recorded alongside divergences.
- Team-membership churn cause (recruit vs regroup vs death) — M1-A
  snapshots members; cause inference not built.

## 6. Proposed DU-1 contract (specification, not code)

- **Lease object (Lua-side, future):** `{unit, order, target, sinceFrame,
  budgetFrames, conditions}`. States: HELD → COMPLETED | EXPIRED |
  INVALID | OVERWRITTEN | CANCELLED. Transitions ONLY on observed facts
  (table: COMPLETED needs target-dead/arrival proof; OVERWRITTEN needs
  alive-target + mission/target contradiction + no SmartAI re-issue since).
- **Reads already available:** issuance log, `GetTarget`, `GetMission`,
  `IsIdle/IsAttacking`, liveness, frame, owner, M1-A team snapshots.
- **Reads missing:** issuer/source tag, mission-change events, queue
  contents, re-task notices (see §7).
- **Hard rules:** never infer overwrite from behavior alone; never auto
  re-issue on divergence (diagnose first); one lease per unit (newest
  SmartAI decision cancels the older → CANCELLED, not overwrite).

## 7. Required future API/hooks (candidates, NOT commitments)

- Issuer/source tagging or order-interception point: needs a native hook
  where `Active_Click_With` failed — candidates per `ENGINEERING_LESSONS.md`
  §9 (`SetTarget`/`QueueMission` interception, input level). Research-grade
  risk (detour fault domain precedent). NO commitment in M1.
- Everything else in §6 is expressible with existing bindings + M1-A reads.
- Ares/Phobos duplication check required before any native work
  (`DYNAMIC_UNIT_BEHAVIOR.md` boundary; `API_FREEZE_AUDIT.md` M14.7).

## 8. Explicitly out of scope

New tactics, re-issue logic, production writes, formation API, Vanilla AI
changes, Ares/Phobos changes, new LuaAPI milestones, roadmap changes,
balance changes, automatic retries, unrelated SmartAI refactors.

## 9. Recommended next runtime experiment (proposal only)

"Divergence-with-cause": extend ONLY the divergence watch (diagnostic) to
record, at the divergence scan, (a) commanded-target liveness via ID-diff,
(b) defender `GetMission`. Classify: target dead → natural; target alive +
mission flipped with no SmartAI re-issue → overwrite candidate. No
re-orders. Single session reusing the parked-army + breach scenario. Needs
a SmartAI diagnostic edit + harness asserts first — NOT done here.
