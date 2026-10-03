-- In-game check for native pilot animation overrides (roadmap 3B / 6 / 7).
--
-- Sets a pilot animation profile, records the crouch transitions with the
-- read-only pilot trace, and compares the measured native state 1 / 3 dwell
-- with what the profile asks for. Output lines are prefixed "[PILOTOVR] ".
--
-- This file is not shipped. In ISDFC's Test Range (or any single-player test
-- mission) copy it next to the mission script, then:
--
--     local check = require("pilot_override_check").New(exu)
--     function Update()
--         check:Update()
--     end
--
-- In game: hop out, select a sniper weapon, wait until fully crouched,
-- deselect it, wait until standing; repeat until "DONE" (default 3 cycles).
-- check:Dump() prints the current results at any time; check:Restore() puts
-- the stock profile back (mission end does that anyway).
--
-- Default profile: enterCrouch completes in exactly 1.0 s (expect a state-1
-- dwell of about 1.0 s instead of the stock 1.934 s); exitCrouch completes
-- after one play of kneel2stand at authored speed (expect about its clip
-- length). Options (second argument to New):
--   enterDuration  seconds for enterCrouch (default 1.0)
--   exitCompletion "stock" | "animation" | "manual" (default "animation";
--                  "manual" calls CompleteTransition after manualHold s)
--   manualHold     seconds to hold a manual exit before completing (default 0.5)
--   substitute     optional { slot = "crouched", animation = "someClip" };
--                  the clip must exist on BOTH the world and first-person
--                  pilot meshes, or EXU keeps that slot stock and logs it
--   cycles         complete enter/exit cycles before DONE (default 3)
--   print, getTime injectable (default the globals)
--
-- Lua 5.1; uses no io/os/debug.

local M = {}

local PREFIX = "[PILOTOVR] "

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

local Check = {}
Check.__index = Check

function M.New(exu, options)
    options = options or {}
    local self = setmetatable({}, Check)
    self.exu = exu
    self.enterDuration = options.enterDuration or 1.0
    self.exitCompletion = options.exitCompletion or "animation"
    self.manualHold = options.manualHold or 0.5
    self.substitute = options.substitute
    self.cycles = options.cycles or 3
    self.print = options.print or print
    self.getTime = options.getTime or GetTime
    self.phase = "boot"
    self.lastProgress = ""
    return self
end

function Check:Line(text)
    self.print(PREFIX .. text)
end

function Check:Profile()
    local profile = {
        enterCrouch = { completion = "duration", duration = self.enterDuration },
        exitCrouch = { completion = self.exitCompletion },
    }
    local sub = self.substitute
    if type(sub) == "table" and sub.slot and sub.animation then
        profile[sub.slot] = profile[sub.slot] or {}
        profile[sub.slot].mode = "substitute"
        profile[sub.slot].animation = sub.animation
    end
    return profile
end

function Check:Restore()
    pcall(self.exu.fps.SetPilotAnimationProfile, nil)
end

function Check:Update()
    local fps = self.exu.fps

    if self.phase == "boot" then
        self:Line("## Pilot animation override check")
        local caps = fps.GetCapabilities()
        self:Line("- pilotAnimationOverrides: " .. fmt(caps and caps.pilotAnimationOverrides))
        if not (caps and caps.pilotAnimationOverrides) then
            self:Line("overrides unavailable in this session (see exu.log); nothing to check")
            self.phase = "done"
            return
        end
        local ok, err = pcall(fps.SetPilotAnimationProfile, self:Profile())
        if not ok then
            self:Line("SetPilotAnimationProfile failed: " .. tostring(err))
            self.phase = "done"
            return
        end
        local profile = fps.GetPilotAnimationProfile()
        for _, slot in ipairs({ "stand", "enterCrouch", "crouched", "exitCrouch", "jump", "land" }) do
            local p = profile[slot]
            self:Line("- " .. slot .. ": mode " .. fmt(p.mode) .. ", animation " .. fmt(p.animation) ..
                ", completion " .. fmt(p.completion) .. ", duration " .. fmt(p.duration))
        end
        fps.StartPilotTrace({ changesOnly = true })
        self.phase = "cycling"
        self:Line("Hop out, then crouch with a sniper weapon and stand up " .. self.cycles .. " times.")
        return
    end

    if self.phase == "done" then
        return
    end

    local pilot = fps.GetPilotState()
    if pilot == nil then
        return
    end

    -- Manual exit: complete it after a short hold so the cycle can finish.
    if self.exitCompletion == "manual" then
        if pilot.nativeState == 3 then
            self.exitStarted = self.exitStarted or self.getTime()
            if self.getTime() - self.exitStarted >= self.manualHold and not self.exitCompleted then
                self.exitCompleted = fps.CompleteTransition()
                self:Line("CompleteTransition -> " .. fmt(self.exitCompleted))
            end
        else
            self.exitStarted = nil
            self.exitCompleted = nil
        end
    end

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

function Check:Dump()
    local fps = self.exu.fps
    local trace = fps.GetPilotTrace(1)
    if trace == nil then
        self:Line("GetPilotTrace: nil (torn read; call Dump again)")
        return
    end

    local function clipLength(name)
        local info = fps.GetInfo(name)
        return type(info) == "table" and info.length or nil
    end

    local expectedExit = nil
    if self.exitCompletion == "animation" then
        expectedExit = clipLength("kneel2stand")
    elseif self.exitCompletion == "stock" then
        expectedExit = 0.967 / 0.5
    elseif self.exitCompletion == "manual" then
        expectedExit = nil -- clip time at stock speed + manualHold; not a fixed number
    end

    local status = fps.GetPilotInterceptStatus()
    self:Line("- overrideCalls " .. fmt(status.overrideCalls) .. ", observeOnly " .. fmt(status.observeOnly) ..
        ", policyDecision " .. fmt(status.policyDecision))
    self:Line("")
    self:Line("| state | visits | min | mean | max | expected |")
    self:Line("| --- | ---: | ---: | ---: | ---: | ---: |")
    local rows = {
        { "enteringCrouch", self.enterDuration },
        { "exitingCrouch", expectedExit },
    }
    for _, row in ipairs(rows) do
        local d = trace.dwell[row[1]]
        self:Line("| " .. row[1] .. " | " .. fmt(d.count) .. " | " .. fmt(d.min) .. " | " .. fmt(d.mean) ..
            " | " .. fmt(d.max) .. " | " .. fmt(row[2]) .. " |")
    end
    self:Line("(dwell is exact to one simulation step; stock is about 1.934 s for both)")
end

return M
