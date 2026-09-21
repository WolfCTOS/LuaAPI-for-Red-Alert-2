-- Focused probe: CP timeline around a2's death.
local function mkHouse(name)
    return {
        name = name,
        GetName = function(self) return self.name end,
        spawned = {},
        SpawnUnit = function(self, typeId, count) self.spawned[#self.spawned + 1] = { typeId, count }; return count end,
    }
end
local houseP = mkHouse("PlayerHouse")
local houseA = mkHouse("AIBoss")

local units, nextId = {}, 1
local function mkUnit(house, maxhp, kind)
    local u = { id = nextId, owner = house, maxhp = maxhp, hp = maxhp,
                kind = kind or "unit", tname = "T", disabledUntil = 0, dead = false }
    nextId = nextId + 1
    function u:IsAlive() return not self.dead end
    function u:GetOwner() return self.owner end
    function u:GetKind() return self.kind end
    function u:GetHealth() return self.hp end
    function u:GetMaxHealth() return self.maxhp end
    function u:GetTypeName() return self.tname end
    function u:GetId() return self.id end
    function u:GetPosition() return { x = 10, y = 20, z = 0 } end
    function u:Disable(f) self.disabledUntil = f end
    function u:SetHealthRatio(r) self.hp = math.floor(self.maxhp * r + 0.5) end
    units[#units + 1] = u
    return u
end

local p1 = mkUnit(houseP, 400)
local p2 = mkUnit(houseP, 400)
local p3 = mkUnit(houseP, 400)
local a1 = mkUnit(houseA, 400); a1.hp = 120
local a2 = mkUnit(houseA, 400)
local a3 = mkUnit(houseA, 400)

World = { GetUnits = function() return units end, GetBuildings = function() return {} end }
House = { GetPlayer = function() return houseP end, GetCount = function() return 2 end,
          GetByIndex = function(i) return ({ [0] = houseP, [1] = houseA })[i] end }
local pressed = {}
Input = { WasKeyPressed = function(vk) return pressed[vk] == true end }
local hud = {}
Engine = { PrintMessage = function(m) hud[#hud + 1] = m end }

local f = assert(loadfile("D:/Games/Red Alert 2 LuaAPI/scripts/mods/command_authority/main.lua"))
local AUTH = f()

local S_LAST = 1
local function runTo(frameEnd)
    for frame = S_LAST + 1, frameEnd do
        pressed = {}
        AUTH.Update(frame)
        S_LAST = frame
    end
end

AUTH.Update(1)
runTo(30)        -- all units seen alive
a3.dead = true
runTo(120)       -- death of a3 detected -> +5

pressed[0x54] = true; AUTH.Update(121); pressed = {}
print("--- after a3 death:")
for _, line in ipairs(hud) do print(line) end
hud = {}

a2.dead = true
runTo(240)       -- death of a2 detected -> +5

pressed[0x54] = true; AUTH.Update(241); pressed = {}
print("--- after a2 death:")
for _, line in ipairs(hud) do print(line) end
print("--- CP trace done; expect player 3+5+5=13, AI 0")
