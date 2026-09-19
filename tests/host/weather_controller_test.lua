-- Host-side checks for Workshop/exu_weather.lua.
--
-- The controller only ever touches the game through the global `exu` table, so
-- a fake one is enough to exercise every decision it makes: which systems get
-- created, how intensity and wind are interpolated, what happens against an
-- older EXU that has no affector API, and whether teardown really restores
-- what Init captured.
--
-- This needs no game, no exu.dll and no Ogre. Run it with any Lua 5.1+.

local modulePath = os.getenv("EXU_WEATHER_PATH") or "Workshop/exu_weather.lua"

local failures = 0

local function fail(message)
    io.stderr:write("FAIL: " .. message .. "\n")
    failures = failures + 1
end

local function check(condition, message)
    if not condition then
        fail(message)
    end
end

local function checkNear(actual, expected, tolerance, message)
    if type(actual) ~= "number" or math.abs(actual - expected) > tolerance then
        fail(message .. " -- expected ~" .. tostring(expected) .. ", got " .. tostring(actual))
    end
end

-- ---------------------------------------------------------------------------
-- Fake exu
-- ---------------------------------------------------------------------------

local fake

-- `options.omit` names functions this build pretends not to have, which is how
-- the "older EXU" cases are set up.
local function makeFakeExu(options)
    options = options or {}
    local omit = {}
    for _, name in ipairs(options.omit or {}) do
        omit[name] = true
    end

    local self = {
        systems = {},         -- name -> { template, emitters, affectors, attached }
        calls = {},           -- ordered log of mutating calls
        destroyed = {},
        fog = { r = 0.1, g = 0.2, b = 0.3, start = 100, ending = 500 },
        ambient = { r = 0.4, g = 0.5, b = 0.6, a = 1.0 },
        sunDiffuse = { r = 0.7, g = 0.8, b = 0.9, a = 1.0 },
        followerTicks = 0,
        -- affector index -> type, per template
        affectorTypes = options.affectorTypes or {},
    }

    local function log(name, ...)
        self.calls[#self.calls + 1] = { name = name, args = { ... } }
    end

    local api = {}

    function api.GetFog()
        return { r = self.fog.r, g = self.fog.g, b = self.fog.b,
                 start = self.fog.start, ending = self.fog.ending }
    end

    function api.SetFog(r, g, b, start, ending)
        self.fog = { r = r, g = g, b = b, start = start, ending = ending }
        log("SetFog", r, g, b, start, ending)
    end

    function api.GetAmbientLight()
        return { r = self.ambient.r, g = self.ambient.g, b = self.ambient.b, a = self.ambient.a }
    end

    function api.SetAmbientLight(r, g, b)
        log("SetAmbientLight", r, g, b)
    end

    function api.GetSunDiffuse()
        return { r = self.sunDiffuse.r, g = self.sunDiffuse.g, b = self.sunDiffuse.b, a = self.sunDiffuse.a }
    end

    function api.SetSunDiffuse(r, g, b)
        log("SetSunDiffuse", r, g, b)
    end

    function api.CreateParticleSystem(name, template)
        if options.failTemplate == template then
            return false
        end
        self.systems[name] = {
            template = template,
            emitters = {},
            affectors = {},
            attached = false,
            timeout = nil,
        }
        log("CreateParticleSystem", name, template)
        return true
    end

    function api.DestroyParticleSystem(name)
        if self.systems[name] ~= nil then
            self.destroyed[#self.destroyed + 1] = name
            self.systems[name] = nil
            log("DestroyParticleSystem", name)
            return true
        end
        return false
    end

    function api.DetachParticleSystem(name)
        local system = self.systems[name]
        if system ~= nil then
            system.attached = false
            return true
        end
        return false
    end

    function api.AttachParticleSystemToCamera(name, offset)
        local system = self.systems[name]
        if system == nil then return false end
        system.attached = true
        system.offset = offset
        return true
    end

    function api.SetParticleSystemKeepLocalSpace(name, enabled)
        local system = self.systems[name]
        if system == nil then return false end
        system.keepLocalSpace = enabled
        return true
    end

    function api.SetParticleSystemNonVisibleUpdateTimeout(name, seconds)
        local system = self.systems[name]
        if system == nil then return false end
        system.timeout = seconds
        return true
    end

    function api.SetParticleEmitterEmissionRate(name, index, rate)
        local system = self.systems[name]
        if system == nil then return false end
        system.emitters[index] = system.emitters[index] or {}
        system.emitters[index].rate = rate
        return true
    end

    function api.SetParticleEmitterDirection(name, index, direction)
        local system = self.systems[name]
        if system == nil then return false end
        system.emitters[index] = system.emitters[index] or {}
        system.emitters[index].direction = direction
        return true
    end

    function api.GetParticleAffectorType(name, index)
        local system = self.systems[name]
        if system == nil then return nil end
        local byTemplate = self.affectorTypes[system.template]
        if byTemplate == nil then return nil end
        return byTemplate[index]
    end

    function api.SetParticleAffectorParameter(name, index, parameter, value)
        local system = self.systems[name]
        if system == nil then return false end
        system.affectors[index] = system.affectors[index] or {}
        system.affectors[index][parameter] = value
        log("SetParticleAffectorParameter", name, index, parameter, value)
        return true
    end

    function api.UpdateParticleFollowers()
        self.followerTicks = self.followerTicks + 1
        return 0
    end

    for name in pairs(omit) do
        api[name] = nil
    end

    self.api = api
    return self
end

-- The baseline templates declare a LinearForce first on rain and dust.
local STOCK_AFFECTORS = {
    ["EXU/WeatherRainLight"] = { [0] = "LinearForce" },
    ["EXU/WeatherRainHeavy"] = { [0] = "LinearForce" },
    ["EXU/WeatherDustLocal"] = { [0] = "LinearForce", [1] = "ColourInterpolator",
                                 [2] = "Scaler", [3] = "Rotator" },
    ["EXU/WeatherMist"]      = { [0] = "ColourInterpolator", [1] = "Scaler" },
}

-- Each test gets a clean module: the controller keeps state in an upvalue, so
-- reloading is how one test is isolated from the next.
local function loadWeather()
    package.loaded["exu_weather"] = nil
    local chunk, err = loadfile(modulePath)
    if chunk == nil then
        error("could not load " .. modulePath .. ": " .. tostring(err))
    end
    return chunk()
end

local function withFake(options, body)
    fake = makeFakeExu(options)
    _G.exu = fake.api
    local Weather = loadWeather()
    body(Weather, fake)
    _G.exu = nil
end

local function countSystems(f)
    local n = 0
    for _ in pairs(f.systems) do n = n + 1 end
    return n
end

-- ---------------------------------------------------------------------------
-- Tests
-- ---------------------------------------------------------------------------

local function TestInitCapturesBaseline()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        check(Weather.GetProfile() == "clear", "a fresh controller starts clear")

        -- Nothing should have been created or changed yet.
        check(countSystems(f) == 0, "Init creates no particle systems")
        check(#f.calls == 0, "Init does not write fog or light")
    end)
end

local function TestSetProfileCreatesSystems()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        check(Weather.SetProfile("rain_heavy", 4.0) == true, "a known profile is accepted")
        check(Weather.GetProfile() == "rain_heavy", "the profile name is recorded")

        check(countSystems(f) == 2, "rain_heavy creates a rain and a mist system")
        check(f.systems["exu_weather_rain"] ~= nil, "the rain system exists")
        check(f.systems["exu_weather_rain"].template == "EXU/WeatherRainHeavy",
            "the rain system uses the heavy template")
        check(f.systems["exu_weather_rain"].attached == true,
            "camera-follow systems are attached to the camera")
        check(f.systems["exu_weather_rain"].keepLocalSpace == false,
            "camera-follow systems emit into world space")
        check(f.systems["exu_weather_mist"].timeout == 4.0,
            "the non-visible timeout from the profile is applied")
    end)
end

local function TestUnknownProfileIsRejected()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        check(Weather.SetProfile("blizzard_of_oz") == false, "an unknown profile is rejected")
        check(Weather.GetProfile() == "clear", "a rejected profile does not change state")
        check(countSystems(f) == 0, "a rejected profile creates nothing")
    end)
