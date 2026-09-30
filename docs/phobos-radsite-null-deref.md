# Null-pointer dereference in a per-object walk (Phobos RVA 0x6E0AA)

## Summary

A `RadSiteClass` constructed directly from an injected DLL — outside Phobos' own
warhead path — is accepted by the engine, renders correctly as a vanilla-green
site, and then faults the process on the next frame while Phobos walks a
per-object collection. The fault is an unchecked three-level pointer chain where
only the first link is null-checked.

## Environment

| | |
|---|---|
| Game | Yuri's Revenge 1.001, `gamemd.exe` (image `0x00400000`..`0x00B93000`) |
| Phobos | 0.4.0.2, "Release Build 0.4.0.2" |
| SyringeEx | 0.1.0.2 (2767 hooks) |
| Also loaded | `Ares.dll`, `CnCNet-Spawner.dll` (CnCNetYR client) |
| Process | `gamemd-spawn.exe` |

## How the site is created

The sequence is the one Phobos/the engine use, taken from the callers of
`RadSiteClass::Activate` (`0x4691F4`, `0x46AE3F`):

```
ctor
SetBaseCell(cell)     // 0x65B4C0
SetSpread(60)         // 0x65B4D0
SetRadLevel(500)      // 0x65B4F0
Activate()            // 0x65B580
```

`Add()` is not called, matching the engine's own sequence.

## Observed behaviour

1. The site is created successfully.
2. It renders. The engine paints it **vanilla green across its whole radius** —
   this is the engine's own colour for a `RadSiteClass`; no tint or light
   fields are written by the injector. Identical to a Desolator detonation.
3. Roughly one frame later the process faults.

```
0xC0000005, read of 0x00000090
EDI = 0x00000000
EIP = <Phobos base> + 0x6E0AA     (RVA 0x6E0AA)
```

A second fault was also observed at RVA `0x6E752`, in the same function.

## Disassembly (Phobos 0.4.0.2, function entry RVA 0x6DF30)

The function takes a two-pointer context, dereferences a vtable slot at `+0x1BC`,
resolves a collection, then walks it:

```
0x6DF30  push ebp
         mov  ebp, esp
         sub  esp, 8
         mov  edi, [ebp+8+0xC]
         mov  eax, [ebp+8+0x10]
         mov  ebx, [edi+0x130]
         mov  esi, [eax+8]
         ...
         call 0x6D680
0x6E07C  mov  edx, [esi+0x1C]      ; vector.last
0x6E07F  mov  esi, [esi+0x18]      ; vector.first
0x6E082  mov  [esp+0x38], edx
0x6E086  cmp  esi, edx
0x6E088  je   0x6E1A5
0x6E090  cmp  dword [esi+4], 0
0x6E094  jle  0x6E19A
0x6E09A  mov  eax, [esi]           ; P1 = elem->obj
0x6E09C  test eax, eax             ; <-- the only null check
0x6E09E  jne  0x6E0A4
0x6E0A0  xor  edi, edi
0x6E0A2  jmp  0x6E0A7
0x6E0A4  mov  edi, [eax+0x18]      ; P2 = P1->+0x18   not checked
0x6E0A7  mov  edi, [edi+0x10]      ; P3 = P2->+0x10   not checked
0x6E0AA  cmp  byte [edi+0x90], 0   ; <== FAULT
```

The collection is a `std::vector` of 8-byte elements `{void* obj; int value;}`
(MSVC layout: first `+0x18`, last `+0x1C`, cap `+0x20`).

## Which link is null

The fault address identifies it uniquely:

| null link | fault would be at |
|---|---|
| P1 | `0x10` (the `0x6E0A7` read) |
| P2 | `0x18` (the `0x6E0A4` read) |
| **P3** | **`0x90`** — observed |

With `EDI = 0` and a fault at `0x90`, **P3 = `P2->+0x10` is NULL** while P1 and
P2 are valid pointers.

## Controls

- **No site created → no fault.** A run with the creation path disabled completes
  a full match with no access violation, so the walk itself is not broken in
  general; it needs this object present.
- **One site is enough.** Reducing the site count from many to a single zone does
  not avoid the crash, so it is not a volume or lifetime problem.
- **The engine's own Desolator never triggers it.** A site created by an actual
  warhead detonation is walked without fault, so the walk tolerates a
  `RadSiteClass` — just not one in this state.
- The site renders correctly, so the object is the right class and its visible
  state is right. Only the third link is missing.

## Questions

1. Which collection is this, and which object in it is expected to always carry
   a valid `+0x18->+0x10`?
2. Is a `RadSiteClass` expected to appear in this collection at all, or is it a
   Phobos-maintained list that our directly-constructed site is entering by
   accident?
3. Regardless of the above, `P2` and `P3` are dereferenced without a check at
   `0x6E0A4` and `0x6E0A7` — should the walk guard them like it guards P1?
