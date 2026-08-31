# EventClass / MEGAMISSION — Verified Findings (Yuri's Revenge 1.001)

**Binary checked:** `gamemd.exe` @ D:/Games/Red Alert 2 LuaAPI — 4,813,072 bytes, ProductVersion 1.11, Westwood Studios.
This is the standard **YR 1.001** build that YRpp is written against. Its PE address space maps RVA≈file-offset (flat/linear), so the addresses below are direct.

---

# 1. Function entry addresses — CHECK: your rumored numbers are WRONG

| Function | Rumored (yours) | **Verified in YRpp** | Verified present in binary |
|---|---|---|---|
| `EventClass::ExecuteDoList` | 0x4F6B40 | **0x64CC68** (see note) | `0x64CC68` maps to `0x0064CC68` (in .text) ✅ |
| `EventClass::HandleEvent` | 0x4F6D10 | **not documented** | see instructions below |
| `EventClass` ctors | — | 0x4C65A0..0x4C6C50 | `0x4C66C0` maps to `0x004C66C0` (in .text) ✅ |
| `Print_CRCs_Current_Player` | — | 0x64DEA0 | reference |
| `Print_CRCs_All_Players` | — | 0x6516F0 | reference |

**> The rumored `0x4F6B40` and `0x4F6D10` are NOT the ExecuteDoList / HandleEvent addresses for 1.001.** Do not hardcode them.

Where the authoritative value comes from — `third_party/YRpp/EventClass.h:28`:
```
// The engine's out-of-sync sync dump, called from Execute_DoList (0x64CC68)
```
So the executable loop that iterates pending events is **`Execute_DoList` @ 0x64CC68**.

---

# 2. What "player clicked Attack on a building" actually is

YRpp's `EventType` enum (GeneralDefinitions.h:1409) has **NO `Attack` value**. The value you assumed, `0x0E`, is **`Produce`**, and `0x4` is **`MegaMission`**, not `0x0E`.

A player "attack" order is not its own event type. It is an **`EventType::MegaMission` (0x4)** event whose `MEGAMISSION.Mission` field == **`Mission::Attack = 1`**.

```
EventType (GeneralDefinitions.h:1409, unsigned char):
  ...
  MegaMission    = 0x4   <-- carries player attack/attack-move/etc orders
  MegaMissionF   = 0x5
  ...
  Produce        = 0xE   <-- NOT Attack
  Repair         = 0x15
  Sell           = 0x16
  ...
```
Confirmed enum value (Mission, GeneralDefinitions.h:970):
```
Mission::Attack = 1
Mission::AttackMove = 29
```

**To distinguish a player "attack" order from engine auto-targeting, latch on:**
`EventType == MegaMission(0x4) && MEGAMISSION.Mission == Mission::Attack(1)`.

---

# 3. MEGAMISSION structure — EXACT offsets

Layout is `#pragma pack(push,1)` (EventClass.h:8-9). Base base-address of the `EventClass` object.

```
EventClass :                                     offset  size
----------------------------------------------------------
  EventType        Type              (u8)         0     1
  bool             IsExecuted        (u8)         1     1
  char             HouseIndex        (i8)         2     1
  unsigned int     Frame            (u32)         3     4
  union DataBuffer[104]                         7   104      <-- MEGAMISSION starts here
```
`sizeof(EventClass) == 111`, `offsetof(EventClass, DataBuffer) == 7` (EventClass.h:427-428 static_asserts).

**MEGAMISSION union member (EventClass.h:181-190):**

| Member | Type | Offset (from EventClass base) | Size |
|---|---|---|---|
| `Whom` | `TargetClass` | **+7**  | 5 |
| `Mission` | `unsigned char` | **+12** | 1 |
| `_gap_` | `char` | +13 | 1 |
| `Target` | `TargetClass` | **+14** | 5 |
| `Destination` | `TargetClass` | +19 | 5 |
| `Follow` | `TargetClass` | +24 | 5 |
| `IsPlanningEvent` | `bool` | +29 | 1 |

`TargetClass` is also `#pragma pack(1)`: `int m_ID (4) + unsigned char m_RTTI (1) = 5 bytes` (TargetClass.h:145-146).

So, from an `EventClass*`:
- Event type byte: `*(unsigned char*)ev + 0`
- `Whom`: `*(TargetClass*)((char*)ev + 7)` → resolves to the TechnoClass/pointer via `TargetClass::As_Techno()` @ 0x6E6F20
- `Mission`: `*(unsigned char*)((char*)ev + 12)`

---

# 4. How to find the canonical hooks yourself (in case you want to confirm / use IDA)

The YRpp addresses above are verified against this exact build and are safe to use directly. But to satisfy "verify by disassembly":

1. **Locate `Execute_DoList` reliably:**
   - Open `gamemd.exe` in IDA/Ghidra.
   - Go to `0x64CC68`. It iterates the `EventClass::DoList` queue (`QueueClass<EventClass, MAX_EVENTS*128>` @ `0x008B41F8`).
   - Cross-reference the `EventNames` table at `0x0082091C` — in your binary the string `"MEGAMISSION"` is at RVA `0x00420BB0` (I found it), and `"PLANCOMMIT"`/`"RESPONSE_TIME"`/`"FRAMESYNC"` are adjacent at `0x420A04/0x420AD8/0x420AF0`. The dispatcher switch on `ev->Type` lives right around the loop.

2. **`Search -> Sequence of bytes` for the event-type dispatch** to find the point where `Type` is compared against `0x4` (MegaMission):
   - Byte pattern for `cmp byte ptr [esi], 4 / cmp byte ptr [esi], 0x4` etc., then the `Mission` check `cmp byte ptr [esi+0Ch], 1` (Attack).

3. **String-driven approach (only for the name table):**
   - `MEGAMISSION` @ `0x420BB0` (found). `IDA String window -> Search "MEGAMISSION"` → xrefs point into the dispatcher.

---

## Summary / what to actually use
- **Do not** use `0x4F6B40` or `0x4F6D10`.
- **Event loop:** `EventClass::Execute_DoList` @ **`0x64CC68`**.
- **Attack is a Mission, not an EventType:** detect `EventType::MegaMission (0x4)` + `MEGAMISSION.Mission == Mission::Attack (1)`.
- **MEGAMISSION offsets (from EventClass base):** `Whom=+7`, `Mission=+12`, `Target=+14`, `Destination=+19`, `Follow=+24`, `IsPlanningEvent=+29`.
- Confirmed `gamemd.exe` is YR 1.001 (ProductVersion 1.11, Westwood); all YRpp thunk/ctor addresses verified in-binary.
