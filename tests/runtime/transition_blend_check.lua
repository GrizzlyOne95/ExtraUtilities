-- In-game check for the pilot FSM transition cross-fade
-- (exu.fps.SetTransitionBlend).
--
-- Turns the blend on and prints exu.fps.GetTransitionBlend() plus the local
-- pilot's animation index whenever the index changes, and a counter summary
-- every few seconds. Output lines are prefixed "[XFADE] ".
--
-- This file is not shipped. Copy it next to a test mission script, then:
--
--     local check = require("transition_blend_check").New(exu, { time = 0.15 })
--     function Update()
--         check:Update()
--     end
--
-- Hop out, then stand, run in each direction, strafe, crouch, snipe, jump and
-- land. Watch both views (first person, and an AI pilot or the third-person
-- body). check:Toggle() flips the blend on/off for an A/B comparison;
-- check:Stop() turns it off.
--
-- Options (second argument to New): any SetTransitionBlend option, plus
--   summarySeconds  seconds between counter summaries (default 5)
--   print, getTime  injectable (default the globals)
--
-- Lua 5.1; uses no io/os/debug.

local M = {}

local PREFIX = "[XFADE] "

local function fmt(value)
    if type(value) == "number" then
        if value == math.floor(value) and math.abs(value) < 1e9 then
            return string.format("%d", value)
        end
        return string.format("%.3f", value)
    end
    return tostring(value)
end

local Check = {}
Check.__index = Check

function M.New(exu, options)
    options = options or {}
    local self = setmetatable({}, Check)
    self.exu = exu
    self.print = options.print or print
    self.getTime = options.getTime or GetTime
    self.summarySeconds = options.summarySeconds or 5
    self.blend = {}
    for _, key in ipairs({ "time", "phaseCarry", "fp", "world", "death" }) do
        if options[key] ~= nil then
            self.blend[key] = options[key]
        end
    end
    self.on = false
    self.lastIndex = nil
    self.nextSummary = 0
    return self
end

function Check:Log(text)
    self.print(PREFIX .. text)
end

function Check:Dump(label)
    local b = self.exu.fps.GetTransitionBlend()
    self:Log(string.format(
        "%s enabled=%s time=%s phaseCarry=%s fp=%s world=%s death=%s available=%s faulted=%s transitions=%s phaseCarries=%s activeFades=%s evictions=%s",
        label or "state", fmt(b.enabled), fmt(b.time), fmt(b.phaseCarry), fmt(b.fp), fmt(b.world),
        fmt(b.death), fmt(b.available), fmt(b.faulted), fmt(b.transitions), fmt(b.phaseCarries),
        fmt(b.activeFades), fmt(b.evictions)))
end

function Check:Start()
    local caps = self.exu.fps.GetCapabilities()
    self:Log("capability transitionBlend=" .. fmt(caps.transitionBlend))
    local available = self.exu.fps.SetTransitionBlend(self.blend)
    self.on = true
    self:Log("SetTransitionBlend -> available=" .. fmt(available))
    self:Dump("start")
end

function Check:Toggle()
    if self.on then
        self.exu.fps.SetTransitionBlend(nil)
        self.on = false
        self:Log("blend OFF (stock hard cut)")
    else
        self.exu.fps.SetTransitionBlend(self.blend)
        self.on = true
        self:Log("blend ON")
    end
end

function Check:Stop()
    self.exu.fps.SetTransitionBlend(nil)
    self.on = false
    self:Dump("stop")
end

function Check:Update()
    if not self.started then
        self.started = true
        self:Start()
    end
    local state = self.exu.fps.GetPilotState()
    if state and state.available then
        if state.animationIndex ~= self.lastIndex then
            self:Log(string.format("index %s -> %s (%s)", fmt(self.lastIndex), fmt(state.animationIndex),
                fmt(state.animationName)))
            self.lastIndex = state.animationIndex
        end
    end
    local now = self.getTime and self.getTime() or 0
    if now >= self.nextSummary then
        self.nextSummary = now + self.summarySeconds
        self:Dump("summary")
    end
end

return M
