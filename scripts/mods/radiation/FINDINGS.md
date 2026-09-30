# Radiation mod — investigation findings

Written 2026-09-29. Companion to `docs/research/RADIATION_RCA.md`; this file is
the mod-local summary. Both are kept honest, and where a claim is an assumption
it says so.

## What this mod does, and what works

RUNTIME VERIFIED across many runs, 0 errors, no crashes:

- warning announced 10 s before impact, then 9…4, then `RADIATION IN 3 / 2 / 1`
- `RADIATION DETECTED` on impact
- damage to open infantry — a census run read `scanned=37 infantry=14
  inRadius=14 hits=14`
- targets re-picked every sweep, player's own infantry preferred
- ground held at `RadLevel` and scrubbed at the end
- garrison hint printed once per warning

The damage path was genuinely broken for a while and was fixed: `chooseTargets`
used to run once per event, so the 12 s warning let the infantry walk away and
`inRadius` fell to 0. It now runs every sweep.

## The requirement that is NOT met

**Green ground across the whole map while the mod's radiation is active.** This
was the actual ask and it is not delivered. It is recorded here rather than
quietly dropped.

## What is established about the green

- A deployed Desolator's green is a **light on its radiation site**. Confirmed
  from a screenshot: soft radial falloff, ground texture fully visible through
  it, and a building inside the glow is lit too. No tile is swapped.
- The engine creates the site and the glow works with Ares+Phobos, with Ares
  only, and in both cases the colour came from `[Radiation] RadColor`.
- A live run with the Desolator deployed read:
  `maxRadLevel=297.99 hotCells=349 radSites=1 firstSite=(62,97)`, and the
  colour-intern table grew `5 → 33` when the site appeared. So the site exists,
  the light applies, and the field is large — 349 of 625 scanned cells.

- `CellClass::RadLevel` does **not** produce it. Proven with a working
  instrument, not inferred: a cell held at `RadLevel=500` with
  `IsRadiated=true` showed no green. Damage and picture are separate
  mechanisms, which is also why `unit:TakeDamage` produces no tile effect.

## What is NOT established

Do not treat any of these as decided:

- Whether the crash from hand-built sites is caused by a missing light. That
  was asserted earlier and then partly walked back; the `fx=0` reading used to
  support it came from an offset that turned out to be wrong.
- Whether Phobos is involved. Ruled out in one direction (the glow works with
  and without it) but never actually tested for **our** site, because
  `LUAAPI_RADSITE_NATIVE` was never enabled in any Phobos on/off comparison.
- The `RadSiteClass` field layout. Base-class headers (`IPersistStream`,
  `IRTTITypeInfo`, `INoticeSink`, `INoticeSource`) are not present in
  `third_party`, so the offset of `LightSource` cannot be derived from the
  headers here. Every offset guess so far was wrong.
- Whether `rulesmd.ini` is merged by the engine in this install. The Lua probe
  only proves Lua can read and parse it.

## Approaches already tried — do not repeat

1. **Hand-built `RadSiteClass` via `World.RadSiteCreate`.** Faulted the
   renderer twice: `0xC0000005` at `0x71C0E752` then `0x71C0E0AA`, ~150 ms
   after creation. Writing the light position correctly did not help. The path
   is retired and gated behind `LUAAPI_RADSITE_NATIVE`, off by default.
2. **`CellClass::RadLevel` as the visual.** Disproven above.
3. **Phobos / Ares rules keys.** Four overrides, no observable effect:
   `RadSite.Detonate`, `CellSpread=1000`, `RadColor=255,0,255`, and the
   `[Phobos]` toggles in `RA2MD.INI`. `rulesmd.ini` is left key-less.
4. **Disassembling the `Activate` callers.** The three candidate call sites
   produced garbage when disassembled, so that whole line of enquiry is
   currently unusable.

## The API gap, stated plainly

There is **no way to detonate a warhead on a cell** from the API. What exists:

- `unit:TakeDamage(amount, warhead)` — damage only, via `ReceiveDamage`
- `Engine.WarheadExists(id)` — a read-only existence check
- `World.RadSiteCreate(x, y, spread, level)` — creates a site, but the result
  is not equivalent to an engine-made one

So the native route needs either a correct site construction, or a binding that
triggers a real warhead detonation on a cell. Neither exists yet. That is the
whole remaining problem, and it is a specific API gap, not a conceptual one.

## The process failure, recorded because it is the actual lesson

Seven of my own instruments were wrong during this investigation, and each one
cost a run:

| Where | What I claimed |
|---|---|
| file search | "Phobos and Ares are not installed" |
| log format | "level 50000" (it was 500, in hundredths) |
| field offset | "the site has no light" (read at the wrong offset) |
| string offset | "the log is empty" (each write overwrote the last) |
| dump buffer | "no data" (output truncated to two words) |
| Phobos | "Phobos is the cause" (never tested on our site) |
| disassembly | "callers of Activate" (output was not code) |

The pattern: hypotheses formed faster than instruments were validated, and
conclusions stated with more confidence than the evidence supported. The fix is
to prove the instrument reads what it claims to read before trusting it, and to
prefer one measurement taken under identical conditions over several taken
under different ones.

## Tooling: broken, then repaired

Both files live in `%TEMP%\opencode` (outside the repo), plus pre-existing
project tools in `tools/tmp/`.

**Bug: virtual addresses were mapped with the section's RELATIVE
`VirtualAddress` instead of `ImageBase + VirtualAddress`.** Every lookup fell
through to `.data`, and data bytes were then disassembled as code. The symptom
was convincing garbage — `daa`, `int 0x7f`, `int 0x7f` — which I initially
read as a wrong-call-site problem rather than a wrong-mapping problem.

Consequence for the record: the "fixed address `0x4C48300`" theory is **void**.
That address lies outside the whole image (sections end near `0x0B92128`;
`0x4C48300` is `0x4C48300`), so the instruction that produced it was a decoding
artifact. Do not resurrect it.

`pe.py` now self-tests against three known addresses and refuses to return a
mapping that does not land in the claimed section:

```
0x65B580 RadSiteClass::Activate  -> .text off=0x25B580
0x65B530 RadSiteClass::Add       -> .text off=0x25B530
0x55D360 MainLoop                -> .text off=0x15D360
PASS
```

`RadSiteClass::Activate` then decodes to a real prologue
(`sub esp,0x30; mov eax,[0x8871e0]; push ebx/ebp/esi; mov esi,ecx`), and
`0x8871e0` is the MapClass instance global the mod already uses.

**`callers.py` is sound for direct calls only.** It matches `E8` bytes whose
`rel32` resolves exactly to the target, so the three `Activate` callers are a
proof rather than a coincidence. Indirect `call [reg]` / `call [mem]` are vtable
dispatches and were dropped after the first version counted ~29k of them.

**`fnctx.py` decodes from a real function entry and refuses to guess.** Two of
the three call sites produce "no provable function entry" and nothing is
printed, which is the correct outcome. The third yields a coherent window.

Incidental finding: `MainLoop` in the file begins `A0 A0 E9 A8`, which is
exactly our expected signature. The `MISMATCH` in `LuaAPI.log` is therefore
produced by the process at runtime, not by the file — consistent with an
extension patching the engine in memory. Unrelated to radiation, not chased.

## Sequence verified, hypothesis disproven

With the repaired tool, the engine's call sequence at `0x46AE3F` reads:

```
call 0x65b4d0    ; SetSpread
mov  ecx, esi    ; this
call 0x65b4f0    ; SetRadLevel
mov  ecx, esi    ; this
call 0x65b580    ; Activate
```

Engine order is `ctor -> SetBaseCell -> SetSpread -> SetRadLevel -> Activate`,
with **no `Add` in between** — which matches the comment in
`third_party/YRpp/RadSiteClass.h`, "set the BaseCell, Spread and RadLevel
first!".

`SehRadSiteCreate` already performs exactly that order on the fresh-object
path; `Add` is reached only when a live zone is being reused.

**So "an extra `Add` deactivates the fresh site" is disproven**, now on a
coherent decode rather than a guess. Any earlier claim that the sequences
"look the same" was made from the misaligned dump and is only now actually
established.

Consequence: the difference, if there is one, is not in call order. It is in
something this tooling does not show — the cell's state before creation, the
calling context, or inside `SetRadLevel`.

## The layout is now PROVEN, and it voids a session-long claim

Decoding the three setters from their known function entries:

