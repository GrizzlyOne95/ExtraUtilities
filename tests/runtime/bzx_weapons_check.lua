-- Live check for the weapon-control bindings (exu.FireWeapons and friends).
-- Paste into a mission's Lua (or run from the console) on any map; it builds
-- its own test unit next to the player. Prints results, asserts nothing.
-- Watch: the tank should fire its first weapon for ~2 s, then stop; disabling
-- slot 0 should stop it firing while selected.

local P = function(...) print("[bzx_weapons]", ...) end
local player = GetPlayerHandle()
local tank = BuildObject("avtank", GetTeamNum(player), GetPositionNear(GetPosition(player), 30, 40))
P("built", tank)

for slot = 0, 4 do
    local hp, odf = exu.GetHardpoint(tank, slot)
    P("slot", slot, "hardpoint", tostring(hp), "odf", tostring(odf),
        "enabled", tostring(exu.GetWeaponEnabled(tank, slot)),
        "special", tostring(exu.IsSpecialActive(tank, slot)))
end
P("player slot 0", exu.GetHardpoint(player, 0))
P("bad slot ->", tostring(exu.GetWeaponEnabled(tank, 7)), tostring(exu.IsSpecialActive(tank, 1.5)), exu.GetHardpoint(tank, -1))
local dead = BuildObject("avtank", GetTeamNum(player), GetPositionNear(GetPosition(player), 60, 80))
RemoveObject(dead)
P("dead handle (expect nil) ->", tostring(exu.FireWeapons(dead)), tostring(exu.CeaseFire(dead)), tostring(exu.GetWeaponEnabled(dead, 0)))
P("bad masks ->", tostring(exu.FireWeapons(tank, 0)), tostring(exu.FireWeapons(tank, 32)), tostring(exu.FireWeapons(tank, 1.5)))

local recycler = GetRecyclerHandle and GetRecyclerHandle(GetTeamNum(player))
if recycler then P("producer SetWeaponEnabled (expect false):", tostring(exu.SetWeaponEnabled(recycler, 0, false))) end

local frame, phase = 0, "fire"
local oldUpdate = Update
function Update(...)
    if oldUpdate then oldUpdate(...) end
    if not IsAlive(tank) then return end
    frame = frame + 1
    if phase == "fire" then
        local fired = exu.FireWeapons(tank, 1)
        if frame == 1 or frame % 20 == 0 then P("frame", frame, "FireWeapons(tank,1) ->", tostring(fired)) end
        if frame == 40 then
            P("CeaseFire ->", tostring(exu.CeaseFire(tank)))
            P("SetWeaponEnabled(0,false) ->", tostring(exu.SetWeaponEnabled(tank, 0, false)),
                "now", tostring(exu.GetWeaponEnabled(tank, 0)))
            phase = "disabled"
        end
    elseif phase == "disabled" then
        local fired = exu.FireWeapons(tank, 1)
        if frame % 20 == 0 then P("disabled frame", frame, "FireWeapons ->", tostring(fired), "(expect false)") end
        if frame == 80 then
            P("SetWeaponEnabled(0,true) ->", tostring(exu.SetWeaponEnabled(tank, 0, true)),
                "now", tostring(exu.GetWeaponEnabled(tank, 0)))
            P("FireSpecialWeapon(player) ->", tostring(exu.FireSpecialWeapon(player)))
            for slot = 0, 4 do P("player special", slot, tostring(exu.IsSpecialActive(player, slot))) end
            phase = "done"
            P("done")
        end
    end
end