end

local function TestIntensityRampsIn()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("rain_light", 4.0)

        -- rain_light emitter 0 is authored at 260/sec.
        Weather.Update(1.0)
        local quarter = f.systems["exu_weather_rain"].emitters[0].rate
        checkNear(quarter, 65.0, 0.001, "one second into a four second fade is a quarter rate")

        Weather.Update(1.0)
        checkNear(f.systems["exu_weather_rain"].emitters[0].rate, 130.0, 0.001,
            "two seconds in is half rate")

        Weather.Update(10.0)
        checkNear(f.systems["exu_weather_rain"].emitters[0].rate, 260.0, 0.001,
            "the ramp settles at the authored rate and does not overshoot")
    end)
end

local function TestIntensityScaleApplies()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("rain_light", 0.0)  -- 0 means snap to full
        Weather.SetIntensity(0.5)
        Weather.Update(0.016)

        checkNear(f.systems["exu_weather_rain"].emitters[0].rate, 130.0, 0.001,
            "SetIntensity scales the authored rate")
    end)
end

local function TestWindEasesAndReachesTheAffector()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init({ windTurnRate = 1.0 })
        Weather.SetProfile("rain_light", 0.0)
        Weather.SetWind(1.0, 0.0, 100.0)

        Weather.Update(0.5)
        local _, _, speed = Weather.GetWind()
        check(speed > 0.0 and speed < 100.0, "wind speed eases rather than snapping")

        local force = f.systems["exu_weather_rain"].affectors[0].force_vector
        check(type(force) == "string", "wind is written to the LinearForce affector as text")
        check(force:match("^[%-%d%.]+ [%-%d%.]+ [%-%d%.]+$") ~= nil,
            "force_vector is space-separated numbers, got: " .. tostring(force))

        -- Immediate is the escape hatch for a mission that wants a hard cut.
        Weather.SetWind(0.0, 1.0, 40.0, true)
        local x, z, snapped = Weather.GetWind()
        checkNear(snapped, 40.0, 0.001, "immediate wind skips the ease")
        checkNear(x, 0.0, 0.001, "immediate wind takes the new direction")
        checkNear(z, 1.0, 0.001, "immediate wind takes the new direction")
    end)