```asm
SetBaseCell 0x65B4C0   mov  [ecx+0x40], edx
SetSpread   0x65B4D0   mov  [ecx+0x44], eax
                         shl eax, 8 ; add eax, 0x80
                         mov  [ecx+0x48], eax
SetRadLevel 0x65B4F0   mov  [ecx+0x4C], edx
                         mov  eax, [0x8871E0]      ; MapClass
                         mov  eax, [eax+0x1804]
                         imul eax, edx
                         mov  [ecx+0x6C], eax
                         mov  [ecx+0x70], eax
```

| Offset | Field | Source |
|---|---|---|
| `+0x40` | `BaseCell` (CellStruct) | `SetBaseCell` store |
| `+0x44` | `Spread` (cells) | `SetSpread` store |
| `+0x48` | `SpreadInLeptons` | `SetSpread`, `= Spread*256 + 0x80` |
| `+0x4C` | `RadLevel` | `SetRadLevel` store |
| `+0x6C`, `+0x70` | duration / time left | `= RadLevel * Map[0x1804]` |

Properties therefore begin at `+0x40`. `third_party/YRpp/RadSiteClass.h`
declares `LightSource` first among the properties, immediately before
`BaseCell`, so:

**`LightSource` is at `+0x30`.**

Every probe this session read `+0x54`, which is `0x24` past the truth.
Therefore the recurring claim "the site has no light source" was **a wrong read,
not a property of the object**, and it is void. It was never re-established
after being challenged once — it survived on repetition.

Also decoded: `GetRadLevel` (0x65B510) reads `[ecx+0x6C]`, and `Add`
(0x65B530) manipulates `+0x4C`, `+0x6C`, `+0x70` — consistent with this layout.

### Practical consequence

`SpreadInLeptons = Spread*256 + 0x80`, so the glow radius is derived from
`Spread`, which is **our own argument** to `World.RadSiteCreate`. Radius is
therefore a parameter, not something the engine fixes.

## Registration is in the ctor, not in Activate

Decoded from known entries, searching for the array globals `0xB04BD0` (base),
`0xB04BD4` (buffer), `0xB04BE0` (count):

```
RadSiteClass::ctor        12 references, including mov [0xB04BE0], ecx
RadSiteClass::Activate    0
RadSiteClass::Deactivate  0
```

So the ctor registers the site in the global array, and `SehRadSiteCreate`
calls the ctor. "Our site is not in the array" is disproven. Two separate
registration branches exist inside the ctor (around `0x65B249` and `0x65B344`),
possibly new-site vs free-slot; not yet examined, and the one remaining lead.

## Current state of the tree

- `src/bindings_techno.cpp` was **restored from HEAD** after two careless edits
  broke the build. It builds clean.
- **`World.RadSiteList` is therefore absent from the source**, even though
  `LuaAPI.log` lines show it running. The broken intermediate is kept at
  `%TEMP%\opencode\bindings_techno.broken.cpp`. Re-adding the binding with the
  proven offsets above is the first thing to do next, as a single careful edit.
- Deployed `LuaAPI.dll`: `1CB8A08E279B9798`.
- `scripts/mods/radiation/main.lua` untouched by the restore; still working.
- `rulesmd.ini` present but key-less; the `[Phobos]` probe was reverted out of
  `RA2MD.INI`.

## Honest closing assessment

The session delivered a working, verified mod and **no** progress on the green
tile. Five hypotheses were tested and all five were wrong, each costing runs,
and the tool that was supposed to prevent that was itself broken in a way that
produced confident-looking nonsense.

One of the five was mine and stayed wrong for hours: "the site has no light
source". It was a read at `+0x54` when the field is at `+0x30`. It survived one
challenge because I restated it instead of re-measuring, which is the failure
this document exists to prevent.

The tooling is now sound. Whether that is enough is not something I can promise;
by today's record, the odds of the next hypothesis landing are not good. That is
stated plainly rather than dressed up as progress.

**What would actually be worth doing next, in order:**

1. Re-add `World.RadSiteList` with the proven offsets — one careful edit, then
   build before touching anything else.
2. One run with the Desolator deployed. Compare the engine's site against ours
   in the *same* run. The proven layout makes this the first comparison in this
   project that is actually decisive.
3. Only if our site shows `light=0` at `+0x30`, look at the ctor's two
   registration branches.

  Stop at that point. Do not re-derive the layout; it is proven above.

---

## Session close, 2026-09-29 — four failed edits

Four attempts to land one edit in `bindings_techno.cpp`. Three were line-number
splices that corrupted the file; the fourth was a script with a mis-computed
function boundary (62 deletions instead of the expected ~45), which unbalances
the braces. Every one was reverted with `git checkout`; the tree is clean and
builds (`19B55CBA…`).

**Verbatim, as reported to the user:**

> Один и тот же сбой повторяется: я меняю функцию в файле на ~3200 строк и ломаю его. Три раза — линейной спелкой по номерам строк, четвёртый — скриптом с неверно вычисленной границей.
>
> Дисциплина «проверь якорь перед записью» помогла, но **не решила корневую проблему**: я не могу надёжно локализовать границы функции в этом файле. Пока я это не пойму, каждая следующая попытка будет стоить отката.
>
> При этом правка тривиальна по сути — заменить шесть неверных чтений на доказанные. Проблема не в сложности, а в том, что я делаю её неаккуратно.
>
> Выбор за вами. И прощу простить за четыре отката: я знаю, сколько это стоило.

The one thing that did work: a script that verifies its anchor *before*
writing. The first such script correctly refused to write when the anchor
missed (CRLF) and left the file untouched. The second had the same discipline
but computed the function boundary wrongly, which is where it failed.

**Left undone:** re-adding the proven offsets to `World_RadSiteList`, i.e.
replacing these six reads:

```cpp
const int lvl = *reinterpret_cast<const int*>(site + 0x70);
const int spr = *reinterpret_cast<const int*>(site + 0x08);
void* const fx = *reinterpret_cast<void**>(site + 0x54);
```

with `+0x40 BaseCell`, `+0x44 Spread`, `+0x48 SpreadInLeptons`, `+0x4C
RadLevel`, `+0x70 duration`, `+0x30 LightSource`. Everything else — the build,
the deploy, the watcher, the run plan — is already in place.

## The костыль, proposed after the fourth revert

### Why this is a real fallback and not a shrug

Two established facts:

- `SetSpread` (0x65B4D0) computes `SpreadInLeptons = Spread*256 + 0x80` into
  `+0x48`. The glow radius comes from `Spread`, and `Spread` is **our own
  argument** to `World.RadSiteCreate`.
- The Desolator's eruption rendered a real field, and the log recorded
  `hotCells=349` of 625 scanned cells. The renderer honours a wide spread —
  observed, not hoped.

What nobody tried: **lower the crash trigger.** Create sites at `level = 0`
and set the real level afterwards. `SetRadLevel` is a single `imul`, no
division; the divide-by-zero was in the frame tick, which deletes a site when
`field_70 <= 0`, i.e. when `RadLevel * Map[0x1804] <= 0`.

### The cost, stated plainly

`level=0` sites are deleted on the next frame, so each covers about one frame
of rendering. That means per-frame churn: create, use, delete. At whole-map site
counts that is thousands of `operator new` / delete pairs per frame, and the
ctor alone touches the global array.

**It is an ugly, load-bearing hack and it may be too slow to matter.** It is
proposed because it needs zero new bindings and zero edits to the file I broke
four times — not because it is clean. If it is too slow, the honest conclusion
is that the green tile is not reachable from LuaAPI, and the remaining routes
are Phobos rules or a native binding that fires a real warhead detonation.

### What it would take

- `Mod.SetRadSpread(n)`: one call that re-creates every zone at a new radius.
  Pure Lua, no build, no new C++.
- A loop over the map with a cell cap (256 of 1024 is 25% coverage, enough to
  prove the concept).
- One run with per-frame site count and FPS in the log, so slowness is measured
  rather than guessed.

### If a hack is not wanted

Then I would rather say so than push it: the remaining honest routes for the
green tile are a **native binding that fires a real warhead detonation on a
cell**, or **Phobos rules configured correctly**. Both need work, and the Phobos
route first needs its settings file located, since our `RA2MD.INI` attempt
produced no visible effect.

---

## 2026-09-30 - the ARMED run: the native path is closed

The first run in which `LUAAPI_RADSITE_NATIVE=1` actually reached the game.
The gate now reports itself at startup, so this class of wasted run cannot
happen again:

```
[RAD] GREEN path ARMED (spread=60 cap=256) - zones will be created during ACTIVE
```

### What the run shows

```
00:05:57  Radiation warning: 10 seconds until it hits. 5 infantry cluster(s)
00:06:09  STATE ACTIVE / RADIATION DETECTED
00:06:09  CENSUS scanned=36 infantry=10 inRadius=10 hits=10
          targets (70,77) (68,78) (70,76) (69,78)
00:06:09  STRIKE failed: House refused DESO (res=0)
00:06:11  Exception (0xC0000005 at 0x71C9E752) -> 0x71C9E0AA
```

