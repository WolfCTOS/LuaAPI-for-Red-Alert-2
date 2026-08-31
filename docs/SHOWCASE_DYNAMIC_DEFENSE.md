# Showcase: Dynamic Objective Defense

**One Lua file. No INI. No TeamTypes. No Triggers. No C++.**

## The Task

Build an autonomous AI defender that protects an oil derrick.

The defender must:

- find the objective automatically;
- select a combat unit;
- patrol the objective's flanks;
- detect approaching enemies;
- engage them dynamically;
- return to patrol after the threat is eliminated.

All of this is implemented in a single Lua script.

## How It Works

The mod runs a simple real-time state machine from `Update()`:

1. **Find the objective**  
   Scans `World.GetBuildings()` for an oil derrick (`CAOILD`).

2. **Select a defender**  
   Picks the player's first available combat unit, skipping the MCV. If no suitable unit exists, an LTNK is spawned next to the derrick using `house:SpawnUnit`.

3. **Patrol**  
   The defender continuously moves between two positions on opposite sides of the objective.

4. **Detect and engage**  
   When an enemy enters the defense radius, the defender switches from patrol to combat and issues `unit:Attack`.

   Neutral and civilian units are ignored.

5. **Resume defense**  
   Once the target is destroyed, the defender calls `unit:Stop` and returns to its patrol route.

The entire decision loop runs from `Update()` on every logic frame.

**No event hooks are required.**

## API Used

```text
World.GetBuildings
World.GetUnitsInRadius
House.GetPlayer
house:SpawnUnit
house:GetName
house:IsAlliedWith
unit:GetOwner
unit:GetTypeName
unit:GetId
unit:Attack
unit:MoveTo
unit:Stop
unit:IsIdle
```

## The Same Task Without LuaAPI

| Aspect | Ares / Phobos | LuaAPI |
|---|---|---|
| Bind AI to an objective | Waypoints + Triggers | `World.GetBuildings` scan |
| Patrol / attack / return transitions | TeamTypes + ScriptTypes + Triggers | Lua state machine in `Update()` |
| Pick a target at runtime | Limited, workaround-heavy | `GetUnitsInRadius` + `Attack` |
| Change the behavior | Edit INI, rebuild triggers, or ship a DLL | Edit one Lua file and reload |

The important difference is not that Lua replaces every existing RA2 modding technique.

It is that **runtime gameplay logic can now be expressed directly as code**.

## Run It

Enable `dynamic_objective_defense` in the launcher and start a skirmish on a map containing an oil derrick.

Watch the game log for:

```text
[OBJECTIVE]
```

Then approach the derrick and watch the defender react.
