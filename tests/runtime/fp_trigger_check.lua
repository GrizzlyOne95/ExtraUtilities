-- In-game check for the trigger-driven first-person layer (SetLayer `fire`
-- option, exu.fps.IsTriggerHeld).
--
-- Once an on-foot first-person target exists, creates
--
--     exu.fps.SetLayer("barrelSpin", { loop = true, speed = 0, weight = 1,
--         fire = { speed = 3, spinUp = 0.4, spinDown = 1.2 } })
--
-- and keeps it for the rest of the mission: the barrel should spin up to
-- 3 rev/s over 0.4 s while the fire bind is held and coast down over 1.2 s
-- when it is released. Prints a line whenever exu.fps.IsTriggerHeld()
-- changes and once a second while it is held, with the layer's
-- effectiveSpeed from exu.fps.GetLayers(). Output lines are prefixed
-- "[FPTRIG] ".
--
-- When the first-person target goes away and comes back (respawn, hopping
-- out of a vehicle again) and the layer then reports inactive (or is gone,
-- e.g. after a Lua-state reset), the layer is applied again.
--
-- This file is not shipped. Copy it next to a test mission script (ISDFC's
-- Test Range has the minigun rig with a barrelSpin clip), then:
--
--     local check = require("fp_trigger_check").New(exu)
--     function Update()
--         check:Update()
--     end
--
-- check:Dump() prints the layer state at any time; check:Stop() clears the
-- layer and ends the check.
--
-- Options (second argument to New):
--   clip            layer clip (default "barrelSpin")
--   speed, weight   base speed / weight (default 0 / 1)
--   fire            fire table (default { speed = 3, spinUp = 0.4, spinDown = 1.2 })
--   heldInterval    seconds between lines while held (default 1)
--   print, getTime  injectable (default the globals)
--
-- Lua 5.1; uses no io/os/debug.

local M = {}

local PREFIX = "[FPTRIG] "

local function fmt(value)
    if type(value) == "number" then
        if value == math.floor(value) and math.abs(value) < 1e9 then
            return string.format("%d", value)
        end
        return string.format("%.3f", value)
    end
    if value == nil then
        return "-"
    end
    return tostring(value)
end

local Check = {}
Check.__index = Check

function M.New(exu, options)
    options = options or {}
    local self = setmetatable({}, Check)
    self.exu = exu
    self.clip = options.clip or "barrelSpin"
    self.speed = options.speed or 0
    self.weight = options.weight or 1
    self.fire = options.fire or { speed = 3, spinUp = 0.4, spinDown = 1.2 }
    self.heldInterval = options.heldInterval or 1
    self.print = options.print or print
    self.getTime = options.getTime or GetTime
    self.state = "boot"
    self.targetPresent = false
    -- A target (re)appeared since the layer was last confirmed active.
    self.recheck = false
    self.held = false
    self.lastHeldLine = 0
    self.lastReason = nil
    return self
end

function Check:Line(text)
    self.print(PREFIX .. text)
end

-- Our layer's GetLayers() entry, or nil.
function Check:Layer()
    for _, layer in ipairs(self.exu.fps.GetLayers()) do
        if layer.name == self.clip then
            return layer
        end
    end
    return nil
end

function Check:Describe(layer)
    if layer == nil then
        return "layer=absent"
    end
    return string.format("effectiveSpeed=%s speed=%s weight=%s time=%s active=%s reason=%s triggerHeld=%s",
        fmt(layer.effectiveSpeed), fmt(layer.speed), fmt(layer.weight), fmt(layer.time),
        fmt(layer.active), fmt(layer.reason), fmt(layer.triggerHeld))
end

function Check:Dump(label)
    self:Line((label or "state") .. ": held=" .. fmt(self.exu.fps.IsTriggerHeld()) .. " "
        .. self:Describe(self:Layer()))
end

