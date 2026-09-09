-- Weapon Override Demo
--
-- Demonstrates the WeaponOverride API and the routing contract:
--   Lua override exists  -> use it
--   no override (nil)    -> original vanilla weapon-selection (engine is source of truth)
--
-- Four cases required:
--   rookie  -> vanilla (we register NO rookie override -> Get returns nil)
--   veteran -> Lua override ("VeteranGun")
--   elite   -> Lua override ("EliteGun")
--   absent  -> vanilla (we register nothing for "GTNK", so every level is nil -> vanilla)
--
-- NOTE: the mod registers overrides per (unit type, veterancy). No INI change is
-- made and no vanilla weapon-selection rule is re-implemented in Lua: the engine
-- always resolves the weapon first (via GetPrimaryWeapon), and this override only
-- swaps the WeaponType before the shot when a matching entry exists.

local Demo = {}

local DEMO_TYPE  = "HTNK"  -- a unit type to attach veteran/elite overrides to
local ABSENT_TYPE = "GTNK" -- a type we intentionally leave untouched -> vanilla fallback

-- Weapons are looked up by rules ID. Guard with Engine.WeaponExists so the demo
-- stays clean if a given rules set omits one of these IDs.
local VETERAN_WEAPON = "Maverick"  -- fast homing missile
local ELITE_WEAPON   = "BlimpBomb" -- big AoE bomb
local FALLBACK_WEAPON = "BlimpBomb"

local function trySet(typeId, vet, weaponId)
    if Engine.WeaponExists(weaponId) then
        local ok = WeaponOverride.Set(typeId, vet, weaponId)
        print(string.format("[WeaponDemo] Set(%q, %q, %q) -> %s",
            typeId, vet, weaponId, tostring(ok)))
        return ok
    end
    print(string.format("[WeaponDemo] Set(%q, %q, %q) skipped: '%s' not in rules",
        typeId, vet, weaponId, weaponId))
    return false
end

local function showRouting(typeId)
    local r = WeaponOverride.Get(typeId, "rookie")
    local v = WeaponOverride.Get(typeId, "veteran")
    local e = WeaponOverride.Get(typeId, "elite")
    print(string.format(
        "[WeaponDemo] routing %q -> rookie=%s | veteran=%s | elite=%s (nil = vanilla fallback)",
        typeId,
        r and tostring(r) or "nil",
        v and tostring(v) or "nil",
        e and tostring(e) or "nil"))
    return r, v, e
end

function Demo.OnScenarioStart()
    print("[WeaponDemo] ---- weapon override demo starting ----")

    -- 1. Override veteran + elite for the demo type. Rookie intentionally left nil.
    trySet(DEMO_TYPE, "veteran", VETERAN_WEAPON)
    trySet(DEMO_TYPE, "elite",   ELITE_WEAPON)

    -- 2. Show the routing matrix for the overridden type.
    local r, v, e = showRouting(DEMO_TYPE)

    -- Assert the three required veterancy behaviours.
    if r ~= nil and v == VETERAN_WEAPON and e == ELITE_WEAPON then
        print("[WeaponDemo] OK: rookie=vanilla, veteran/elite=Lua override")
        Engine.PrintMessage("WeaponOverride demo: rookie=vanilla, veteran/elite=override")
    else
        print(string.format("[WeaponDemo] WARN: unexpected routing (rookie=%s veteran=%s elite=%s)",
            tostring(r), tostring(v), tostring(e)))
        Engine.PrintMessage("WeaponOverride demo: routing mismatched (see LuaAPI.log)")
    end

    -- 3. A type with NO override at all -> every level falls back to vanilla.
    print(string.format("[WeaponDemo] registering nothing for %q -> expected all nil",
        ABSENT_TYPE))
    showRouting(ABSENT_TYPE)

    -- 4. Verify Clear() is round-trip (clear elite, re-register a fallback).
    WeaponOverride.Clear(DEMO_TYPE, "elite")
    WeaponOverride.Set(DEMO_TYPE, "elite", FALLBACK_WEAPON)
    showRouting(DEMO_TYPE)

    print("[WeaponDemo] ---- demo done ----")
end

function Demo.OnTick(frame)
    -- Nothing per-frame; the routing is printed once on scenario start.
end

return Demo