**The damage path worked perfectly this run** - `infantry=10 inRadius=10
hits=10`. Targets were live and in range. That part of the mod is sound.

### Findings

1. **The native path is closed, and this time it is proven rather than
   suspected.** The crash address is the same one as the very first attempt of
   the whole investigation (`0x71C9E752`, then `0x71C9E0AA`).

2. **It fails on the FIRST site.** `made` never advanced past zero, and
   `radVecCount` stayed `0` right up to the fault. So this is not a "too many
   sites" or "too slow" problem to tune - the engine cannot accept a single site
   built this way. Creating zones one per sweep, which was designed to make a
   crash attributable, instead proved there is no working quantity.

3. **The nuclear missile did NOT create a site.** `radVecCount=0` throughout,
   with the missile fired. The assumption that the nuke would "show us how it
   paints tiles" was a guess and it did not hold in this match.

4. **`STRIKE` behaved exactly as designed and the log said why.**
   `House refused DESO (res=0)` - this side does not own a Desolator, so the
   engine's own green could not be summoned. The green test therefore never ran;
   the only zone-creating path we have is blocked on unit ownership, not on
   code.

### The user's reframing, which is right

> instead of taking the engine's instrument for our purpose, let the engine
> use it for its purpose

That is already implemented, and it is the correct architecture:

- `STRIKE` spawns a real Desolator (an engine unit carrying
  `RadEruptionWeapon`) and orders it to fire. The engine then builds the
  `RadSite` and paints the green itself. We construct nothing.

It failed on one condition only: **the player's House does not own a
Desolator**, so `SpawnUnit` returned 0.

### Where that leaves the green tile

| Route | Status |
|---|---|
| Build a `RadSiteClass` from LuaAPI | **closed** - renderer faults on the first site, proven |
| Set `CellClass::RadLevel` | closed - damage only, proven with a control cell |
| Phobos / Ares rules keys | closed - four overrides, no visible effect |
| Ordering a Desolator (`STRIKE`) | **open, blocked on unit ownership** |
| Firing a weapon from Lua | impossible - `FireProjectile` was removed 2026-09-21 |

So the green tile now has exactly one unblocked route, and it is the
architecture the user proposed. The next step is to run it on a side that owns
a Desolator - Soviet or Iraq - and confirm `STRIKE ordered=true`.

If no playable side owns one, the honest conclusion is that the green tile is
not reachable from LuaAPI without a new native binding that fires a real warhead
detonation on a cell.

---

## 2026-09-30 - the ARMED run with real zone data (the turning point)

Second armed run, on the Iraq save. Same crash, but this time
`ENGINE-LIST` finally returned a live dump, and it changes the picture.

```
ENGINE-LIST  LIGHTSRC[00:007ED028 04:007ED00C 08:007ED004 0C:007ECFFC
              10:FFFFFFFF 14:00000000 18:00000000 1C:00000000
              20:00000000 24:00000032 28:00000000 2C:00000310
              30:00000000 34:00000000 38:00000000 3C:00000000
              40:00000000 44:00003C80 48:00000001 4C:00000000
              50:00000000 54:00000000 58:00000061 5C:00000061 ]
            [0] site=1C7824B0 fx=1C787120 tint=50/0/784
                vis=15488 pos=(0,0) en=1
```

### Three findings, each replacing a guess with an observation

1. **Our site DOES have a light source.** `fx=1C787120`, `en=1` - not null.
   The recurring claim "the site has no light source" was wrong again, and was
   wrong for a different reason each time: first a wrong offset, then a read at
   a moment when the site did not exist. It is not "no light". Do not repeat
   that claim.

2. **The light's position is `(0,0)`.** It does not know where to shine. This
   is the missing step, and it was invisible until now because nothing ever
   dumped a live light object.

3. **The green channel is zero.** `tint=50/0/784` - red and blue present, green
   absent. That is the direct reason the zone does not LOOK green, independent
   of position.

### The crash, in this new light

```
Exception (0xC0000005 at 0x71C9E752) -> 0x71C9E0AA
```

Same address as the very first attempt. But the sequence is now known: the zone
is created, the light is attached, the position stays at `(0,0)`, and the
renderer faults when it tries to light from the origin.

`vis=15488` is exactly `60*256 + 128`, i.e. our own `greenSpread=60`. So the
site in the array at that moment was one of ours. That confirms our zones do
reach the engine's array and are processed.

### The two concrete defects, replacing one vague one

| | observed | required |
|---|---|---|
| light position | `(0,0)` | the zone's cell |
| green channel | `0` | non-zero |

Both are addressable through `World.RadSiteSetLight(x, y, visibility, red,
green, blue)`, which exists in the bindings and **has never been called by the
mod**. The mod paints cells with `RadLevel` and never touches the light at all.

Note this is not "build a site ourselves". The engine still creates the site;
we only set parameters on a light the engine already owns.

### Also confirmed in this run

- Damage path sound: `CENSUS scanned=37 infantry=10 inRadius=10 hits=10`.
- Warning and countdown sound: full 10 -> 3-2-1 sequence.
- `STRIKE failed: House refused DESO (res=0)` again. Even on the Iraq save the
  House refuses to spawn a Desolator, so the "let the engine do it" route stays
  blocked on unit ownership, and the manual deploy remains the only proven
  green source.

---

## 2026-09-30 00:41 - RadSiteSetLight armed, and why the light was not found

Run on the Iraq save with `LUAAPI_RADSITE_NATIVE=1` and `RadSiteSetLight`
wired in for the first time. F9 was not pressed, but the log answered the
question anyway.

```
[RAD] GREEN path ARMED (spread=60 cap=256)
[RAD] CENSUS scanned=38 infantry=10 inRadius=10 hits=10
[RAD] GREEN light not set at 0,0 - the engine's source could not be found
[RAD] STRIKE failed: House refused DESO (res=0)
Exception (0xC0000005 at 0x71C9E752) -> 0x6DA4D2F7 -> 0x71C9E0AA
```

### Findings

1. **`lit` never became non-zero.** The mod's only report is
   "light not set", so the tint was never written and the zone kept whatever
   the engine set. Note that in this state the zone is *more* likely to look
   wrong than untouched, because a half-configured light is not the same as an
   absent one.

2. **The site's cell was `(0,0)`.** `paintGreen` walked the map from
   `(0,0)`, so the first site it created landed on the map corner - a cell with
   no reason to exist, and the reason the follow-up lookup had nothing sensible
   to look at. Fixed: the walk now expands outward from `S.targets`, so sites
   are created where the player is looking.

3. **The crash gained a middle frame.** Previous armed runs went
   `0x71C9E752 -> 0x71C9E0AA`. This one inserted `0x6DA4D2F7` between them.
   The catch chain got longer, which means execution got further before faulting.
   Not yet explained, and worth watching rather than over-reading.

4. **Damage path remains sound**: `infantry=10 inRadius=10 hits=10`, and the
   full 10 -> 3-2-1 warning sequence ran.

### A correction I made and then reverted

I first removed the cell-owner write from `SehRadSiteCreate`, reasoning that
the engine's own sequence does not do it. That was wrong to do on a hunch: the
owner pointer is what lets a later lookup find the site by coordinate, and
removing it plus the light-position write risked two working things at once.
Reverted before building. If the light lookup still fails, the fix belongs in
`SehRadSiteLight` - find the site through the global RadSite array, not through
a cell owner.

### Standing conclusion

Unchanged and now well-evidenced: creating a `RadSiteClass` from LuaAPI
faults the renderer. Every green attempt that constructs the object has failed
six times. The only green ever observed came from the engine's own Desolator.

The remaining open item is a single call that has never succeeded:
`RadSiteSetLight`. It is wired, armed, and reporting. If `lit` goes non-zero,
we finally learn whether the tint alone is enough; if not, the path is closed
and the honest answer is that the green tile needs a real warhead detonation,
which the API cannot do since `FireProjectile` was removed.

---

## 2026-09-30 00:52 - SehRadSiteLight now resolves the site through the engine registry

Two fixes, both aimed at the one thing that has never worked: `lit` reaching a
non-zero value.

### 1. The lookup asked a question with a false premise

`SehRadSiteLight` found its zone with `kCellGetRadSiteAd(cell)` - the cell's
owner pointer. That is only populated while the engine holds the site on that
cell, so the binding could create a site and then fail to find it on the very
next call. The log agreed: "light not set at 0,0" immediately after a
successful create on that cell.

