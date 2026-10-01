-- Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
-- Add this to a mission with its own authored cockpit overlay/TextArea slots.
-- Hull/ammo relocation stays unavailable until OpenShim qualifies the native
-- adapter. The custom numeric readouts already use stock live game values.
local exu = require("exu")
local Hud = require("exu_hud")
local cockpit

function Start()
    cockpit = Hud.New(exu, {
        -- Example normalized slot bounds; replace with your bezel's instruments.
        hull = { 0.12, 0.70, 0.018, 0.20 },
        ammo = { 0.86, 0.70, 0.018, 0.20 },
    }, {
        -- The cockpit asset creates these TextArea elements before Update.
        hull = "cockpit_hull_value",
        ammo = "cockpit_ammo_value",
    })
end

function Update(dt)
    if cockpit then cockpit.Update() end
end

-- Call from a mission-owned teardown, never from DeleteObject(handle).
local function ShutdownCockpit()
    if cockpit then cockpit.Shutdown(); cockpit = nil end
end

-- Existing composition controls remain separate and can be used alongside it:
-- exu.SetRadarSizeScale(0.85)
-- exu.SetScrapHudTopLeft(80, 700)
-- exu.SetPilotHudTopLeft(80, 730)
-- exu.SetHudSpriteVisible("status_left", false)
