# Yuri's Echo launcher

WPF launcher (`net10.0-windows`, `UseWPF`) for Yuri's Revenge 1.001. It is built
as `YurisEcho` and published as `Yuri's Echo.exe` in the game folder; the game
root is resolved from `AppContext.BaseDirectory`, so the launcher expects to run
from the YR install (the repo root is a full YR install).

## Layout

```
tools/LuaApiLauncher/
  build-and-deploy.ps1        build + copy artifacts into the game folder
  check_l10n.ps1              localization audit (missing/extra strings)
  LuaApiLauncher/             the application project
```

## Build

```powershell
dotnet build tools\LuaApiLauncher\LuaApiLauncher -c Release -r win-x64 --self-contained false
```

`build-and-deploy.ps1` does the same and copies `Yuri's Echo.exe`,
`YurisEcho.dll`, `.pdb`, `.deps.json` and `.runtimeconfig.json` into the game
folder. Stop a running launcher first, it locks `YurisEcho.dll`.

The assembly name stays `YurisEcho` on purpose: WPF bakes it into the XAML
resource URI, and an apostrophe there breaks resource lookup at startup. Only
the executable file is renamed.

## What it does

- Campaign, skirmish, mods and options screens; the skirmish screen writes the
  CnCNet-compatible `spawn.ini` / `spawnmap.ini` and starts the match with
  `gamemd-spawn.exe -SPAWN`.
- Skirmish player slots: faction, color, team, start position and per-AI
  difficulty (`[HouseHandicaps]`).
- Manual start positions: numbered markers are drawn on the map preview using
  the isometric waypoint projection from `INI\MPMaps.ini`; left click assigns
  the position to a player, right click clears it back to random. The chosen
  positions are written to `[SpawnLocations]`.
- Interface sounds are embedded (`Resources/Sfx`) and can be switched off in the
  launcher options.

## Localization

UI strings are Russian in code and translated through `Localization.cs`
(`L10n.T` / `L10n.F`). `check_l10n.ps1` audits the translation table against
the strings used in the XAML/code and reports missing or stale entries.
