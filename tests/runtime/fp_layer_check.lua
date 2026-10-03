-- In-game check for EXU-clocked first-person layers (exu.fps.SetLayer).
--
-- Creates one layer on the local pilot's first-person skeleton and cycles its
-- speed 0 -> 1 -> 3 -> 0, holding each speed for a few seconds, printing the
-- capability and exu.fps.GetLayers() state at the start and end of every
-- phase. Output lines are prefixed "[FPLAYER] ".
--
-- This file is not shipped. In ISDFC's Test Range (or any test mission;
-- layers are presentation-only, so multiplayer works too) copy it next to the
-- mission script, then:
--
--     local check = require("fp_layer_check").New(exu)
--     function Update()
--         check:Update()
--     end
--
-- Hop out of the vehicle and watch the first-person view. check:Dump() prints
-- the current layer state at any time; check:Stop() clears the layer.
--
-- Clip choice: `clip` (default "barrelSpin", the ISDFC minigun rig's barrel
-- loop on issoldfp.skeleton, bone msfp_spin, 1 s per revolution) when the
-- first-person skeleton has it; otherwise `fallback` (default "runForward", a
-- stock looping clip) at `fallbackWeight` (default 0.3), so the loop is
-- visible on the stock rig while standing still. The stock skeleton blends
-- "average", so the fallback visibly distorts the whole pose: that is
-- expected and is why real layer rigs must use blendmode "cumulative". While
-- the pilot actually runs, the FSM itself plays runForward and the layer
-- reports reason "engineOwned" (EXU leaves the clip to the engine).
--
-- Options (second argument to New):
--   clip            preferred clip (default "barrelSpin")
--   fallback        clip used when `clip` is missing (default "runForward")
--   fallbackWeight  weight for the fallback (default 0.3)
--   weight          weight for `clip` (default 1.0)
--   speeds          speed per phase (default { 0, 1, 3, 0 })
--   phaseSeconds    seconds per phase (default 4)
--   print, getTime  injectable (default the globals)
--
-- Lua 5.1; uses no io/os/debug.

local M = {}

local PREFIX = "[FPLAYER] "

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
    self.fallback = options.fallback or "runForward"
    self.fallbackWeight = options.fallbackWeight or 0.3
    self.weight = options.weight or 1.0
    self.speeds = options.speeds or { 0, 1, 3, 0 }
    self.phaseSeconds = options.phaseSeconds or 4
    self.print = options.print or print
    self.getTime = options.getTime or GetTime
    self.state = "boot"
    self.layer = nil
    self.phase = 0
    self.phaseStart = 0
    return self
end

function Check:Line(text)
    self.print(PREFIX .. text)
end

function Check:Dump(label)
    local layers = self.exu.fps.GetLayers()
    if #layers == 0 then
        self:Line((label or "state") .. ": no layers")
        return
    end
    for _, layer in ipairs(layers) do
        self:Line(string.format(
            "%s: name=%s speed=%s weight=%s loop=%s time=%s length=%s active=%s reason=%s blendMode=%s",
            label or "state", layer.name, fmt(layer.speed), fmt(layer.weight), fmt(layer.loop),
            fmt(layer.time), fmt(layer.length), fmt(layer.active), fmt(layer.reason), fmt(layer.blendMode)))
    end
end

-- Picks the clip once a first-person target exists. False while there is none.
function Check:Choose()
    local fps = self.exu.fps
    if not fps.IsAvailable() then
        return false
    end
    if fps.HasAnimation(self.clip) then
        self.layer = self.clip
        self.layerWeight = self.weight
        self:Line("using clip '" .. self.clip .. "' weight " .. fmt(self.weight))
    elseif fps.HasAnimation(self.fallback) then
        self.layer = self.fallback
        self.layerWeight = self.fallbackWeight
        self:Line("clip '" .. self.clip .. "' missing on the first-person skeleton; using fallback '"
            .. self.fallback .. "' weight " .. fmt(self.fallbackWeight))
        self:Line("note: on an \"average\"-blend skeleton (stock rig) the fallback distorts the pose; expected")
    else
        self:Line("neither '" .. self.clip .. "' nor '" .. self.fallback .. "' exists on the first-person skeleton; DONE")
        self.state = "done"
        return false
    end
    return true
end

function Check:BeginPhase(index, now)
    self.phase = index
    self.phaseStart = now
    local speed = self.speeds[index]
    self.exu.fps.SetLayerSpeed(self.layer, speed)
    self:Line(string.format("phase %d/%d: speed %s for %s s", index, #self.speeds, fmt(speed),
        fmt(self.phaseSeconds)))
    self:Dump("phase " .. index .. " start")
end

function Check:Update()
    if self.state == "done" then
        return
    end
    local now = self.getTime()

    if self.state == "boot" then
        local caps = self.exu.fps.GetCapabilities()
        self:Line("firstPersonLayers=" .. fmt(caps.firstPersonLayers)
            .. " pilotFsmIntercept=" .. fmt(caps.pilotFsmIntercept))
        if not caps.firstPersonLayers then
            self:Line("layers unavailable in this session (see exu.log); DONE")
            self.state = "done"
            return
        end
        self.state = "waiting"
        self:Line("waiting for an on-foot first-person target")
    end

    if self.state == "waiting" then
        if not self:Choose() then
            return
        end
        self.exu.fps.SetLayer(self.layer, { speed = 0, weight = self.layerWeight, loop = true, time = 0 })
        self.state = "running"
        self:BeginPhase(1, now)
        return
    end

    if now - self.phaseStart >= self.phaseSeconds then
        self:Dump("phase " .. self.phase .. " end")
        if self.phase >= #self.speeds then
            self:Stop()
            self:Line("DONE")
            self.state = "done"
            return
        end
        self:BeginPhase(self.phase + 1, now)
    end
end

function Check:Stop()
    if self.layer ~= nil then
        self.exu.fps.ClearLayer(self.layer)
        self:Line("cleared layer '" .. self.layer .. "'")
    end
end

return M
