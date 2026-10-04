-- Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
-- A BZ2-demo style status ring: hull on the upper-left quarter, ammo on the
-- lower-left quarter, percentages inside and weapon lines beside it. It draws
-- over the stock HUD; hiding the stock hull/ammo bars needs the native meter
-- hook that is not qualified yet.
local exu = require("exu")
local Ring = require("exu_ring_gauge")
local status

function Start()
    -- Defaults to the lower-left corner sized from the screen height; pass
    -- x, y and radius (pixels) to place it yourself.
    status = Ring.NewStatus(exu, { name = "bz2_status" })
end

function Update(dt)
    if status then status.Update() end
end

-- A single gauge for anything else, e.g. a shield or boost meter:
--   local gauge = Ring.New(exu, { name = "boost", parent = "my_panel",
--       x = 200, y = 200, radius = 80, start = 0, step = 18, on = { 1, 0.5, 0, 1 } })
--   gauge.SetRatio(0.4)