end

-- A template edit that reorders affectors must not silently write force_vector
-- into whatever now sits at that index.
local function TestWrongAffectorTypeIsRefused()
    local wrong = {}
    for k, v in pairs(STOCK_AFFECTORS) do wrong[k] = v end
    wrong["EXU/WeatherRainLight"] = { [0] = "Scaler" }

    withFake({ affectorTypes = wrong }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("rain_light", 0.0)
        Weather.SetWind(1.0, 0.0, 100.0, true)
        Weather.Update(0.016)

        check(f.systems["exu_weather_rain"].affectors[0] == nil,
            "wind is not written to an affector that is not a LinearForce")
    end)
end

-- An older EXU without the generic affector bridge must lose wind, not break.
local function TestOlderExuLosesWindOnly()
    withFake({
        affectorTypes = STOCK_AFFECTORS,
        omit = { "SetParticleAffectorParameter", "GetParticleAffectorType" },
    }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("rain_light", 0.0)
        Weather.SetWind(1.0, 0.0, 100.0, true)
        Weather.Update(0.016)

        check(f.systems["exu_weather_rain"] ~= nil, "the system is still created")
        checkNear(f.systems["exu_weather_rain"].emitters[0].rate, 260.0, 0.001,
            "emission still works without the affector API")
        check(next(f.systems["exu_weather_rain"].affectors) == nil,
            "no affector write is attempted")
    end)
