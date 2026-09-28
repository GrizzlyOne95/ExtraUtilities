-- In-game capture for the pilot animation FSM (handoff steps 0 and 5a).
--
-- Measures, without writing anything to the game, how long the stock crouch
-- transitions (native states 1 and 3) last and compares that with the
-- stand2Kneel / kneel2stand clip lengths. It also records the step 0
-- qualification facts. Output is Markdown, prefixed "[PILOTCAP] ", ready to
-- paste into a dated Docs/Research/ note.
--
-- This file is not shipped. Copy it next to a test mission's script, then,
-- on a qualified Redux build:
--
--     local capture = require("pilot_fsm_capture").New(exu)
--     function Update()
--         capture:Update()
--     end
--
-- Then, in game:
--   1. Start in a vehicle, then hop out (the script records both).
--   2. Select a sniper weapon, wait until fully crouched, deselect it, and
--      wait until standing. Repeat until the script prints "DONE"
--      (default: 5 complete cycles). Stay on foot throughout.
--   3. Copy the [PILOTCAP] lines from the log.
-- capture:Dump() prints the current results at any time.
--
-- Options (second argument to New): cycles (default 5), sampleLimit
-- (default 64), print and getTime (default the globals; injectable for
-- host tests).
--
-- Lua 5.1; uses no io/os/debug.

local M = {}

local PREFIX = "[PILOTCAP] "

local function fmt(value)
    if type(value) == "number" then
        if value == math.floor(value) and math.abs(value) < 1e9 then
            return string.format("%d", value)
        end
        return string.format("%.4f", value)
    end
    if value == nil then
        return "-"
    end
    return tostring(value)
end

local Capture = {}
Capture.__index = Capture

function M.New(exu, options)
    options = options or {}
    local self = setmetatable({}, Capture)
    self.exu = exu
    self.cycles = options.cycles or 5
    self.sampleLimit = options.sampleLimit or 64
    self.print = options.print or print
    self.getTime = options.getTime or GetTime
    self.phase = "boot"
    self.onFootUpdates = 0
    self.lastProgress = ""
    return self
end

function Capture:Line(text)
    self.print(PREFIX .. text)
end

function Capture:ReportCapabilities(heading)
    local fps = self.exu.fps
    local caps = fps.GetCapabilities()
    self:Line("")
    self:Line("### " .. heading)
    self:Line("")
    if type(caps) ~= "table" then
        self:Line("- capabilities: unavailable")
        return
    end
    local keys = {
        "localFirstPersonTarget", "animationInventory", "pilotStateInspection",
        "pilotFsmIntercept", "pilotAnimationOverrides", "managedClock",
        "nativeAdvancement", "firstPersonStatus",
    }
    for _, key in ipairs(keys) do
        self:Line("- `" .. key .. "`: " .. fmt(caps[key]))
    end
end

function Capture:ReportIntercept(heading)
    local hook = self.exu.fps.GetPilotInterceptStatus()
    self:Line("")
    self:Line("### " .. heading)
    self:Line("")
    local keys = {
        "installed", "active", "observeOnly", "calls", "localCalls",
        "stateChanges", "animationChanges", "policyDecision",
        "beforeState", "afterState",
    }
    for _, key in ipairs(keys) do
        self:Line("- `" .. key .. "`: " .. fmt(hook[key]))
    end
end