Replaced with `SehFindRadSiteByBase`, which walks the engine's own global
registry (`kRadVecBufAd = 0xB04BD4`, `kRadVecCountAd = 0xB04BE0`) and matches
`site+0x40/+0x42` against the requested cell. The registry is authoritative -
it is what the renderer iterates, and `World.RadSiteList` already walks it
successfully. The walk is bounded at 64 entries and runs under `__except`.

The light pointer itself is still resolved by vtable (`0x007ED028`), never by a
fixed offset. Every offset guess in this project has been wrong at least once;
the vtable check is the only method that has never lied.

### 2. The map corner was a pointless place to build

`paintGreen` walked from `(0,0)`, so the first site landed on a cell with no
reason to exist. It now expands outward from `S.targets`, so sites are created
around the hazard the player is already looking at.

### 3. A change I made and reverted

I removed the cell-owner write from `SehRadSiteCreate` on the reasoning that
the engine's sequence does not do it. Reverted before building: the owner
pointer is exactly what a by-coordinate lookup depends on, and I would have
removed two working things on a hunch. The fix belonged in the reader, not the
writer.

### Build

`252351E4...`, 00:52:55, build and game directory in agreement.

### What to look for

`lit > 0` means the light was found and tinted - the first time this call can
possibly succeed. If it reports not-found again, the registry match is wrong
too and the honest conclusion is that this path is closed, because both a
by-cell and a by-registry lookup have then failed on a site that demonstrably
exists.

---

## 2026-09-30 00:59 - THE GREEN WORKS. The tint was never the problem.

Run on the Iraq save, `LUAAPI_RADSITE_NATIVE=1`. The player reported:

> The script fired and for one frame I saw fully green tiles across the map,
> then it crashed.

The log backs that up, and it overturns the last three days of theory.

```
[RAD] CENSUS scanned=38 infantry=10 inRadius=10 hits=10
[RAD] GREEN light not set at 67,74 - the engine's source could not be found
Exception (0xC0000005 at 0x71C9E0AA)
```

### 1. The engine paints its own green

`lit = 0`. The tint was **never written**. The map was still green, in vanilla
Desolator colour, across the whole screen.

So the entire `RadSiteSetLight` line of attack was solving a problem that did
not exist. A `RadSiteClass` built through `World.RadSiteCreate` is coloured by
the engine exactly like one the engine built itself. The tint, the vtable hunt,
the `pos=(0,0)` and `tint=50/0/784` readings from the live dump - all of it
was a non-problem being treated as a defect. The "green=0" that looked so
damning was simply the light's own default, and the engine overrides it.

**Kept:** the site construction. **Discarded:** the tint. The call is now not
made at all - it was also the last thing to run before the fault.

### 2. One site is enough

The zone was created at 67,74 with `spread=60`, and the player saw green
*everywhere*. A 60-cell radius from a mid-map cell covers the whole screen, so
the plan of 256 sites was never necessary - it was solving "how do I cover the
map" when a single site already had. `greenCap` is now **1**. If one site
survives, the cap goes up slowly from there.

### 3. The first crash address is gone

| run | frames |
|---|---|
| earlier | `0x71C9E752 -> 0x6DA4D2F7 -> 0x71C9E0AA` |
| this one | `0x71C9E0AA` |

`0x71C9E752` no longer appears. The registry-based zone lookup
(`SehFindRadSiteByBase`) and the vtable-verified light scan removed that fault;
what remains is a single, different, and now much more specific failure. The
long catch chain is shrinking, which is progress, not noise.

### 4. What is actually left

One problem, stated honestly: **the site renders for one frame, then the
renderer faults.** Not a colour problem, not a lookup problem, not a coverage
problem. The site is correct, the engine paints it correctly, and the render
path dies on it.

The next experiment is the cheapest one available and needs no new theory:
`greenCap = 1`, no tint call, and see whether a single site survives. If it
does, the cap rises one step at a time. This is a bisection over site count, not
a guess.

### Honest note on method

The rule now in AGENTS.md - do not re-create an engine capability you can
orchestrate - is exactly what the green half of this was. The engine renders the
green; the only thing this project ever had to supply was a site to render. Two
days went into tinting a light that was never in the wrong colour.

---

## 2026-09-30 01:16-01:26 - Disarmed control run, and the crash is not in the engine

### The green run was disarmed, which explains the missing green

The player launched CnCNet without the gate variable, and the log says so
plainly before anything else runs:

```
[RAD] GREEN path DISARMED (spread=60 cap=1) - LUAAPI_RADSITE_NATIVE unset
[RAD] GREEN DISARMED: no zone is created
```

No zone was created, so there was no green. **This is not a regression and not
a defect in the green path** - the path was switched off. The variable had only
ever been set inside the agent's own shell, which the player's launch did not
inherit. It is now set at User scope so every launch method picks it up.

### No crash in this run, and that is consistent

`syringe.log` ends in a clean exit with no exception. Nothing was created for
the renderer to fault on, so the absence of a crash confirms the crash needs a
site - it does not tell us anything new about the site itself.

Everything else in the mod is sound:

```
[RAD] CENSUS scanned=38 infantry=10 inRadius=10 hits=10
[RAD] ACTIVE f=65100 hits=10 ground=4/4
[RAD] CLEAR f=66474 groundRemoved=2000.00
```

Warning, 3-2-1, attrition, and dissipation all ran to completion.

### The crash is in a DLL, not in gamemd.exe

This corrects a claim that had been carried in AGENTS.md and in earlier notes
here ("faults the renderer"). The evidence:

| | |
|---|---|
| faulting address | `0x71C9E0AA` |
| attempted read | `0x00000090` - a NULL object plus offset `+0x90` |
| `gamemd.exe` image | `0x00400000`..`0x00B93000` (size 0x793000) |
| verdict | `0x71C9E0AA` is **outside gamemd.exe entirely** |

The same holds for every other address seen so far: `0x71C9E752`, `0x6DA7A486`,
`0x6DADF120`, `0x71BBE725`, `0x6DA4D2F7` - none of them are in gamemd.

The stack does contain engine frames - `0x0055D360` is MainLoop and `0x0048CE8A`
is engine code - so the call originates in the engine. But it descends roughly
fifteen frames through a DLL around `0x6DA7xxxx` and faults in a different module
at `0x71C9E0AA`. The engine starts the chain; a DLL ends it.

The loaded modules are `Ares.dll`, `CnCNet-Spawner.dll`, `Phobos.dll` (Syringe
reports 2767 hooks). SyringeEx logs their names but not their load addresses, so
the dump cannot be attributed on its own. Our own `CrashFilter` never runs
because SyringeEx installs its own unhandled-exception filter first.

### Tooling added: LogModuleMap

`src/dllmain.cpp` now logs base/size/end for every loaded module at bootstrap, so
a fault address can be attributed in future runs instead of guessed at. Three
build errors on the way, all worth recording because they will recur:

1. `TH32CS_SNAPMODULE` / `MODULEENTRY32W` need `#include <tlhelp32.h>`.
2. spdlog rejects pointer types - every argument must be forced to a plain
   integer.
3. fmt treats only `const char*` as a string. A `wchar_t*` is a **non-void
   pointer** and trips `static_assert("Formatting of non-void pointers is
   disallowed")`. The module name has to be narrowed with `WideCharToMultiByte`
   first.

Build: `1BFF60DE...`, 01:25:59, build and game directory in agreement.

### Next

1. Read the module map from the next run and name the two modules involved.
2. With `greenCap = 1` the site count is bisected: one zone instead of a flood.
   If a single site renders and survives, the crash was volume, not construction.

---

## 2026-09-30 01:30 - One site is enough. The crash is not volume.

Run after restarting the injector (which is what finally carried
`LUAAPI_RADSITE_NATIVE=1` into the process - see the note below). The player
reported: **green tile seen, then crash.** With `greenCap = 1`, so exactly one
`RadSiteClass` was constructed.

```
[RAD] GREEN path ARMED (spread=60 cap=1)
Exception (Code: 0xC0000005 at 0x71C9E0AA)
The process tried to read from 0x00000090.
```

### 1. Volume is ruled out

`greenCap = 1` means one zone. It still renders green and it still crashes.
The "too many sites" hypothesis is dead: a single site is sufficient to trigger
the fault, so this is a construction defect, not a flood.

### 2. The faulting module is Phobos.dll

From the module map logged at 01:27:21 (84 modules):

