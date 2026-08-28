# Mod Manager (injector_gui.cpp) — Load Order, Conflicts, Injection

This document describes how the LuaAPI **Mod Manager** (the Win32 launcher,
`injector_gui.cpp`) decides which mods run, in what order, and what actually
happens when two mods conflict. It does **not** cover the runtime Lua/C++ API
for writing mods — see [`API.md`](../API.md) for that.

---

## 1. What "order" actually means

The card list in the Mod Manager can be reordered by dragging cards up and
down. That order is written to `scripts/active_mods.txt`, one mod ID per
line, top to bottom.

`scripts/init.lua` reads that file top to bottom and does two things with it,
in the same order:

1. **Load order.** Each mod is `require()`'d in file order. If two mods
   define something with the same global name at load time, the one loaded
   **later** (lower in the list) wins, because it runs second and overwrites
   whatever the first one set up.
2. **Update order.** Every game tick, `OnTick` calls `mod.Update(frame)` for
   each loaded mod, again in file order.

### This is not the same as "top = wins"

Vortex/Nexus-style managers usually treat "higher in the list" as "higher
priority, overrides what's below." **LuaAPI's actual behavior is closer to
the opposite for same-tick writes**, because of ordinary last-write-wins
semantics:

- If two mods both write to the same piece of shared state inside
  `Update()` during the same tick (e.g. both set `unit.someField`), the mod
  that runs **later** — i.e. **lower** in the list — is the one whose value
  survives that tick.
- If instead the conflict is about who **claims a resource first** (e.g.
  "only the first mod to see this event should act on it, others should
  skip"), then the mod **higher** in the list wins, because it runs first.

Which of these applies depends entirely on what the two conflicting mods
actually do. The Mod Manager has no way to know this — it only detects
*that* two mods declare a conflict (see below), not what kind of conflict it
is or which order is "correct." **Don't assume drag-to-top means
"this mod wins."** Test the actual in-game behavior with both orderings if
it matters.

---

## 2. Conflict detection is a warning, not a lock

A mod can declare conflicts in its `mod.json`:

```json
{
  "id": "mod_a",
  "conflicts": ["mod_b"]
}
```

If both `mod_a` and `mod_b` are enabled at the same time, the Mod Manager
shows an orange warning banner naming the pair. That's all it does:

- It does **not** prevent both mods from being enabled.
- It does **not** prevent `SaveMods()` from writing both IDs to
  `active_mods.txt`.
- It does **not** auto-disable one of them.

The banner exists to tell a human "you probably don't want both of these
on," nothing more. Resolving the conflict — by disabling one, or by
reordering if the mods are order-sensitive rather than mutually
exclusive — is up to the user.

`conflicts` is a flat list of mod IDs, matched case-insensitively in both
directions (A declaring a conflict with B is treated the same as B
declaring a conflict with A). There is no severity level, no free-text
reason field, and no distinction between "these will crash together" and
"these will just look weird together" — if that distinction matters to your
mod, say so in its `description`.

---

## 3. Persisted order vs. filesystem order

On startup, `ScanMods()` re-scans `scripts/mods/` from disk. Filesystem
enumeration order is not something the user controls and isn't guaranteed
to be alphabetical on every system.

To keep a user's drag-and-drop order stable across restarts:

1. `ScanMods()` reads the existing `active_mods.txt` first.
2. Every mod ID listed in that file is placed first in `g_mods`, in the
   file's order.
3. Any mod found on disk but *not* listed in the file (new mods, or mods
   the user has never enabled) is appended after, in filesystem order.

This means: reordering only "sticks" for mods that have been saved at least
once via the **Save & Apply** button. A brand-new mod you haven't touched
yet will show up wherever the filesystem happens to put it until you save.

---

## 4. Injection is asynchronous — what that means if it fails

Clicking **Inject** does not block the UI. The actual `LoadLibraryW` call
into the target process runs on a worker thread; the launcher stays
responsive and the button is disabled/dimmed (`g_injecting`) until the
result comes back via a posted message.

Injection has a **5 second timeout**. If the target process doesn't
finish loading the DLL within that window, the attempt is treated as a
**failure**, not a success-in-progress — you'll get an error dialog, not a
frozen window. If you hit this in practice (antivirus interference, a
process that's mid-load-screen and not pumping messages, etc.), the fix is
to retry after the target has finished whatever it was doing, not to wait
longer — 5 seconds already assumes something is wrong.

---

## 5. Guidance for mod authors

If your mod needs to be robust regardless of where the user drags it in the
list:

- Don't assume you run first or last. Check state before mutating it
  (`if unit.someField == nil then ... end`) rather than unconditionally
  overwriting.
- If you genuinely need to run before or after a specific other mod,
  document that in your mod's `description` field so the user knows to
  order them manually — the manager has no concept of declared
  dependencies or "load after X," only flat conflicts.
- If two mods truly cannot coexist regardless of order, declare it via
  `conflicts` in `mod.json` — even though the manager won't enforce it,
  the warning banner is the only signal the user gets.

---

*Last updated: August 2026, alongside the async-injection and
drag-to-reorder changes in `injector_gui.cpp`.*
