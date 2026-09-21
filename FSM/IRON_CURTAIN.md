# Iron Curtain Experiment — Concept (NOT IMPLEMENTED)

Working title: **selective Iron Curtain**. The name is kept because the
fantasy is the Soviet superweapon's, but the mechanic differs from both
the vanilla effect and the existing native primitive (see below). No code
exists. No harness exists. Nothing here is claimed as implementable until
the feasibility checks (§5) are resolved — several currently resolve to
NO or UNKNOWN, and that is the honest state of this document.

## Gameplay problem

Vanilla Iron Curtain is binary and boring: press button → units
invulnerable → no decisions during the effect, no counterplay for the
opponent except waiting. A runtime version should create a *window with a
hole*: strong protection with a defined bypass, forcing both sides to
adapt (holder masses armor; opponent reaches for psychic assets).

## Proposed mechanic

- On activation (Lua-defined trigger — cost, radius or target set,
  duration, cooldown; explicitly NOT the vanilla superweapon button,
  for which no API exists), shielded units take **20% of incoming
  conventional damage** (80% reduction).
- **Psychic attacks are exempt** — full damage. Rationale codified in
  the fiction: the curtain shields the hull, not the minds inside.
- Tracking is per-unit (ID set + expiry frames), symmetric for AI
  houses if feasible.

## Intended counterplay

- Holder: timing (activate before an armor push), target selection
  (which units are worth it), cost management.
- Opponent: psychic assets (Yuri Prime, Masterminds, Chaos Drones,
  Psychic Towers) become premium answers; conventional focus fire is
  deliberately devalued during the window; out-waiting the duration
  clock.
- Neither side gets a silent passive: activation is announced (HUD),
  effect has visible feedback, expiry is crisp.

## Why this is a runtime gameplay system

None of this is expressible as static config: the 80%-with-hole rule
requires per-damage discrimination at runtime; the trigger is a match
decision with cost and timing; the AI must evaluate the same window.
INI/warhead arithmetic (`Verses` tables) can set fixed resistances but
cannot condition on a match-state window with a damage-source exception.

## Potentially needed capabilities (existing API only)

- Trigger/state/timers: `Input`/CP-like ledger/`Update` cadence —
  FSM-VERIFIED patterns (CA powers).
- Unit set tracking by ID + `IsAlive` re-resolution — FSM-VERIFIED
  (CA `S.seen`, RCA tracking).
- Damage observation: HP-drop polling — FSM-VERIFIED as observation.
- Damage modification: `unit:TakeDamage` (outgoing only — wrong
  direction), `SetHealthRatio` compensation (lossy, see §5.5),
  `unit:IronCurtain(frames)` (native FULL invulnerability +
  tint, `src/bindings_techno.cpp:1425` — implemented, live use
  unverified; wrong mechanic for the 80% rule, no exception possible).
- HUD: `Engine.PrintMessage` (text only).

## Feasibility checks (current API/repo)

### 1. Moment of activation — PARTIAL (YES for Lua trigger, NO for vanilla button)
- A Lua-defined activation (hotkey/CP purchase/timer) is trivially
  detectable — it is own state (CA powers precedent, harness-verified).
- The **vanilla Iron Curtain superweapon button/fire event has no API**:
  no superweapon namespace exists in `src/` bindings (verified by
  source grep; `SuperClass.h` exists only in vendored YRpp with no
  Lua-facing wrappers). Hooking the vanilla button would need native
  work. The concept therefore assumes a Lua-side trigger. If the
  design ever requires the vanilla button, that check flips to NO
  without an extension.

### 2. Which units are under the effect — YES (with staleness discipline)
- Own ID-set + per-frame `IsAlive` re-resolution is the proven CA/RCA
  pattern. Limits known: snapshot semantics, death/disappearance races,
  savegame restore untested. Sufficient for a duration-window effect.

### 3. Incoming damage — NO for live interception (source-proven)
- The engine collects the *global* `OnPreDamage` reference every frame
  (`src/lua_engine.cpp:994-1026`) but **never invokes it with damage
  arguments** — no invocation site exists repo-wide, and no
  `ReceiveDamage` hook is installed (hook inventory: MainLoop,
  LoadString, Bullet-Detonate, GetPrimaryWeapon, DrawAsVXL;
  ActiveClickWith disabled). Live engine damage does not reach Lua.
- Post-hoc HP-drop polling (CA `combatScan` precedent) CAN observe
  amounts after the fact, but that is aftermath, not an event. Any
  design requiring pre-application interception fails check 3 today.

### 4. Is the damage psychic — UNKNOWN
- No damage-type/warhead/attacker payload reaches Lua (follows from §3:
  there is no interception payload at all). `TakeDamage` *accepts* a
  warhead name as an *input* (outgoing), which is the wrong direction.
  No `IsPsychicAttack`-style getter exists. Inferring the source from
  geometry (nearest attacker) is a heuristic, not identification —
  and must not be presented as such.

### 5. Apply damage modification without API extension — NO for true
    interception; PARTIAL workarounds with hard limits
- True interception (replace damage value pre-application): NO —
  follows from §3.
- Post-hoc compensation via `SetHealthRatio`: POSSIBLE but lossy —
  restore 80% of the observed drop on the next scan. Hard limits:
  (a) frame-delayed (damage lands first); (b) lethal blows are
  unpreventable (dead objects fail validation); (c) the binding takes
  effectively a **percent scale** (divides by 100;
  `src/bindings_techno.cpp:1127-1131` — fractional 0–1 inputs
  truncate, cf. the CA `1.0`→1% incident); (d) max-HP edge cases.
  Playable? Unproven — needs a prototype + live measurement, and the
  lethality hole may be a design-breaker for a "protection" fantasy.
- Native `unit:IronCurtain(frames)`: real invulnerability + tint, but
  binary (no 80%, no psychic hole). Using it would abandon the
  concept's core rule. It is an alternative mechanic, not an
  implementation of this one.

## Checks required before implementation

1. Decide interception-vs-compensation honestly: if the lethality hole
   or the frame delay breaks the fantasy in prototype, stop — do not
   relabel compensation as interception.
2. Resolve the psychic rule without identification: candidate fallbacks
   are (a) attacker-type allowlist via `GetTypeName` of *suspected*
   attackers (heuristic, needs validation), (b) restricting the
   exemption to specific known psychic *units* observed near the
   victim (heuristic), (c) dropping the exemption (kills the concept's
   counterplay — then it isn't this mod anymore).
3. Prototype the compensation math at percent scale with lethal-blow
   accounting; measure live HP trajectories in a real match.
4. MP determinism review of all HP arithmetic before any 2-client claim.
5. UX contract first: trigger, cost, duration, cooldown, selection,
   announcement, expiry feedback — per PRINCIPLES (decisions +
   counterplay), not as an afterthought.
6. Re-check the `OnPreDamage` discrepancy: if a future build wires live
   interception, this entire document must be revised (checks 3–5 flip
   to YES and the mod becomes directly implementable).
