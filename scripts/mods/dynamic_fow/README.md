# dynamic_fow — shroud that comes back when you walk away

Vanilla RA2/YR only ever REMOVES shroud (`TechnoClass::See` reaches
`DisplayClass::MapCellVisibility` with `bIncrease` hard-coded to 0), so
explored ground stays visible all match. This mod re-shrouds explored cells
nobody watches, so the visible area matches live vision. It HIDES ground and
never reveals any. Runs AUTOMATICALLY (no hotkeys; remove from
`active_mods.txt` to opt out).

## How it works

- **Guard** — a cell is skipped while inside any player object's live sight
  radius (`unit:GetSight`, padded), inside recent sight history (adaptive
  window: watch-duration clamped `[HISTORY_MIN, HISTORY_SWEEPS]`), or while
  the engine reports sight (`shroudCounter`, kept as a canary — dead live).
- **Hysteresis, not delay** — per-cell streak in `[-1, +2]`; write at `+2`,
  release at `0`. Single blips change nothing. Fast-motion cone (net
  displacement over 4 sweeps) bypasses the history wait for steady marchers;
  millers/pacers keep the full delay.
- **Coverage** — per-object boxes plus a rolling trail of everything ever
  seen (shuffled slices, paced write budgets). Every visited cell joins the
  trail; black cells leave it (self-cleaning).
- **Honest ledger** — memory drops only for cells the engine provably
  re-opened; tracked-black cells stay managed. No silent dark matter.
- **Render** — engine bits only (`AltFlags 0x18` + `Center/Edge`, exactly the
  vanilla Reshroud combination); one tactical dirty-area flush per writing
  sweep (`World.FlushShroudRedraw`), plus a periodic mop-up. No renderer
  hooks in the visual path.

## Knobs (`main.lua` top)

| Knob | Default | Effect |
|---|---|---|
| `SWEEP_INTERVAL` | 3 | frames between sweeps (response base unit) |
| `HISTORY_SWEEPS` / `HISTORY_MIN` | 150 / 6 | sight-history window cap/floor (sweeps) |
| `BOX_WRITE_BUDGET` / `TRAIL_WRITE_BUDGET` | 80 / 120 | writes per box / trail pass (pacing) |
| `MAX_PER_SWEEP` | 4000 | global backstop |
| `PROGRESS_MIN` | 1 | net cells/4 sweeps arming the fast cone |
| `VISUAL_CRISP` | false | resolver-mark visuals (off: engine bits only) |

## Meters (LuaAPI.log)

- `SWEEP#` — `reshrouded / hyst / reblack / cleared / blackWatch / fast / orphan` per sweep;
- `SHROUD` — window volumes: `total`, `+cells`, `rate` (cells/s), `meanAge` (abandonment-to-blacken), `maxBurst`, `activeSw`;
- `blackWatch@` — coords black-in-data inside true sight (engine not reopening);
- `orphan@` — open cells under no management (coverage holes);
- `census` — object/position/sight coverage of the guard.

## Version notes (v0.9.x live findings)

- Frame `== -1` and `ShroudCounter` are invalid guards (stale/dead live).
- The engine draws dirty rects only: writes need `FlushShroudRedraw`.
- Patrol/patrol-route oscillation is prevented by watch-duration history,
  not by freezing writes (one-sided pauses drain the map open).
- Per-sweep batch order is row-major: budget slices need shuffling or they
  paint bands; uniform speckle means sliced processing, organic advancing
  fronts mean contiguous processing.

## На русском, коротко

Туман возвращается туда, откуда ушел. Мод только скрывает землю, включается
сам, настраивается константами в начале `main.lua`, состояние видно в
`LuaAPI.log` по строкам `SWEEP#`/`SHROUD`. Отключение — убрать строку
`dynamic_fow` из `scripts/active_mods.txt` и перезапустить игру.
