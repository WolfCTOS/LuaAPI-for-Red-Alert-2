# EventClass / MEGAMISSION — Verified Binary Findings

> **Target binary:** `gamemd.exe` — Yuri's Revenge 1.001
> **Purpose:** Document verified reverse-engineering findings relevant to EventClass / MEGAMISSION research.
> **Status:** Research reference — **not a public LuaAPI API contract**.

This document records observations made against the specific `gamemd.exe` build used during the investigation. Addresses and offsets below must be treated as build-specific. They are not stable API addresses and must be revalidated if the executable changes.

---

## 1. Binary Identification

The examined executable was reported as:

- File size: `4,813,072` bytes
- ProductVersion: `1.11`
- Publisher: Westwood Studios
- Target: Yuri's Revenge 1.001

These values identify the binary used for the investigation. They should not be treated as a universal proof that every executable reporting the same version metadata is byte-for-byte identical.

---

## 2. EventClass Function Findings

| Function / Symbol | Status | Address in examined build |
|---|---|---:|
| `EventClass::Execute_DoList` | Verified | `0x64CC68` |
| `EventClass::HandleEvent` | Not established | — |
| EventClass constructors | Partially verified | `0x4C65A0`–`0x4C6C50` range; `0x4C66C0` observed |
| `Print_CRCs_Current_Player` | Reference | `0x64DEA0` |
| `Print_CRCs_All_Players` | Reference | `0x6516F0` |

The previously suspected addresses `0x4F6B40` and `0x4F6D10` should **not** be used as `Execute_DoList` / `HandleEvent` addresses for this build.

`third_party/YRpp/EventClass.h` identifies the engine's out-of-sync sync dump as being called from `Execute_DoList` at `0x64CC68`, providing the basis for the `Execute_DoList` identification.

> **Important:** These addresses are binary-research data. They are not part of LuaAPI's supported public API.

---

## 3. Player Attack Orders Are MEGAMISSION Events

The YRpp event definitions do not expose a dedicated `Attack` event type.

A player attack order is represented as:

```text
EventType::MegaMission = 0x4
MEGAMISSION.Mission    = Mission::Attack = 1
```

Relevant mission values identified in the examined YRpp definitions include:

```text
Mission::Attack     = 1
Mission::AttackMove = 29
```

Therefore, research code looking specifically for a player attack order should distinguish:

```text
EventType == MegaMission (0x4)
        AND
MEGAMISSION.Mission == Mission::Attack (1)
```

This distinction is important because `0x0E` is `Produce`, not `Attack`.

---

## 4. MEGAMISSION Memory Layout

`EventClass` and the relevant structures use packed layouts in the referenced YRpp definitions.

The observed `EventClass` layout is:

```text
EventClass
  +0   Type             u8
  +1   IsExecuted       u8 / bool
  +2   HouseIndex       i8
  +3   Frame            u32
  +7   DataBuffer       104 bytes
```

The referenced definitions report:

```text
sizeof(EventClass) == 111
offsetof(EventClass, DataBuffer) == 7
```

Within the `MEGAMISSION` data beginning at `DataBuffer`, the relevant fields are:

| Field | Offset from EventClass | Size |
|---|---:|---:|
| `Whom` | `+7` | 5 |
| `Mission` | `+12` | 1 |
| padding / gap | `+13` | 1 |
| `Target` | `+14` | 5 |
| `Destination` | `+19` | 5 |
| `Follow` | `+24` | 5 |
| `IsPlanningEvent` | `+29` | 1 |

The referenced `TargetClass` representation is:

```text
m_ID   : int                +0
m_RTTI : unsigned char      +4
sizeof(TargetClass) == 5
```

Consequently, code interpreting raw `EventClass` memory must account for the packed layout rather than assuming normal compiler alignment.

---

## 5. Event Dispatch Investigation

The identified `Execute_DoList` address is the starting point for further event-dispatch research.

Useful investigation steps are:

1. Open the exact target binary in IDA or Ghidra.
2. Inspect `0x64CC68` and confirm the event-list iteration in the current image.
3. Cross-reference the YRpp event-name definitions where useful.
4. Locate the dispatch logic that examines the event type.
5. Confirm the `MegaMission` branch (`0x4`) and the `Mission` field (`+0x0C`) in the actual disassembly before writing a hook.

The string table can assist navigation, but string references alone are not proof that a particular instruction is the desired event hook.

Previously observed strings include:

```text
MEGAMISSION
PLANCOMMIT
RESPONSE_TIME
FRAMESYNC
```

These are useful reverse-engineering landmarks, not API contracts.

---

## 6. What This Research Does — and Does Not — Prove

### Verified by this investigation

- `Execute_DoList` is identified at `0x64CC68` in the examined build.
- `MegaMission` is event type `0x4` in the referenced YRpp definitions.
- `Mission::Attack` is value `1`.
- `Mission::AttackMove` is value `29`.
- The relevant `MEGAMISSION` field offsets are documented above.

### Not established by this document

- A production-safe LuaAPI hook for `HandleEvent`.
- A stable cross-version EventClass ABI.
- A public Lua binding for raw EventClass objects.
- Reliable interception of every player command.
- A complete Z-mode / planning-mode implementation.
- Multiplayer determinism of a future event hook.

These require separate implementation and runtime validation.

---

## 7. Relationship to the LuaAPI Roadmap

This research is relevant to future command/event interception work, including investigation of Z-mode and non-linear waypoint command handling.

It does **not** mean that those capabilities are currently implemented.

When implementation begins, the correct workflow is:

```text
Binary research
      ↓
Disassembly confirmation
      ↓
Minimal native hook
      ↓
Runtime validation
      ↓
Multiplayer / savegame testing
      ↓
Lua binding
      ↓
Documentation
```

Until those steps are completed, EventClass findings remain engineering research rather than a LuaAPI capability.

---

## 8. Source of the Findings

The analysis references the YRpp definitions included in the repository, especially:

```text
third_party/YRpp/EventClass.h
third_party/YRpp/GeneralDefinitions.h
third_party/YRpp/TargetClass.h
```

The final authority for an address is the exact target binary being executed. YRpp definitions provide structural and symbolic context; they do not make an address universally valid across different game builds.
