-- Live check for the environment hazard bindings (lightning, lava, glare).
-- Paste into a mission script (or the Lua console) on any map; it prints
-- every result. Lava damage needs a TRN [NormalView] Lava material, or a
-- SetLava index that exists on the map, to be seen on a craft.
local function show(name, ...) print("bzx_env", name, ...) end

show("GetLightningBolts", exu.GetLightningBolts())
show("GetLightningColor", exu.GetLightningColor())
show("GetLightningDistance", exu.GetLightningDistance())
show("GetLightningDuration", exu.GetLightningDuration())
show("GetLightningMinDelay", exu.GetLightningMinDelay())
show("GetLightningMaxDelay", exu.GetLightningMaxDelay())
show("GetLightningSound", exu.GetLightningSound())

show("SetLightningColor", exu.SetLightningColor(160, 180, 255, 255), exu.GetLightningColor())
show("SetLightningColor bad", exu.SetLightningColor(300, 0, 0))
show("SetLightningDistance", exu.SetLightningDistance(400), exu.GetLightningDistance())
show("SetLightningDuration", exu.SetLightningDuration(150), exu.GetLightningDuration())
show("SetLightningMinDelay", exu.SetLightningMinDelay(1000), exu.GetLightningMinDelay())
show("SetLightningMaxDelay", exu.SetLightningMaxDelay(3000), exu.GetLightningMaxDelay())
show("SetLightningDuration bad", exu.SetLightningDuration(-1))
show("SetLightningSound", exu.SetLightningSound("thunder.wav"), exu.GetLightningSound())
show("SetLightningSound too long", exu.SetLightningSound("sixteen_chars_xx"))

show("StartLightning(4)", exu.StartLightning(4), exu.GetLightningBolts()) -- expect true 4
show("StartLightning(17)", exu.StartLightning(17))                         -- expect false
show("FireLightning", exu.FireLightning())                                 -- expect strikes next frame
show("StopLightning", exu.StopLightning(), exu.GetLightningBolts())        -- expect true 0
show("FireLightning stopped", exu.FireLightning())                         -- expect 0
show("StartLightning()", exu.StartLightning(), exu.GetLightningBolts())    -- expect true 4

show("StopLava", exu.StopLava())
show("StartLava", exu.StartLava())       -- false if the TRN had no lava material
show("SetLava(3)", exu.SetLava(3))
show("SetLava(16)", exu.SetLava(16))     -- expect false
show("StartLava(80)", exu.StartLava(80)) -- twice stock damage on material 3

show("SetGlare", exu.SetGlare(0.5))
show("SetGlare bad", exu.SetGlare(-1))   -- expect false
-- Sustained glare needs a per-frame call, e.g. in Update():
-- exu.SetGlare(0.5)
