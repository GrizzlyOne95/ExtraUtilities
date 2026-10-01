-- Host checks for authored slots, late provider availability, reflow and live values.
local Hud = dofile("Workshop/exu_hud.lua")
local player = 1
local reads, captions, sets, restores = {}, {}, {}, {}
local available, frame, width, height, paused = false, false, 1000, 800, false
function GetPlayerHandle() return player end
function IsValid(h) return h ~= 0 end
function GetCurHealth(h) reads[#reads + 1] = h; return h * 10 end
function GetMaxHealth(h) return 100 end
function GetCurAmmo(h) reads[#reads + 1] = h; return h * 20 end
function GetMaxAmmo(h) return 200 end
local api = {
    GetGameResolution = function() return width, height end,
    IsNativeHudLayoutAvailable = function() return available end,
    GetNativeHudMeterDefaultRect = function() if frame then return 10, 20, 30, 40 end end,
    SetNativeHudMeterRect = function(meter, x, y, w, h)
        sets[#sets + 1] = {meter, x, y, w, h}; return true
    end,
    RestoreNativeHudMeter = function(meter) restores[#restores + 1] = meter; return true end,
    HasOverlayElement = function() return true end,
    SetOverlayCaption = function(name, caption) captions[name] = caption end,
    IsGameUiOpen = function() return paused end,
}
local controller = Hud.New(api, {hull={0.1, 0.5, 0.02, 0.25}, ammo={0.8, 0.5, 0.02, 0.25}},
    {hull="hull_value", ammo="ammo_value"})
assert(not controller.Update() and #sets == 0)
assert(captions.hull_value == "10 / 100" and captions.ammo_value == "20 / 200")
available = true
assert(not controller.Update() and #sets == 0)
assert(controller.status.hull == "awaiting_frame")
frame = true
assert(controller.Update() and #sets == 2)
assert(sets[1][1] == "hull" and sets[1][2] == 100 and sets[1][3] == 400 and sets[1][4] == 20 and sets[1][5] == 200)
assert(controller.Update() and #sets == 2) -- stable viewport doesn't resubmit
width, height = 2000, 1000
player = 2
assert(controller.Update() and #sets == 4)
assert(sets[3][2] == 200 and sets[3][3] == 500 and sets[3][4] == 40 and sets[3][5] == 250)
assert(captions.hull_value == "20 / 100" and reads[#reads] == 2)
paused = true
controller.Update()
assert(captions.hull_value == "" and captions.ammo_value == "")
paused = false
player = 0
controller.Update()
assert(captions.hull_value == "") -- no stale value after player disappears
controller.Shutdown()
assert(#restores == 2 and restores[1] == "hull" and restores[2] == "ammo")
controller.Shutdown()
assert(#restores == 2 and not controller.Update())
api.IsNativeHudLayoutAvailable = nil -- older EXU still supports numeric text
player = 3
controller = Hud.New(api, {hull={0.1, 0.5, 0.02, 0.25}}, {hull="hull_value"})
assert(not controller.Update() and captions.hull_value == "30 / 100")
controller.Shutdown()
assert(#restores == 2) -- never restore an unrelated or unaccepted meter
assert(not pcall(Hud.New, api, {hull={0, 0, 0/0, 1}}))
assert(not pcall(Hud.New, api, {hull={0, 0, 1, -1}}))
print("Native HUD controller checks passed.")