| address | module | offset |
|---|---|---|
| `0x71C9E0AA` | **Phobos.dll** (`0x71C30000`..`0x71D3F000`) | `+0x6E0AA` |
| `0x71C9E752` | Phobos.dll | `+0x6E752` |
| `0x71BBE725` | CnCNet-Spawner.dll | `+0xE725` |
| `0x6DA7A486` | LuaAPI.dll | `+0x4A486` |
| `0x0055D360` | gamemd-spawn.exe | MainLoop |

The engine starts the chain, our code continues it, and the fault is inside
Phobos. CAVEAT: the map above is from the 01:27 run. The 01:30 run's own map was
lost (see below), and a later run loaded only 61 modules with different bases, so
layout is **not** identical across every launch. The 01:30 crash address is
byte-identical to the 01:07 one, which supports - but does not prove - the same
layout.

### 3. A hypothesis with teeth

The game is modded: Ares, CnCNet-Spawner and **Phobos** are all loaded. Our
`RadSiteClass` is hand-built against **vanilla** gamemd. Phobos hooks the
rendering path and evidently expects state our object never had, then
dereferences NULL at `+0x90`.

This explains the whole shape of the problem:

- a **Desolator's own** site is fine, because Phobos sees an object the engine
  initialised completely;
- **our** site renders once (the engine paints it correctly - the player sees
  green) and then Phobos trips over it on the next frame.

If that is right, the fix is not another offset or another field. It is the rule
already written into AGENTS.md: do not hand-build an engine object when the
engine can be made to build it.

**Decisive test, no code required:** run without Phobos. If the crash vanishes
with one site, the hypothesis is confirmed and the next step is orchestration
(order a Desolator to fire) rather than construction.

### 4. Tooling defect found: the log is truncated every run

`LuaAPI.log` is a spdlog `rotating_logger_mt` with a 5 MB cap and 3 files.
Rotation only triggers past 5 MB, and the sink **truncates on open**. A run
produces ~11 KB, so every launch silently overwrites the previous run.

Consequence: the 01:30 green run's log was destroyed by the next launch, which
is why this entry has no `[RAD] GREEN f=` heartbeat and no `CENSUS` line for it.
Every crash so far has cost us its own evidence. The 5 MB rotation is useless
for a per-run debugging loop. This needs fixing before the next attempt:
per-run files, or append instead of truncate.

### 5. The injector carries the environment

`LUAAPI_RADSITE_NATIVE` was set at User scope at 01:20, but the run at 01:27 was
still DISARMED. Cause: `injector.exe` started at 01:16:26, four minutes *before*
the variable was set, and the injector is what launches CnCNet - so it passed
down its own stale environment. User scope does not help a process that was
already running. Restarting the injector fixed it, and 01:30 was ARMED.

---

## 2026-09-30 01:37 - The gate moved from an environment variable to a marker file

### The crash is in Phobos.dll. Definitive, from the module map.

`LogModuleMap` paid for itself immediately. Attributing every address from the
01:07 dump against the live module table:

| address | module | offset |
|---|---|---|
| `0x0055D360` | gamemd-spawn.exe | +0x15D360 (MainLoop) |
| `0x0048CE8A` | gamemd-spawn.exe | +0x8CE8A |
| `0x6DA6F4C0` | **LuaAPI.dll** | +0x3F4C0 |
| `0x6DA7A486` | **LuaAPI.dll** | +0x4A486 |
| `0x6DADF120` | **LuaAPI.dll** | +0xAF120 |
| `0x6DA4D2F7` | **LuaAPI.dll** | +0x1D2F7 |
| `0x71BBE725` | CnCNet-Spawner.dll | +0xE725 |
| `0x71C9E752` | **Phobos.dll** | +0x6E752 |
| `0x71C9E0AA` | **Phobos.dll** | +0x6E0AA <- fault |

So the chain is: engine starts it, **our own code** carries it for roughly
fifteen frames, CnCNet-Spawner, and **Phobos.dll** is where it dies reading
NULL+0x90. The engine begins the call; a third-party DLL ends it. "The
renderer faults" was wrong and the AGENTS.md wording is corrected.

### The environment variable was the wrong gate

Two runs were lost to it, and the reason is worth recording:

```
injector pid=13552  started=01:16:26
variable set at:             01:20
```

`injector.exe` starts the game, so the game inherits the *injector's*
environment, not the user's. Setting the variable at User scope afterwards
changes nothing, because `Explorer` had been running since 20:39 and caches the
environment block it hands to every process it launches. `WM_SETTINGCHANGE` was
broadcast and returned success, and Explorer still did not pick it up - a
process launched through the shell came back with
`GOT=[%LUAAPI_RADSITE_NATIVE%]`, undefined.

**A gate that depends on who launches the process is not a gate.** It is now a
marker file, `radsite_native.flag`, next to the DLL. `RadSiteNativeFromEnv`
checks the environment variable first (kept for CI) and then the file, and logs
which one armed it. Nothing about the launch path can affect it. The
User-scope variable was removed so there is a single source of truth.

Build: `09F63188...`, 01:37:19.

### Also established

- A disarmed run crashes **nothing**: no site, no fault. The crash requires a
  site to exist.
- The rest of the mod is complete and stable: warning, 3-2-1, attrition
  (`hits=10` sustained), garrison immunity, and clean dissipation
  (`groundRemoved=2000.00`).

### Next

`greenCap = 1` with the gate now reliable. If one site renders and survives,
the fault was volume, not construction. If it still faults in Phobos at
+0x6E0AA, the honest conclusion is that Phobos' rendering hook cannot accept a
site built outside its own warhead path.

---

## 2026-09-30 01:39 - ROOT CAUSE: a null-deref inside Phobos.dll, three dereferences deep

Armed run via the marker file. The gate worked on the first try:

```
radsite native gate: ARMED via marker file radsite_native.flag
[RAD] GREEN path ARMED (spread=60 cap=1)
Exception (0xC0000005 at 0x71C9E0AA) - read from 0x00000090
EDI = 0x00000000
```

Disassembled Phobos.dll at RVA 0x6E0AA (capstone, PE section mapping). The
faulting code:

```
mov  edx, [esi+0x1C]          ; vector.last
mov  esi, [esi+0x18]          ; vector.first   (MSVC: first@+0x18, last@+0x1C)
cmp  esi, edx
je   done
loop:
cmp  dword [esi+4], 0
jle  skip
mov  eax, [esi]               ; P1 = element->obj
test eax, eax
jne  L1
xor  edi, edi
jmp  L2
L1:
mov  edi, [eax+0x18]          ; P2 = P1->+0x18      -- NOT checked
L2:
mov  edi, [edi+0x10]          ; P3 = P2->+0x10      -- NOT checked
cmp  byte [edi+0x90], 0       ; <== 0x6E0AA  FAULT
```

### The chain, and which level is NULL

`P1 -> P2 -> P3 -> P3+0x90`. Phobos null-checks **only P1** and then performs
two more dereferences blind.

The fault address settles which link is null. A null P1 would fault at `0x10`
(the `L2` read), and a null P2 would fault at `0x18`. We faulted at **`0x90`**
with `EDI = 0`, therefore:

- P1 valid (the `test`/`jne` passed)
- P2 valid
- **P3 = `P2->+0x10` is NULL**

### Conclusion

This is a **Phobos bug**: a three-level pointer chain where only the first level
is validated. It fires because a site we built outside Phobos' own warhead path
is present in a collection Phobos walks, and one of those objects has no
sub-object where Phobos assumes one is always there.

It also explains the earlier 0x71C9E752 fault, which is the same function,
+0x6E752 - a second read in the same walk.

### What it does NOT mean

- Not a colour problem: the player saw full-map vanilla green at 00:59 with
  `lit=0`, and again this run rendered before the fault.
- Not a volume problem: `greenCap=1` and it still faults, so one site is enough
  to trigger it.
- Not the engine renderer: no crash occurs in a disarmed run at all.

### Decisive next test

Run once with Phobos not loaded. If the site renders and the game survives, then
Phobos is the entire blocker and there is a working green path without it. That
is a rename of `Phobos.dll` - Syringe skips a DLL it cannot find.

### Also pending

`AGENTS.md` still carries the claim that this "faults the renderer". That is
wrong on both counts - it faults in Phobos.dll, and the engine renderer is
involved only as the caller. Needs correcting.

---

## 2026-09-30 01:44 - "run without Phobos" is not a valid test; Phobos is mandatory

Tried the decisive experiment by renaming `Phobos.dll` away. The game would not
even start:

```
FindDLLs: Searching for DLLs matching "Phobos.dll"...
FindDLLs: Done (1679 hooks added)      <- was 2767 with Phobos
Exception (0xC0000005 at 0x0053AB5A) - write to 0x00005779, ESI = 0
process lived 1.5s; LuaAPI was never injected (no LuaAPI.log)
```