end

-- A missing exu_weather.particle is the most likely real-world failure.
local function TestMissingTemplateDoesNotCrash()
    withFake({
        affectorTypes = STOCK_AFFECTORS,
        failTemplate = "EXU/WeatherRainLight",
    }, function(Weather, f)
        Weather.Init()
        check(Weather.SetProfile("rain_light", 0.0) == true,
            "SetProfile still succeeds so the mission keeps running")
        Weather.Update(0.016)
        check(countSystems(f) == 0, "no system exists when the template is missing")
    end)
end

local function TestUpdateDrivesFollowers()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("dust", 0.0)
        Weather.Update(0.016)
        Weather.Update(0.016)
        check(f.followerTicks == 2, "camera followers are ticked once per Update")
    end)
end

local function TestUpdateBeforeInitIsSafe()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Update(0.016)
        check(f.followerTicks == 0, "Update before Init does nothing")
    end)
end

local function TestBadDeltaIsIgnored()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("rain_light", 4.0)
        Weather.Update(-1.0)
        Weather.Update(0.0 / 0.0)  -- NaN
        check(f.followerTicks == 0, "a negative or NaN delta is refused")
    end)
end

local function TestShutdownRestoresBaseline()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        local originalFog = f.fog
        Weather.Init()
        Weather.SetProfile("rain_heavy", 0.0)
        Weather.Update(0.5)

        check(f.fog.start ~= originalFog.start, "weather changed the fog")

        Weather.Shutdown()
        check(countSystems(f) == 0, "shutdown destroys every system it created")
        checkNear(f.fog.r, originalFog.r, 1e-9, "shutdown restores fog colour")
        checkNear(f.fog.start, originalFog.start, 1e-9, "shutdown restores fog start")
        checkNear(f.fog.ending, originalFog.ending, 1e-9, "shutdown restores fog end")
        check(Weather.GetProfile() == "clear", "shutdown returns to clear")
    end)
end

-- Two machines running the same mission must see the same dust, without
-- anything being synchronised across the wire.
local function TestPulseIsDeterministic()
    local rates = {}
    for run = 1, 2 do
        withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
            Weather.Init()
            Weather.SetProfile("dust", 0.0)
            for _ = 1, 40 do
                Weather.Update(0.1)
            end
            rates[run] = f.systems["exu_weather_dust"].emitters[0].rate
        end)
    end
    checkNear(rates[1], rates[2], 1e-12, "the emission pulse is deterministic across runs")

    -- And it must actually be doing something.
    check(math.abs(rates[1] - 46.0) > 0.01, "the dust profile really does pulse its emission")
end

local function TestProfileSwitchReplacesSystems()
    withFake({ affectorTypes = STOCK_AFFECTORS }, function(Weather, f)
        Weather.Init()
        Weather.SetProfile("rain_heavy", 0.0)
        check(countSystems(f) == 2, "rain_heavy has two systems")

        Weather.SetProfile("dust", 0.0)
        check(countSystems(f) == 1, "switching profiles drops the old systems")
        check(f.systems["exu_weather_dust"] ~= nil, "the new profile's system exists")
        check(f.systems["exu_weather_rain"] == nil, "the old profile's system is gone")
    end)
end

TestInitCapturesBaseline()
TestSetProfileCreatesSystems()
TestUnknownProfileIsRejected()
TestIntensityRampsIn()
TestIntensityScaleApplies()
TestWindEasesAndReachesTheAffector()
TestWrongAffectorTypeIsRefused()
TestOlderExuLosesWindOnly()
TestMissingTemplateDoesNotCrash()
TestUpdateDrivesFollowers()
TestUpdateBeforeInitIsSafe()
TestBadDeltaIsIgnored()
TestShutdownRestoresBaseline()
TestPulseIsDeterministic()
TestProfileSwitchReplacesSystems()

if failures > 0 then
    io.stderr:write(failures .. " weather controller check(s) failed\n")
    os.exit(1)
end

print("All weather controller checks passed.")