function Check:Apply(why)
    self.exu.fps.SetLayer(self.clip, {
        loop = true,
        speed = self.speed,
        weight = self.weight,
        fire = { speed = self.fire.speed, spinUp = self.fire.spinUp, spinDown = self.fire.spinDown },
    })
    self:Line(string.format("SetLayer('%s', speed=%s weight=%s fire={speed=%s spinUp=%s spinDown=%s}) (%s)",
        self.clip, fmt(self.speed), fmt(self.weight), fmt(self.fire.speed), fmt(self.fire.spinUp),
        fmt(self.fire.spinDown), why))
end

-- After a target change: re-apply when the layer is gone or reports inactive
-- for a reason re-applying can fix. "pending" waits a tick, "engineOwned" is
-- the FSM playing the clip, "missing" means the skeleton has no such clip.
function Check:Recheck()
    local layer = self:Layer()
    if layer ~= nil and layer.active then
        self:Line("target change: layer active again; " .. self:Describe(layer))
        self.recheck = false
        return
    end
    local reason = layer ~= nil and layer.reason or "absent"
    if reason == "pending" or reason == "engineOwned" then
        return
    end
    if reason == "missing" then
        self:Line("target change: '" .. self.clip .. "' is missing on this first-person skeleton; waiting")
        self.recheck = false
        return
    end
    self:Apply("re-apply after target change, was " .. reason)
    self.recheck = false
end

function Check:Update()
    if self.state == "done" then
        return
    end
    local now = self.getTime()
    local fps = self.exu.fps

    if self.state == "boot" then
        local caps = fps.GetCapabilities()
        self:Line("firstPersonLayers=" .. fmt(caps.firstPersonLayers)
            .. " firstPersonTrigger=" .. fmt(caps.firstPersonTrigger)
            .. " pilotFsmIntercept=" .. fmt(caps.pilotFsmIntercept))
        if not caps.firstPersonLayers then
            self:Line("layers unavailable in this session (see exu.log); DONE")
            self.state = "done"
            return
        end
        if not caps.firstPersonTrigger then
            self:Line("trigger signal unavailable (see exu.log): IsTriggerHeld stays false, the layer stays at its base speed")
        end
        self.state = "waiting"
        self:Line("waiting for an on-foot first-person target")
    end

    local present = fps.IsAvailable()
    if present ~= self.targetPresent then
        self.targetPresent = present
        self:Line("first-person target " .. (present and "present" or "gone"))
        if present and self.state == "running" then
            self.recheck = true
        end
    end

    if self.state == "waiting" then
        if not present then
            return
        end
        if not fps.HasAnimation(self.clip) then
            self:Line("note: '" .. self.clip .. "' is not on this first-person skeleton; the layer will report missing")
        end
        self:Apply("first target")
        self.state = "running"
        return
    end

    if self.recheck and present then
        self:Recheck()
    end

    local layer = self:Layer()
    if layer == nil and not self.recheck then
        -- Gone without a target change (e.g. a Lua-state reset cleared it).
        self:Apply("layer disappeared")
        layer = self:Layer()
    end
    local reason = layer ~= nil and (layer.active and "active" or layer.reason) or "absent"
    if reason ~= self.lastReason then
        self.lastReason = reason
        self:Line("layer state: " .. self:Describe(layer))
    end

    local held = fps.IsTriggerHeld()
    if held ~= self.held then
        self.held = held
        self.lastHeldLine = now
        self:Line((held and "trigger HELD: " or "trigger released: ") .. self:Describe(layer))
    elseif held and now - self.lastHeldLine >= self.heldInterval then
        self.lastHeldLine = now
        self:Line("held: " .. self:Describe(layer))
    end
end

function Check:Stop()
    if self.state == "running" then
        self.exu.fps.ClearLayer(self.clip)
        self:Line("cleared layer '" .. self.clip .. "'; DONE")
    end
    self.state = "done"
end

return M
