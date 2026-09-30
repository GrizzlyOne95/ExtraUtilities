-- Attach as a Lua mission script in a disposable test mission. Use tracks
-- present in the install and an absent track for MISS. Run with EXU alone,
-- then with OpenShim. The host interpreter only syntax-checks this fixture.
local exu = require("exu")
local clock, nextStep = 0, 1
local function mark(text)
    print("[MUSICCHK] " .. text)
end
local function snapshot(key)
    local s = exu.GetMusicState()
    if not s then
        mark(key .. " state=nil")
        return
    end
    mark(string.format("%s track=%d playing=%s paused=%s gain=%.3f fading=%s options=%d",
        key, s.track, tostring(s.playing), tostring(s.paused), s.gain,
        tostring(s.fading), s.userVolume))
end
local steps = {
    {1,  "PLAY07",  function() return exu.PlayMusic(7) end},
    {3,  "SAME07",  function() return exu.SetMusicTrack(7) end},
    {5,  "PAUSE",   exu.PauseMusic},
    {7,  "PAUSE2",  exu.PauseMusic},
    {9,  "RESUME",  exu.ResumeMusic},
    {11, "RESUME2", exu.ResumeMusic},
    {13, "STOP",    exu.StopMusic},
    {15, "STOP2",   exu.StopMusic},
    {17, "RESSTOP", exu.ResumeMusic},
    {19, "SET07",   function() return exu.SetMusicTrack(7) end},
    {21, "FADEOUT", function() return exu.FadeMusic(0, 4) end},
    {23, "FREEZE",  exu.PauseMusic},
    {27, "CONTINUE",exu.ResumeMusic},
    {31, "FADEIN",  function() return exu.FadeMusic(1, 2) end},
    {35, "CHANGE12",function() return exu.ChangeMusicTrack(12, 2, 3) end},
    {43, "LOW",     function() return exu.FadeMusic(0.3, 1) end},
    {45, "RESET",   exu.ResetMusic},
    {47, "MISS98",  function() return exu.ChangeMusicTrack(98, 2, 2) end},
}

function Start()
    clock, nextStep = 0, 1
    mark("START: confirm 07.ogg/12.ogg exist and 98.ogg is absent")
end

function Update(dt)
    if not exu.UpdateMusic(dt) then mark("UPDATE failed") end
    clock = clock + dt
    while steps[nextStep] and clock >= steps[nextStep][1] do
        local step = steps[nextStep]
        local ok, result = pcall(step[3])
        mark(step[2] .. " ok=" .. tostring(ok) .. " result=" .. tostring(result)
            .. " selected=" .. tostring(exu.GetMusicTrack()))
        snapshot(step[2])
        nextStep = nextStep + 1
    end
end
