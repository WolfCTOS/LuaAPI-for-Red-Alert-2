# LuaAPI Modding Tutorial — Your First Gameplay Mod

This guide walks you through writing a complete, working gameplay mod in about
30 lines of pure Lua. No C++, no compilers — just a text editor and the game.

---

## 1. How Mods Are Structured

The engine ships with a **Universal ModLoader** (`scripts/init.lua`). On every
game tick it calls `Update(frame)` on each active mod. You never touch
`init.lua` logic — you only edit two things:

```
scripts/
├── init.lua                      ← 1. add your mod name to ACTIVE_MODS
└── mods/
    └── my_mod/
        └── main.lua              ← 2. your entire mod lives here
```

**Step 1** — register the mod in `scripts/init.lua`:

```lua
local ACTIVE_MODS = {
    "tesla_overload",
    "my_mod",          -- ← add this line
}
```

**Step 2** — create `scripts/mods/my_mod/main.lua`. The minimal skeleton:

```lua
local MyMod = {}

function MyMod.Update(frame)
    -- runs every game frame (~30-60 fps), only during an active battle
end

return MyMod
```

That's it. The loader wraps every `Update` call in `pcall`, so a broken mod
logs an error and keeps running — it can never crash the game.

> **Tip:** mods are hot-swappable for testing. Comment out a name in
> `ACTIVE_MODS`, save, restart the match — no recompilation involved.

---

## 2. Showcase: Instant Tank Cook-Off (~30 lines)

This real, shipped mod makes every destroyed vehicle explode in a fireball
that ignites nearby units (the classic "ammo cook-off"):

```lua
local DelayedExplosion = {}

local BLAST_RADIUS = 3.0      -- 3 cells blast radius
local BLAST_DAMAGE = 150      -- lethal to infantry

local trackedTanks = {}

function DelayedExplosion.Update(frame)
    local currentUnits = World.GetUnits()
    local livingIds = {}

    -- 1. Track currently living vehicles/tanks
    for _, u in ipairs(currentUnits) do
        if u:IsAlive() and u:GetKind() == "unit" then
            local id = u:GetId()
            livingIds[id] = true
            trackedTanks[id] = { pos = u:GetPosition(), typeName = u:GetTypeName() }
        end
    end

    -- 2. A tracked ID that vanished this frame = destroyed -> detonate NOW
    for id, data in pairs(trackedTanks) do
        if not livingIds[id] then
            local hitCount = 0
            for _, target in ipairs(currentUnits) do
                if target:IsAlive() then
                    local p = target:GetPosition()
                    local dx, dy = p.x - data.pos.x, p.y - data.pos.y
                    if math.sqrt(dx * dx + dy * dy) <= BLAST_RADIUS then
                        target:TakeDamage(BLAST_DAMAGE, "TerrorBombWH")
                        hitCount = hitCount + 1
                    end
                end
            end
            Engine.PrintMessage(string.format("💥 %s cooked off! (%d caught fire)",
                data.typeName, hitCount))
            trackedTanks[id] = nil
        end
    end
end

return DelayedExplosion
```

Key ideas to steal:

- **Track by identity:** `GetId()` returns an engine-wide unique number that
  survives movement. Comparing "who was alive last frame" against "who is alive
  now" is how you detect deaths.
- **Filter by kind:** `GetKind()` returns `"building" | "unit" | "infantry" |
  "aircraft"`, letting you target exactly one category.
- **Use real warheads:** `TakeDamage(n, "TerrorBombWH")` goes through the
  engine's damage pipeline — proper animations, sounds and kill credit.

---

## 3. Core API Cheat-Sheet

### Global namespaces

| Call | Returns |
|---|---|
| `House.GetPlayer()` | Local player's house handle (or `nil`) |
| `House.GetCount()` / `House.GetByIndex(i)` | Scenario house enumeration |
| `World.GetBuildings()` | All buildings |
| `World.GetUnits()` | All mobile units (vehicles + infantry + aircraft) |

### House handles

| Method | Description |
|---|---|
| `h:GetName()` | Internal ID, e.g. `"Americans"` |
| `h:GetCredits()` / `SetCredits(n)` / `AddCredits(n)` | Economy control |
| `h:GetPowerOutput()` / `GetPowerDrain()` | Power grid status |
| `h:IsHuman()` / `h:IsAlliedWith(other)` | Relations |

### Techno handles (units & buildings)

| Method | Description |
|---|---|
| `o:GetId()` | Unique engine ID — stable for the object's lifetime |
| `o:GetTypeName()` | Rules INI name, e.g. `"MTNK"` |
| `o:GetHealth()` / `GetMaxHealth()` | HP values |
| `o:GetOwner()` | Owning house handle |
| `o:GetPosition()` | `{x, y, z}` in map cells |
| `o:IsAlive()` | Health > 0 and not in limbo |
| `o:GetDistanceTo(other)` | Distance in cells |
| `o:GetKind()` | `"unit"` / `"infantry"` / `"aircraft"` / `"building"` |
| `o:TakeDamage(dmg [, warhead])` | Real engine damage; warhead defaults to `"Fire"`, falls back to `"TerrorBombWH"` → `"DemobombWH"` → C4 |
| `o:Disable(frames)` | EMP-style lockout (buildings go offline, units paralyzed); auto-restores |

### Globals

| Name | Description |
|---|---|
| `print(...)` | Writes to `LuaAPI.log` (tagged `[script]`) |
| `OnTick(frame)` | Define in `init.lua`; the loader fans out to all mods |

---

## 4. Debugging

Everything your mod prints lands in `LuaAPI.log` next to `LuaAPI.dll`:

```
[12:00:01.123] [info] [script] [LuaAPI] 💥 MTNK cooked off! (2 caught fire)
[12:00:01.124] [info] [Combat] E1 took 150 damage via warhead 'TerrorBombWH', HP remaining: 0
```

- `[script]` lines are your `print()` output.
- `[Combat]` lines show every damage application with the warhead used.
- Script errors appear as `OnTick error:` entries with the Lua message — the
  game itself never crashes from them.

Happy modding, Commander.