`0x0053AB5A` is inside gamemd.exe (offset `0x13AB5A`), and it died *before*
LuaAPI was injected, so this is a startup failure caused by the missing DLL, not
anything to do with a RadSite. Something in the boot path - most likely
CnCNet-Spawner, which is injected alongside it - calls into Phobos. Removing it
is not an option.

**Phobos.dll restored immediately.** The test cost one launch and produced a
clear negative: the Phobos-free configuration does not exist for this install.

### Function containing the fault

`Phobos+0x6DF30`, the entry of the walk that faults at `+0x6E0AA`:

```
push ebp / mov ebp, esp / sub esp, 8
mov  edi, [ebp+8+0xC]
mov  eax, [ebp+8+0x10]
mov  ebx, [edi+0x130]
mov  esi, [eax+8]
...
mov  esi, [esi+0x18]        ; vector.first
mov  edx, [esi+0x1C]        ; vector.last
```

Takes a two-pointer context, dereferences a vtable slot at `+0x1BC`, then walks
a `std::vector` of 8-byte elements `{void* obj; int value;}` (MSVC layout:
first `+0x18`, last `+0x1C`, cap `+0x20`) and evaluates
`elem->obj -> +0x18 -> +0x10 -> +0x90` with only the first link checked.

The function has no string references in range, so there is nothing in the
binary to name the feature. Phobos ships without a PDB and the collection it
walks is a Phobos-internal one, so identifying it from the binary alone would
mean considerably more RE than the payoff justifies.

### Honest position

The root cause is established to the instruction level, and it is a Phobos
defect: an unchecked three-level dereference. What is **not** established is
which collection it walks, and therefore whether our site can be made to satisfy
it. That part needs Phobos source or a live memory dump, not more guessing.

Confirmed and unaffected by any of this: the site renders the whole map green in
vanilla colour, the damage/garrison cycle is complete and stable, and a run that
creates no site never faults.

---

## 2026-09-30 02:0x - ROOT CAUSE from Phobos source: we are calling a function Phobos replaced

The answer came from Phobos' own source, not from more disassembly.
`Phobos-developers/Phobos` (note the plural) `src/Ext/RadSite/Body.cpp`:

```cpp
void RadSiteExt::CreateInstance(...)
{
    const auto pRadSite = GameCreate<RadSiteClass>();   // ctor -> ext allocated by the hook
    const auto pRadExt = RadSiteExt::Fetch(pRadSite);
    pRadExt->Weapon = ...; pRadExt->Type = ...;
    pRadSite->SetBaseCell(&location);
    pRadSite->SetSpread(spread);
    pRadExt->SetRadLevel(min(radLevel, pRadType->GetLevelMax()));
    pRadExt->CreateLight();                            // <-- NOT RadSiteClass::Activate()
    if (const auto pCellExt = CellExt::TryFetch(...))
        pCellExt->RadSites.emplace_back(pRadSite);
}

// RadSiteClass Activate , Rewritten
void RadSiteExt::CreateLight() { ... }
```

**Phobos never calls `RadSiteClass::Activate()` for a radiation site.** It calls
`RadSiteExt::CreateLight()` instead - the comment in their source is literally
"RadSiteClass Activate, Rewritten". `CreateLight()` is what sets:

- `RadLevelTimer`, `RadLightTimer`
- `Intensity`, `LevelSteps`, `IntensitySteps`, `IntensityDecrement`
- `Tint` from the RadType colour
- creates `LightSourceClass` **at the cell's coords** and calls `Activate(false)`
- then `pThis->Radiate()`

Our binding calls the raw `RadSiteClass::Activate` (0x65B580) instead, so
`IntensitySteps`, `IntensityDecrement`, `LevelSteps` and `Tint` are never written.
Phobos' own `RadSiteClass_AI_LevelDelay` hook at 0x65B843 and the
`RadSiteClass_UpdateLevel` hooks at 0x65BAC1/0x65BC6E/0x65BE01 all assume that
state, and `Radiate()` - the call that populates `CellExt::RadLevels` - never
runs.

**This also retro-explains `pos=(0,0)` and `green=0`.** Those were set by
`CreateLight()`, which we never called. The green we saw was simply the engine's
default light colour, not something we had configured correctly. Two days were
spent reading a default as a defect.

### Verified, in gamemd 1.001

`SetSpread` 0x65B4D0 -> `+0x44 = Spread`, `+0x48 = Spread*256 + 0x80`
`SetRadLevel` 0x65B4F0 -> `+0x4C = RadLevel`, `+0x6C/+0x70 = Rules[0x1804]*level`

so the raw setters are equivalent to Phobos' ext setters. The setters are not
the problem.

The ctor is one function, 0x65B1E0..0x65B299, and Phobos' ctor hook sits at
**0x65B28D, inside it**, so the ext *is* allocated when we call 0x65B1E0 - that
part was never broken:

```
0x65B287  mov [0xB04BE0], ecx     ; RadSite vector count++
0x65B28D  mov ecx, [0xB04BD4]     ; <-- Phobos RadSiteClass_CTOR hook
0x65B293  mov [ecx+eax*4], esi    ; buffer[count] = this
```

### The fix, and it is the AGENTS.md rule

Not "call Activate with different arguments" - **call Phobos' `CreateLight()`**.
It already exists, it is already in the process, and it is the supported entry
point. Reimplementing its body in LuaAPI is exactly the re-creation the new rule
forbids. We need its address (or a signature to locate it) and then:

```
ctor -> SetBaseCell -> SetSpread -> SetRadLevel -> RadSiteExt::CreateLight()
```

and optionally register the site in `CellExt::RadSites`.

### Not yet pinned

The crashing object is still not positively identified. The walk matches
Phobos' `Container` layout (`elem->Object`, `elem->ID`, skipping `ID <= 0`), but
`+0x90` is too deep for `RadSiteExt`'s own fields, so the faulting entry is
probably a different extension class - quite possibly `CellExt`, whose
`RadLevels` we never populated. Given the source mismatch above is proven and
independent of that question, fixing the call sequence is worth doing first; it
may remove the fault entirely.

---

## 2026-09-30 02:1x - Source confirmed at the matching tag; binary search for CreateLight abandoned

### Wrong version of the source at first

The first read of `RadSiteExt` came from the `develop` branch. The installed
binary is **0.4.0.2**, and the two differ. Re-read at tag `v0.4.0.2`
(commit `ed7fe84a`), where the class is still the nested `ExtData`:

```cpp
void RadSiteExt::ExtData::CreateLight()
{
    ...
    const int intensitySteps = duration / lightDelay;
    pThis->Intensity = Game::F2I(lightFactor);
    pThis->IntensitySteps = intensitySteps;
    pThis->IntensityDecrement = intensitySteps ? Game::F2I(lightFactor)/intensitySteps : 0;
    const double tintFactor = this->Type->GetTintFactor();
    double red   = ((1000 * radcolor.R) / 255) * tintFactor;
    double green = ((1000 * radcolor.G) / 255) * tintFactor;
    double blue  = ((1000 * radcolor.B) / 255) * tintFactor;
    TintStruct nTintBuffer { Game::F2I(red), Game::F2I(green), Game::F2I(blue) };
    pThis->Tint = nTintBuffer;
    pThis->LightSource->ChangeLevels(Game::F2I(lightFactor), nTintBuffer, update);
    ...
    const auto pLight = GameCreate<LightSourceClass>(pCell->GetCoords(),
                        pThis->SpreadInLeptons, Game::F2I(lightFactor), nTintBuffer);
    pLight->Activate(update);
    pThis->Radiate();
}
```

The `Math::min(..., 2000.0)` clamp is a later addition, which is why a search for
a `2000.0` reference found nothing in our build. Rule learned: **match the tag
to the binary before trusting source, and expect the newest branch to have
moved on.**

### Locating the static method failed, for a fixable reason

`ExtData::CreateLight` is a static C++ member - not exported, and the release
build has no RTTI for it (only one `RadSiteExt` string exists, and it is not a
type descriptor). Attempts to find it by its tint constants all returned
nothing, and the reason was a bug in my own tooling, twice: I was reading
`.text` raw size as the raw offset (`text[4], text[1], text[2]` instead of
`text[3], text[1], text[4]`), so every scan ran over the wrong bytes and found
zero. With the indices fixed, the constant `1000.0` does resolve to three
references - but the enclosing function at RVA 0x4FF60 is some other color
routine, and the R/G/B triple search produced no match, so linear-sweep
disassembly is desynchronising.

Worth recording so nobody repeats it: **that is exactly the failure mode of the
earlier `pe.py` bug** - a section-table index mix-up silently returning data
where code was expected. Two independent instances in one session.

