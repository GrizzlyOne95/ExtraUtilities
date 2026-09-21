--[[
    Weather.

    Two halves, and you only need the first one to get rain on screen.

    1. The shipped controller (exu_weather.lua) does the whole job: it creates
       the particle systems, fades between weather states, steers wind, and
       matches fog and light to the weather.

    2. Underneath it is the generic parameter bridge, which is what makes any
       of that possible. The second half of this file shows it directly, for
       when you want your own effect rather than one of the presets.

    Requires exu_weather.particle, exu_weather.material and the four
    exu_*.png textures to be present in an enabled mod - they ship with EXU.
--]]

-- ---------------------------------------------------------------------------
-- 1. Using the shipped controller
-- ---------------------------------------------------------------------------

-- `exu` is NOT a global in mission scope. Several older examples in this folder
-- omit this line and rely on something else having required it first; verified
-- in-game, a mission that does so sees `exu` as nil and every call below fails.
local exu = require("exu")
local Weather = require("exu_weather")

function Start()
    Weather.Init()

    -- Profiles: "clear", "rain_light", "rain_heavy", "dust", "mist".
    -- The second argument is how long the change takes, in seconds.
    Weather.SetProfile("rain_heavy", 10.0)

    -- Wind is a horizontal direction plus a speed. It eases in, so this is a
    -- storm arriving rather than a switch being flipped.
    Weather.SetWind(1.0, 0.3, 45.0)
end

function Update(dt)
    Weather.Update(dt)

    -- Intensity scales the current profile without changing which one it is,
    -- so this thins the same storm out rather than swapping to a lighter one.
    -- Weather.SetIntensity(0.4)
end

-- Call this from a mission-owned teardown/reload path if that mission has one.
-- Do not wire it to DeleteObject(handle): Redux invokes that callback whenever
-- any game object is deleted, which would turn the weather off mid-mission.
local function ShutdownWeather()
    Weather.Shutdown()
end

-- ---------------------------------------------------------------------------
-- 2. Driving particles directly
--
-- Ogre publishes a particle system's type-specific properties only through its
-- text parameter interface. exu.SetParticleEmitterParameter and
-- exu.SetParticleAffectorParameter are the way in; the typed setters
-- (SetParticleEmitterEmissionRate, SetParticleEmitterVelocity, ...) remain the
-- better choice for anything they already cover.
-- ---------------------------------------------------------------------------

local function CustomStorm()
    exu.CreateParticleSystem("my_storm", "EXU/WeatherDustLocal")
    exu.AttachParticleSystemToCamera("my_storm", SetVector(0, 20, 0))

    -- Ask the emitter what it is before assuming what it accepts. A Box
    -- publishes width/height/depth; a Ring adds inner_width/inner_height.
    print("emitter 0 is a " .. tostring(exu.GetParticleEmitterType("my_storm", 0)))

    -- ...or list everything it publishes, which beats guessing from docs.
    for _, name in ipairs(exu.GetParticleEmitterParameterNames("my_storm", 0) or {}) do
        print("  " .. name .. " = " .. tostring(exu.GetParticleEmitterParameter("my_storm", 0, name)))
    end

    -- Widen the emission volume. Numbers and booleans are converted for you.
    exu.SetParticleEmitterParameter("my_storm", 0, "width", 500)
    exu.SetParticleEmitterParameter("my_storm", 0, "depth", 500)

    -- Affectors are what animate a particle over its life. Walk them rather
    -- than hard-coding an index: editing the .particle template reorders them,
    -- and writing force_vector to a Scaler silently does nothing.
    for i = 0, (exu.GetParticleSystemAffectorCount("my_storm") or 0) - 1 do
        local kind = exu.GetParticleAffectorType("my_storm", i)

        if kind == "LinearForce" then
            -- Vectors and colours are space-separated text, exactly as they
            -- are spelled in a .particle script.
            exu.SetParticleAffectorParameter("my_storm", i, "force_vector", "20 0 -5")
            exu.SetParticleAffectorParameter("my_storm", i, "force_application", "add")

        elseif kind == "ColourInterpolator" then
            -- Fade in and back out, so particles never pop into existence.
            exu.SetParticleAffectorParameter("my_storm", i, "time0", 0.0)
            exu.SetParticleAffectorParameter("my_storm", i, "colour0", "1 1 1 0")
            exu.SetParticleAffectorParameter("my_storm", i, "time1", 0.45)
            exu.SetParticleAffectorParameter("my_storm", i, "colour1", "1 1 1 0.75")
            exu.SetParticleAffectorParameter("my_storm", i, "time2", 1.0)
            exu.SetParticleAffectorParameter("my_storm", i, "colour2", "1 1 1 0")

        elseif kind == "Scaler" then
            exu.SetParticleAffectorParameter("my_storm", i, "rate", 12)
        end
    end
end
