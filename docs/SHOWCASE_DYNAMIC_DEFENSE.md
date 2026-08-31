# Showcase: Dynamic Objective Defense

**One Lua file. No INI. No TeamTypes. No Triggers. No C++.**

## The Task

Build an autonomous AI defender that protects an oil derrick.

The defender must:

- find the objective automatically;
- receive a manually selected combat unit;
- patrol the objective's flanks;
- detect approaching enemies;
- engage them dynamically;
- return to patrol after the threat is eliminated.

All of this is implemented in a single Lua script.

## How It Works

The mod runs a simple real-time state machine from `Update()`:

1. **Find the objective**  
   Scans `World.GetBuildings()` for an oil derrick (`CAOILD`).

2. **Assign a defender**  
   The defender is assigned manually by the player: select a combat unit and press **Numpad1**. The mod then takes that selected unit as the defender. If no suitable unit is selected, the showcase can spawn an LTNK next to the derrick using `house:SpawnUnit`.

3. **Patrol**  
   The defender continuously moves between two positions on opposite sides of the objective.

4. **Detect and engage**  
   When an enemy enters the defense radius, the defender switches from patrol to combat and issues `unit:Attack`.

   Neutral and civilian units are ignored.

5. **Return to patrol**  
   After the target is destroyed, the defender calls `unit:Stop` and resumes its patrol route.

### Combat Cycle — Runtime Verified

The complete **detect → attack → target eliminated → stop → return to patrol** cycle has been verified in live gameplay against the current LuaAPI build.

The showcase therefore demonstrates a complete closed-loop tactical behavior rather than a scripted sequence: the Lua state observes the current game state, reacts to an approaching hostile unit, issues a native attack command, detects the end of the engagement, and resumes the defensive patrol.

The entire decision loop runs from `Update()` on every logic frame.

**No event hooks are required.**

## API Used

```text
World.GetBuildings
World.GetUnitsInRadius
House.GetPlayer
World.GetSelectedUnits
Input.WasKeyPressed
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
| Select the defender at runtime | Player selection + command plumbing | `World.GetSelectedUnits` + `Input.WasKeyPressed` |
| Patrol / attack / return transitions | TeamTypes + ScriptTypes + Triggers | Lua state machine in `Update()` |
| Pick a target at runtime | Limited, workaround-heavy | `GetUnitsInRadius` + `Attack` |
| Change the behavior | Edit INI, rebuild triggers, or ship a DLL | Edit one Lua file and reload |

The important difference is not that Lua replaces every existing RA2 modding technique.

It is that **runtime gameplay logic can now be expressed directly as code**.

## Run It

Enable `dynamic_objective_defense` in the launcher and start a skirmish on a map containing an oil derrick.

Select the combat unit you want to use as the defender and press **Numpad1**.

Approach the derrick with an enemy unit and observe the full detect → attack → return cycle.

Watch the game log for:

```text
[OBJECTIVE]
```