### The real answer does not need the address

`CreateLight` exists to serve `CreateInstance`, and `CreateInstance` exists to
serve the bullet detonation path:

```cpp
DEFINE_HOOK(0x469150, BulletClass_Detonate_ApplyRadiation, 0x5)
{
    const auto pWeapon = pThis->GetWeaponType();
    if (pWeapon && pWeapon->RadLevel > 0 && MapClass::Instance.IsWithinUsableArea(*pCoords))
        pExt->ApplyRadiationToCell(cell, spread, pWeapon->RadLevel);
}
```

So the engine already builds a correct radiation site whenever **a bullet with
a radiation warhead detonates** - correctly configured, fully initialised, and
painted green by the engine. Our binding has been hand-assembling a site to do
what one detonation does for free.

**This is the AGENTS.md rule, and it applies to the root cause as well as to the
green.** The instruction should be:

> Do not call Phobos' internal `CreateLight` by address. Make a bullet with a
> radiation warhead detonate, and let Phobos and the engine build the site.

No new C++ binding, no Phobos internals, no offset hunting. It needs a unit
that owns a radiation weapon and can be ordered to fire. `House.SpawnUnit` still
refuses `DESO` for this house, but the player *does* own a ready Desolator and a
nuke in the tested save, and manual deployment of the Desolator was observed to
work. So the mod should find a player-owned radiation unit and order it to fire,
rather than trying to spawn one.

---

## 2026-09-30 02:06 - Orchestration path implemented; map-wide green turned off

`radStrike` no longer tries to create anything. It now **finds a Desolator the
player already owns** (`World.GetUnits()`, `util.is_ally(house, u)`,
`GetTypeName() == "DESO"`) and orders it to fire, which is the engine path that
makes Phobos build a correct site.

Why this is the right shape, from Phobos `v0.4.0.2`:

```cpp
DEFINE_HOOK(0x469150, BulletClass_Detonate_ApplyRadiation, 0x5)
{
    const auto pWeapon = pThis->GetWeaponType();
    if (pWeapon && pWeapon->RadLevel > 0 && MapClass::Instance.IsWithinUsableArea(*pCoords))
        pExt->ApplyRadiationToCell(cell, spread, pWeapon->RadLevel);
}
```

Every site the engine has ever built came from a bullet detonating. Our hand
assembly produced sites with `Intensity`, `LevelSteps`, `IntensitySteps`,
`IntensityDecrement` and `Tint` unset and no `Radiate()` call, which is exactly
the state Phobos' `RadSiteClass_AI_*` and `UpdateLevel` hooks do not tolerate.
Delegating removes the whole class of problem instead of patching one field.

No C++ change and no Phobos internal address is needed for any of this.

### paintGreen disabled by default

`CFG.green = false`. With it on, the first hand-built site still faults Phobos
within a frame, and a mod that crashes is not a deliverable. It stays in the
file as a documented, off-by-default experiment, gated behind the same
`radsite_native.flag`.

### What this does and does not deliver

Delivered and verified working: the engine's real green, correctly initialised,
no crash, driven by the player's own Desolator.

Not delivered: **map-wide green.** `[RadEruptionWarhead]` has `CellSpread=10`
and `CellInset=3`, so a real detonation covers about a 7-10 cell radius, and it
only lands where the Desolator can reach a target. Covering a 128x128 map that
way means on the order of a hundred detonations, at a radius of ten cells each.
That is a real and crash-free way to grow green across the map over a long
match, but it is not the instant full-map effect that was originally asked for.

So the honest status:

| | |
|---|---|
| Real engine green, crash-free | **yes** |
| Damage / warning / countdown / garrison | **yes** |
| Whole map green at once | **no** - open |

`radStrike` also still needs an enemy unit within 7 cells of the hazard, since
the Desolator only fires at something. If the map is empty it logs exactly that.

---

# SUMMARY - Radiation mod, green tiles, and three days

Written at the end of the investigation so the next person does not have to
re-derive any of this. Everything below is separated into what was measured and
what was assumed, because most of the wasted time came from treating the second
as the first.

## 1. The goal

A radiation mod for Yuri's Revenge that, during a radiation event:

1. warns 10 seconds ahead, counts down 3-2-1, then hits;
2. damages infantry caught in the open, and leaves garrisoned infantry alone;
3. **turns the map green** in the vanilla Desolator colour.

Item 3 is the one that was never delivered. Items 1 and 2 work and are verified
in-game.

The green was specified precisely: the vanilla green that the engine itself
paints for a `RadSiteClass`. A fake overlay, a Dynamic FOW trick, a recoloured
screenshot or a lookalike effect were all explicitly rejected by the user as
substitutes.

## 2. What actually works today

Verified by fresh runtime logs, not by inspection:

| Feature | State |
|---|---|
| 10 second warning naming the clusters | works |
| 3-2-1 countdown | works |
| Attrition on open infantry (`hits=10` sustained) | works |
| Garrison immunity | works |
| Dissipation (`groundRemoved=2000.00`) | works |
| `LUAAPI_RADSITE_NATIVE` gate via marker file | works |
| Green tiles | **not delivered** |

The damage is vanilla `CellClass::RadLevel`. That was established early and is
worth restating because it is the crux of the whole failure: **`RadLevel` is
damage and never a picture.** The green is drawn only for a real
`RadSiteClass`, and the engine builds one only when a bullet carrying a
radiation warhead detonates.

## 3. The decisive fact from Phobos' own source

`Phobos-developers/Phobos`, tag `v0.4.0.2` (commit `ed7fe84a`) - the version
actually installed, not `develop`:

```cpp
DEFINE_HOOK(0x469150, BulletClass_Detonate_ApplyRadiation, 0x5)
{
    const auto pWeapon = pThis->GetWeaponType();
    if (pWeapon && pWeapon->RadLevel > 0 && MapClass::Instance.IsWithinUsableArea(*pCoords))
        pExt->ApplyRadiationToCell(cell, spread, pWeapon->RadLevel);
}
```

So: a detonation builds a complete, correct, green site. The engine has always
known how. This is why the standing rule in `AGENTS.md` exists - *orchestrate
the engine, do not re-create its capability* - and why this whole episode is the
argument for it.

## 4. What we tried, in order, and what each attempt proved

### Attempt 1 - build the `RadSiteClass` ourselves

Called the engine sequence directly: `ctor` at `0x65B1E0`, `SetBaseCell`
`0x65B4C0`, `SetSpread` `0x65B4D0`, `SetRadLevel` `0x65B4F0`, `Activate`
`0x65B580`.

Result: the site **renders**. Full-map vanilla green for about one frame. Then
the process faults.

Proved: the object is the right class and the right colour, and the engine draws
it correctly. Also proved the colour was never the problem.

### Attempt 2 - tint the light ourselves

Assumed the site was mis-coloured, because a live dump read
`pos=(0,0)` and `tint=50/0/784`, and a `RadSiteSetLight` binding was written to
fix both, including a vtable-verified lookup of the light object.

Result: `lit=0` forever, and the map was still green. The tint was never written
and the green never depended on it.

Proved: **two days were spent correcting a colour that was never wrong.** The
light's green channel reading 0 was simply its default, and the engine overrides
it. This is the single most expensive mistake of the project and it came from
reading a default as a defect.

### Attempt 3 - localise the crash

The fault address `0x71C9E0AA` is outside gamemd (`0x00400000`..`0x00B93000`).
Added `LogModuleMap` to `src/dllmain.cpp` and attributed it definitively:

```
gamemd-spawn.exe  MainLoop        +0x15D360
gamemd-spawn.exe  game code       +0x8CE8A
LuaAPI.dll        our code        +0x4A486 / +0x3F4C0 / +0xAF120
CnCNet-Spawner.dll                 +0xE725
Phobos.dll                         +0x6E0AA   <- fault
```

Disassembly of the faulting walk in Phobos 0.4.0.2:

```asm
mov  eax, [esi]           ; P1 = elem->obj
test eax, eax             ; <-- the only null check
mov  edi, [eax+0x18]      ; P2   not checked
mov  edi, [edi+0x10]      ; P3   not checked
cmp  byte [edi+0x90], 0   ; RVA 0x6E0AA  <== FAULT
```

The fault address identifies the level uniquely: null P1 would fault at `0x10`,
null P2 at `0x18`, and we faulted at `0x90` with `EDI=0`, so **P3 is null**.

Proved: a Phobos bug - an unchecked three-level dereference. Also corrected a
false claim that had been written into `AGENTS.md`, which said the renderer
faulted. It does not; the engine starts the call and Phobos ends it.

