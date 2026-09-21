# FSM — Future Ideas (archive, not a roadmap)

This file preserves future ideas and research directions so they are not
lost. It is an archive of ideas and investigation boundaries — **not a
development plan**. Nothing here is scheduled, prioritized for
implementation, or claimed as feasible. Status words are load-bearing;
`BLOCKED`/`UNEXPLORED`/`UNDEFINED` mean exactly what they say.

## Iron Curtain: Physical Protection

Status: `CONCEPT / FEASIBILITY BLOCKED`

Concept:

- While Iron Curtain is active, ordinary incoming damage should be
  reduced by 80% — an affected unit takes 20% of the normal damage.
- Psychic attacks must not receive this reduction.
- The idea rests on the distinction between physical protection of the
  machine and effects on the crew/minds inside.
- Counterplay should preferably use existing game mechanics, without a
  separate artificial anti-Iron-Curtain system.

Current limitations (all confirmed against present API/repo; see
`FSM/IRON_CURTAIN.md` §5 for the full analysis):

- Vanilla Iron Curtain activation is not confirmed through current LuaAPI
  (no superweapon namespace exists in bindings).
- Tracking affected units is confirmed (own ID-set + `IsAlive`
  re-resolution pattern).
- Live incoming damage interception is not confirmed (no invocation site
  for `OnPreDamage` with damage arguments exists in source; no
  `ReceiveDamage` hook is installed).
- Psychic damage/type detection is unknown (no payload reaches Lua).
- Direct damage modification is not confirmed.
- `OnPreDamage` remains an OPEN DISCREPANCY between documentation
  (`PROJECT/ROADMAP.md` Milestone 4 marks, `API.md` /
  `PROJECT/CAPABILITIES.md` contract language) and source.

No workaround is proposed here — including no health-polling
compensation presented as a ready solution.

### Requirements before implementation

Future checks, in order. Implementation may only be reconsidered after
all of them resolve:

1. Find a real runtime path for Iron Curtain activation.
2. Find the real damage pipeline as reachable from Lua.
3. Confirm incoming damage interception with arguments.
4. Determine psychic interaction (identification, not heuristics
   presented as identification).
5. Verify multiplayer/determinism of all involved arithmetic and state.
6. Verify burst/overkill/multiple simultaneous attacks behavior
   (lethality races, frame delays, max-HP edges).
7. Only then decide whether implementation without API expansion is
   possible.

## FSM Design Direction

FSM investigates not "which mods can be written" but how deep LuaAPI
can act as a runtime gameplay layer. The following are recorded as
future *categories of investigation* — not as supported capabilities.
The later categories in particular are directions to probe, not claims:

- Runtime combat systems
- Dynamic economy
- Tactical decision systems
- AI decision systems
- Persistent gameplay state
- Player-driven abilities
- Emergent interaction between existing game systems
- Runtime modification of existing vanilla mechanics
- Combat-resolution interception
- Dynamic counterplay systems

## Future mod ideas (placeholders)

### Iron Curtain

Physical damage reduction + psychic vulnerability.

Status: `CONCEPT / FEASIBILITY BLOCKED`

### [Future Idea 1]

`UNDEFINED`

### [Future Idea 2]

`UNDEFINED`

### [Future Idea 3]

`UNDEFINED`

## A future FSM mod should ideally satisfy

- It is a gameplay system, not telemetry.
- The player has a clear decision or interaction.
- The system creates a repeating gameplay loop or emergent behavior.
- It uses the existing LuaAPI.
- It does not duplicate simple Ares/Phobos configuration.
- It does not require API expansion without proven necessity first.
- It has clear counterplay, if the mechanic calls for it.
- It must be verifiable separately via harness and live runtime.
- Capability claims must rest on source/runtime evidence, not on
  documentation alone.

## Current evidence boundary

Standing references for what is actually established:

- `FSM/CAPABILITIES.md`
- `FSM/IRON_CURTAIN.md`
- `FSM/VERIFICATION.md`

Separately noted: `OnPreDamage` remains an OPEN DISCREPANCY.
Older documentation is not corrected as part of this task.

## Future Research Queue

| Idea                    | Status     | Reason                                      |
| ----------------------- | ---------- | ------------------------------------------- |
| Iron Curtain            | BLOCKED    | Damage interception capability not verified |
| New FSM gameplay system | UNEXPLORED | No concept yet                              |
