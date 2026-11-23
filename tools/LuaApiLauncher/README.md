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

## Faction setup

Skirmish factions come from `[Factions]` in `rulesmd.ini`. Groups are read from
`[FactionGroups]`: a faction listed there is rendered under a non-selectable
header (the first member inserts the header). `FactionChoice` and
`RulesCountries.cs` implement this; `SkirmishSpawn.ResolveCountry` ignores
header rows so Random and free-country fallback never pick one.

Faction flag icons live in `Resources/`. `FactionIconAliases` in
`MainWindow.xaml.cs` maps faction keys to icon names when the rules `Name=`
no longer matches a flag file (for example `EU Artillery Command` -> `France`).
Hover descriptions are Russian keys in `Localization.cs`, translated through
`L10n.T`; the dark tooltip style is in `App.xaml`.

## Game assets (not in this repository)

Executables, `.dll`, `.pdb` and `.mix` files are intentionally gitignored: they
are build outputs or third-party game assets. To assemble a working install:

1. Use a clean Yuri's Revenge 1.001 install with Ares and Phobos.
2. Copy `artmd.ini` and `rulesmd.ini` from `Yuris-Echo/config/` into the game root.
3. Add the GGAIRC SHP/VXL assets to `ecachemd01.mix` (XCC Mixer) keeping exact
   filenames: `GGAIRC.SHP`, `GGAIRC_A.SHP`, `GGAIRC_B.SHP`, `GGAIRCBB.SHP`,
   `GGAIRCMK.SHP`, and `M1.VXL` / `M1.HVA` for the AM1 tank.
4. Merge the `[GAAIRC]`, `[GAAIRCBB]`, `[GAAIRC_A]`, `[GAAIRC_B]` sections from
   the airfield replacement patch into `artmd.ini`; do not replace the whole file.
5. Build the launcher with `build-and-deploy.ps1`, then start `Yuri's Echo.exe`
   from the game folder.