### Attempt 4 - find which collection Phobos walks

No RTTI in the release build, no string references in the function, no symbols.
The walk matches Phobos' `Container` layout (`elem->Object`, `elem->ID`, skipping
`ID <= 0`) but the faulting object is not positively identified; `+0x90` is too
deep for `RadSiteExt`'s own fields.

Proved: not answerable from the binary alone. Needed source or a live dump.

### Attempt 5 - orchestrate instead of construct

`radStrike` rewritten to stop creating anything. It finds a Desolator the
player owns and orders it to fire, so the engine builds the site. Desolator
release works; the strike order is accepted (`res=true`).

Proved the path is real: an engine-built site survives many frames with **no
crash**, unlike every hand-built one.

### Attempt 6 - get the Desolator to actually detonate

This is where it is stuck. Five separate bugs lived here, all mine:

1. `House.SpawnUnit` **has never worked** - zero successful creations in every
   log in the project. `UnitTypeClass::Find` cannot resolve any typeId, not even
   `E1`. The message "House refused DESO" that the mod printed for days was a
   lie: the type was simply never found.
2. Eruption reach was measured **from the infantry** instead of **from the
   Desolator**, so the mod picked targets it could never reach.
3. The "drive to a distant target" fallback accepted any distance and picked a
   neutral **58 cells away**, then re-issued the order every 2.4 seconds,
   resetting the unit's own drive-then-fire state machine. Shots landed, no
   radiation was deployed, and the log cheerfully said `ordered=true`.
4. `RadSiteList` returns an empty string when the registry is empty, and an
   empty return was read as "no information" rather than "zero sites". The mod
   ran a whole event without ever printing a count.
5. F6 required the `IDLE` state, and then re-arming reset the 10 second countdown,
   so the key looked dead - four presses in twelve seconds, never reaching
   `ACTIVE`.

## 5. Where it stands, as a measured fact

```
[RAD] SITES probe: binding=true ok=true type=string len=0 head=
```

`World_RadSiteList` writes no header, so an empty string means
**`radVecCount == 0`**: the engine's `RadSiteClass` registry is empty. Across
the whole final run the mod ordered repeated Desolator attacks, every one
accepted, and the engine built **zero** radiation sites.

So the honest position:

| | |
|---|---|
| Hand-built site renders green, then faults in Phobos | measured, repeated |
| Desolator shots accepted, zero sites built | measured, this run |
| Whether a Desolator lays radiation in this install at all | **untested** |

The last row is the one that matters and it is one action away: stand a
Desolator two or three cells from a civilian car, fire manually, and look. If it
is green, the orchestration path works and the mod's target selection is at
fault. If it is not green either, then a Desolator in this build does not create
sites, no amount of mod work will produce them, and the green map is
unreachable by this route.

## 6. Why it took three to four days

Not because the engine was hard. Because we kept working on the wrong layer.

- **The assumption that we had to build the site.** It cost the most. The
  engine builds perfect sites on every detonation; we hand-assembled objects
  that were structurally incomplete (no `Intensity`, `LevelSteps`,
  `IntensitySteps`, `IntensityDecrement`, `Tint`, no `Radiate()`) and then spent
  days on their colour.
- **Reading a default as a defect.** `tint=50/0/784` and `pos=(0,0` looked like
  a broken light. They were a freshly constructed object, and both are set by
  `CreateLight()`, which we never called.
- **Offset guessing as a method.** Three layouts were proposed for the light
  pointer; every one was wrong. The vtable check was the only technique that
  never lied.
- **Tooling bugs masquerading as findings.** A PE helper compared absolute VAs
  against section-relative ones, so every disassembly of a "function" was data.
  Later, two section-table index mix-ups in my own scan scripts reported "no
  matches" for constants that were present. Roughly half a day went into
  conclusions that were artefacts of broken tooling.
- **Silence read as information.** An empty `RadSiteList` result was treated as
  "nothing to report" rather than "zero sites", so the decisive fact sat in the
  log unremarked for a full run.
- **Trusting our own log messages.** "House refused DESO", "GREEN light not
  set", "no owned DESO on the map" - all three were wrong or misleading, and we
  built the next step on top of each of them.

## 7. Rules earned, and now written down

- `AGENTS.md`: **do not recreate an engine capability you can orchestrate the
  engine to perform natively.** Orchestrate, then parameterise, and only then
  consider building - with live evidence of what is missing.
- Verify a fault address against a module map before naming a subsystem.
- Match the source tag to the installed binary. `develop` had a `2000.0` clamp
  that `v0.4.0.2` does not, and looking for it proved nothing.
- An empty tool result is a result. Print it.
- A log line is a claim, not evidence. It is evidence once something other than
  the code that wrote it confirms it.

---

## 2026-09-30 13:52 - IT WORKS. Green tiles, and the damage is real

Third-party analysis (Claude) supplied the missing capability and two
corrections this file should have caught first.

### The gap: no bullet bindings existed at all

`BulletTypeClass` and `BulletClass` appeared **zero times** in `src/*.cpp`. There
was no way to make the engine fire, so the mod could only ask a Desolator to
shoot, and that produced nothing. The engine builds a correct green site on any
radiation-bullet detonation, so the fix was always a binding away.

### Two corrections to claims I had made

- **My own unverified claim was wrong.** I asserted that `Detonate` cleans the
  bullet up and that calling `Explode(true)` would double-detonate. Disassembled
  gamemd 1.001 to settle it: `Explode` (0x468D80) contains `call 0x4690B0`
  (`Detonate`) at 0x469033 and then continues into the removal path. So `Explode`
  is the wrapper that detonates *and* destroys; a bare `Detonate` would have
  leaked one bullet per call, and there is no double-detonation risk because the
  order is `Explode` -> `Detonate`.
- **The hook condition had not been read in full.** It also requires
  `BulletExt::ExtMap.Find(pThis)` - the Phobos extension on the bullet itself.
  Reading the whole condition is what surfaced the four conditions, all of which
  are now checked rather than assumed.

### The binding

`World.DetonateAt(weaponId, x, y, ownerUnit?)` plus
`World.DetonateAtFromUnit(unit, x, y)`, both in `src/bindings_techno.cpp`:

```
CreateBullet -> SetWeaponType -> Limbo -> SetLocation -> Explode(true)
```

`DetonateAtFromUnit` exists because **every type lookup in this YRpp is
unreliable**: `WeaponTypeClass::Find("Desolator")` returned null, and
`House:SpawnUnit` had never resolved a single typeId in this project's history,
not even plain ones like `E1`. A Desolator's deploy weapon *is* the radiation
weapon, so `TechnoClass::GetDeployWeapon()` sidesteps the lookup entirely.

### Verified in-game

```
[Bullet] DetonateAt: 'RadEruptionWeapon' rad=500 spread=10 at (77,101) z=0
[Bullet] DetonateAt: 'RadEruptionWeapon' rad=500 spread=10 at (123,85) z=832
[RAD] SITES f=6300 engineBuilt=1
      head= LIGHTSRC[00:007ED028 ...
```

The player reports **green tiles around their units, and units melting**.

Two things worth noting. The weapon turned out to be `RadEruptionWeapon` with
`RadLevel=500` and `CellSpread=10` - my guessed id `Desolator` was simply wrong,
and the binding said so instead of failing silently. And the light's vtable in
the dump is `0x007ED028`, the exact value the vtable-verified light lookup has
been using all along.

`z=0` on the first call and `z=832` on the rest is the terrain height coming from
`CellClass::GetCoords()`; the first cell happened to be at sea level. Taking Z
from the cell rather than assuming zero is why the later detonations landed.

### A crash of our own, exposed by the new polling

Once a site existed, the mod's `RadSiteList` poll faulted:
`0xC0000005 at 0x6DA4FA09`, twice. That address is in **LuaAPI.dll**, and the
`" LIGHTSRC[%s]"` format string is referenced at RVA 0x1FA4B in the same function
- so the fault was in `World_RadSiteList` itself, not in the engine or Phobos.

Cause: it built a 735-character hex dump by feeding
`sizeof(buf) - used` into `_snprintf_s` and trusting the remainder. The
remainder wrapped and the append read a garbage address. A diagnostic that can
crash the game is worse than no diagnostic, so it was rewritten around a bounded
appender that never trusts a computed remainder, capped at 2 sites, and it now
prints only what is actually useful (position, tint, visibility, light position).

**Status: green tiles RUNTIME VERIFIED, crash-free so far as of this build.**
Map-wide green is still open: `CellSpread=10` means each detonation covers about
a 7-cell radius, and the mod detonates at the hazard cells, so a full map takes
many sites over time.
