-- Host-side checks for tests/runtime/pilot_fsm_capture.lua.
--
-- The capture script only touches the game through the exu table it is given,
-- so a fake one can walk it through a whole session: in a vehicle, hopping
-- out, several crouch cycles, and the final dump. Needs no game; any Lua 5.1+.

local failures = 0

local function check(condition, message)
    if not condition then
        io.stderr:write("FAIL: " .. message .. "\n")
        failures = failures + 1
    end
end

local capture = dofile("tests/runtime/pilot_fsm_capture.lua")

-- ---- Fake game ----------------------------------------------------------

local onFoot = false
local now = 100
local calls = { start = nil, stop = 0 }
local dwell = {
    standing = { nativeState = 0, count = 0 },
    enteringCrouch = { nativeState = 1, count = 0 },
    crouched = { nativeState = 2, count = 0 },
    exitingCrouch = { nativeState = 3, count = 0 },
}
local localCalls = 0
local traceTime = 0

local function completeVisit(name, seconds)
    local d = dwell[name]
    d.count = d.count + 1
    d.last = seconds
    d.min = d.min and math.min(d.min, seconds) or seconds
    d.max = d.max and math.max(d.max, seconds) or seconds
    d.total = (d.total or 0) + seconds
    d.mean = d.total / d.count
    d.lastCalls = 12
end

local exu = {
    fps = {
        GetCapabilities = function()
            return { localFirstPersonTarget = true, pilotFsmIntercept = true,
                pilotAnimationOverrides = false, nativeAdvancement = "unvalidated",
                firstPersonStatus = "native" }
        end,
        GetPilotInterceptStatus = function()
            return { installed = true, active = true, observeOnly = true, calls = 5, localCalls = localCalls }
        end,
        IsAvailable = function() return onFoot end,
        ListAnimations = function()
            if not onFoot then
                return nil
            end
            return {
                { name = "idle", length = 2, enabled = true, loop = true, timePosition = 0.5 },
                { name = "stand2Kneel", length = 0.4, enabled = false, loop = false, timePosition = 0 },
            }
        end,
        GetInfo = function(name)
            if name == "stand2Kneel" then return { length = 0.4 } end
            if name == "kneel2stand" then return { length = 0.6 } end
            return nil
        end,
        GetPilotState = function()
            if onFoot then
                return { available = true }
            end
            return nil
        end,
        StartPilotTrace = function(options)
            calls.start = options
            return true
        end,
        StopPilotTrace = function()
            calls.stop = calls.stop + 1
        end,
        GetPilotTrace = function()
            return {
                enabled = calls.stop == 0,
                localCalls = localCalls,
                recorded = 3,
                time = traceTime,
                dwell = dwell,
                samples = {
                    { call = 1, dt = 1 / 60, time = 0.1, beforeNativeState = 0, afterNativeState = 1,
                        beforeAnimationIndex = 2, afterAnimationIndex = 0,
                        beforeAnimationHandle = -1, afterAnimationHandle = 7 },
                },
            }
        end,
    },
}

local lines = {}
local session = capture.New(exu, {
    cycles = 2,
    print = function(text) lines[#lines + 1] = text end,
    getTime = function() return now end,
})

local function frame()
    now = now + 0.1
    if onFoot then
        localCalls = localCalls + 6
        traceTime = traceTime + 0.1
    end
    session:Update()
end

local function has(pattern)
    for _, line in ipairs(lines) do
        if string.find(line, pattern, 1, true) then
            return true
        end
    end
    return false
end

-- ---- Session --------------------------------------------------------------

frame()
check(calls.start ~= nil and calls.start.changesOnly == true, "trace started with changesOnly")
check(has("Capabilities (start)"), "start capabilities reported")
check(has("`ListAnimations()`: nil"), "in-vehicle ListAnimations nil reported")

frame()
frame()
check(not has("On foot."), "no on-foot report while in a vehicle")

onFoot = true
frame()
check(has("On foot."), "on-foot instructions printed")
check(has("| stand2Kneel | 0.4000 |"), "on-foot animation table printed")

completeVisit("enteringCrouch", 0.4)
frame()
check(has("progress: entering 1, exiting 0 of 2"), "progress reported")
completeVisit("exitingCrouch", 0.6)
completeVisit("enteringCrouch", 0.4)
frame()
check(calls.stop == 0 and not has("DONE"), "not done until both transitions reach the cycle count")
completeVisit("exitingCrouch", 0.9)
frame()

check(calls.stop == 1, "trace stopped once when done")
check(has("DONE"), "DONE printed")
check(has("| enteringCrouch | 2 | 0.4000 | 0.4000 | 0.4000 | 12 | stand2Kneel | 0.4000 | 0 |"),
    "entering dwell row compares with stand2Kneel")
check(has("| exitingCrouch | 2 | 0.6000 | 0.7500 | 0.9000 | 12 | kneel2stand | 0.6000 | 0.1500 |"),
    "exiting dwell row compares with kneel2stand")
check(has("hint enteringCrouch: mean within one step"), "matching clip hinted as within")
check(has("hint exitingCrouch: mean NOT within one step"), "mismatching clip hinted as not within")
check(has("local Simulate calls per Lua `Update`: 6"), "calls-per-update ratio reported")
check(has("| 1 | 0.0167 | 0.1000 | 0 | 1 | 2 | 0 | -1 | 7 |"), "sample row printed")

local count = #lines
frame()
check(#lines == count and calls.stop == 1, "nothing more happens after DONE")

for _, line in ipairs(lines) do
    check(string.sub(line, 1, 11) == "[PILOTCAP] ", "every line carries the prefix: " .. line)
end

if failures > 0 then
    io.stderr:write(failures .. " pilot FSM capture check(s) failed\n")
    os.exit(1)
end
print("All pilot FSM capture checks passed.")