function Capture:ReportAnimations(heading)
    local fps = self.exu.fps
    self:Line("")
    self:Line("### " .. heading)
    self:Line("")
    self:Line("- `IsAvailable()`: " .. fmt(fps.IsAvailable()))
    local states = fps.ListAnimations()
    if states == nil then
        self:Line("- `ListAnimations()`: nil (target unavailable, or the Ogre iterator export is missing)")
        return
    end
    self:Line("- `ListAnimations()`: " .. #states .. " states")
    self:Line("")
    self:Line("| name | length | enabled | loop | time |")
    self:Line("| --- | ---: | --- | --- | ---: |")
    for _, state in ipairs(states) do
        self:Line("| " .. fmt(state.name) .. " | " .. fmt(state.length) .. " | " ..
            fmt(state.enabled) .. " | " .. fmt(state.loop) .. " | " .. fmt(state.timePosition) .. " |")
    end
end

function Capture:ClipLength(name)
    local info = self.exu.fps.GetInfo(name)
    if type(info) == "table" then
        return info.length
    end
    return nil
end

function Capture:Update()
    local fps = self.exu.fps

    if self.phase == "boot" then
        self:Line("## Pilot FSM capture")
        self:ReportCapabilities("Capabilities (start)")
        self:ReportIntercept("Intercept status (start)")
        self:ReportAnimations("First-person animations (start)")
        self.seamActive = fps.StartPilotTrace({ changesOnly = true })
        self:Line("")
        self:Line("- `StartPilotTrace` seam active: " .. fmt(self.seamActive))
        self.phase = "waitOnFoot"
        self:Line("")
        self:Line("Waiting for the player to be on foot...")
        return
    end

    if self.phase == "done" then
        return
    end

    local pilot = fps.GetPilotState()
    if pilot == nil then
        return
    end

    if self.phase == "waitOnFoot" then
        self.phase = "cycling"
        self.onFootTime = self.getTime()
        local trace = fps.GetPilotTrace(1)
        self.onFootTraceTime = trace and trace.time or 0
        self.onFootLocalCalls = trace and trace.localCalls or 0
        self:ReportAnimations("First-person animations (on foot)")
        self:ReportIntercept("Intercept status (on foot)")
        self:Line("")
        self:Line("On foot. Select a sniper weapon, wait until fully crouched, deselect, wait until standing. Repeat " ..
            self.cycles .. " times.")
        return
    end

    self.onFootUpdates = self.onFootUpdates + 1

    local trace = fps.GetPilotTrace(1)
    if trace == nil then
        return
    end
    local entering = trace.dwell.enteringCrouch.count
    local exiting = trace.dwell.exitingCrouch.count
    local progress = entering .. "/" .. exiting
    if progress ~= self.lastProgress then
        self.lastProgress = progress
        self:Line("progress: entering " .. entering .. ", exiting " .. exiting .. " of " .. self.cycles)
    end

    if entering >= self.cycles and exiting >= self.cycles then
        fps.StopPilotTrace()
        self:Dump()
        self.phase = "done"
        self:Line("DONE")
    end
end

function Capture:Dump()
    local fps = self.exu.fps
    local trace = fps.GetPilotTrace(self.sampleLimit)
    self:Line("")
    self:Line("### Timing trace")
    self:Line("")
    if trace == nil then
        self:Line("- `GetPilotTrace`: nil (torn read; call Dump again)")
        return
    end

    self:Line("- trace `localCalls`: " .. fmt(trace.localCalls) ..
        ", samples recorded: " .. fmt(trace.recorded) .. ", `time`: " .. fmt(trace.time))

    if self.onFootTime ~= nil then
        local luaElapsed = self.getTime() - self.onFootTime
        local traceElapsed = trace.time - self.onFootTraceTime
        local calls = trace.localCalls - self.onFootLocalCalls
        self:Line("- since on foot: `GetTime()` elapsed " .. fmt(luaElapsed) ..
            ", trace dt elapsed " .. fmt(traceElapsed) ..
            ", local Simulate calls " .. fmt(calls) ..
            ", Lua `Update` calls " .. fmt(self.onFootUpdates))
        if self.onFootUpdates > 0 then
            self:Line("- local Simulate calls per Lua `Update`: " .. fmt(calls / self.onFootUpdates))
        end
    end

    local clips = {
        enteringCrouch = { name = "stand2Kneel", length = self:ClipLength("stand2Kneel") },
        exitingCrouch = { name = "kneel2stand", length = self:ClipLength("kneel2stand") },
    }

    self:Line("")
    self:Line("| state | visits | min | mean | max | last calls | clip | clip length | mean - clip |")
    self:Line("| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: |")
    local order = { "standing", "enteringCrouch", "crouched", "exitingCrouch" }
    for _, name in ipairs(order) do
        local d = trace.dwell[name]
        local clip = clips[name]
        local clipName, clipLength, delta = "-", nil, nil
        if clip ~= nil then
            clipName = clip.name
            clipLength = clip.length
            if clipLength ~= nil and d.count > 0 then
                delta = d.mean - clipLength
            end
        end
        self:Line("| " .. name .. " | " .. fmt(d.count) .. " | " .. fmt(d.min) .. " | " .. fmt(d.mean) ..
            " | " .. fmt(d.max) .. " | " .. fmt(d.lastCalls) .. " | " .. clipName ..
            " | " .. fmt(clipLength) .. " | " .. fmt(delta) .. " |")
    end

    -- A hint for the note, not a conclusion: dwell is exact to one step, so
    -- compare against the largest dt observed.
    local maxDt = 0
    for _, s in ipairs(trace.samples) do
        if type(s.dt) == "number" and s.dt == s.dt and s.dt > maxDt then
            maxDt = s.dt
        end
    end
    self:Line("")
    self:Line("- largest sampled dt: " .. fmt(maxDt))
    for _, name in ipairs({ "enteringCrouch", "exitingCrouch" }) do
        local d = trace.dwell[name]
        local clip = clips[name]
        if d.count == 0 then
            self:Line("- hint " .. name .. ": no complete visits")
        elseif clip.length == nil then
            self:Line("- hint " .. name .. ": clip " .. clip.name .. " length unavailable")
        else
            local spread = d.max - d.min
            local within = math.abs(d.mean - clip.length) <= maxDt
            self:Line("- hint " .. name .. ": mean " .. (within and "within" or "NOT within") ..
                " one step of " .. clip.name .. " length; spread " .. fmt(spread))
        end
    end

    self:Line("")
    self:Line("| call | dt | time | before state | after state | before anim | after anim | before handle | after handle |")
    self:Line("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for _, s in ipairs(trace.samples) do
        self:Line("| " .. fmt(s.call) .. " | " .. fmt(s.dt) .. " | " .. fmt(s.time) ..
            " | " .. fmt(s.beforeNativeState) .. " | " .. fmt(s.afterNativeState) ..
            " | " .. fmt(s.beforeAnimationIndex) .. " | " .. fmt(s.afterAnimationIndex) ..
            " | " .. fmt(s.beforeAnimationHandle) .. " | " .. fmt(s.afterAnimationHandle) .. " |")
    end
end

return M
