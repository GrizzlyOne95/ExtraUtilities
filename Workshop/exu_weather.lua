-- exu_weather.lua
--
-- A reusable weather controller built on the Extra Utilities particle API.
--
--   local Weather = require("exu_weather")
--   Weather.Init()
--   Weather.SetProfile("rain_heavy", 8.0)
--   -- once per frame:
--   Weather.Update(dt)
--
-- Scope: this drives the baseline templates in exu_weather.particle - creating
-- and destroying the systems, interpolating between weather states, steering
-- wind, and matching fog and light to the current state. It is deliberately
-- smaller than a campaign weather director: there is no scheduling, no
-- per-mission scripting and no random weather. A campaign that wants those
-- should own them and call SetProfile.
--
-- Everything here works on ordinary Redux rendering. Nothing in this file
-- requires OpenShim, an Enhanced render profile, or DX11.
--
-- MODULE NAME: on a Workshop install Redux's fallback asset searcher truncates
-- module names to 11 characters, so this file must keep an 11-character stem.
-- "exu_weather" is exactly 11. Renaming it longer makes require() fail with a
-- confusing "no asset" error.

local Weather = {}

Weather.VERSION = "1.0"

-- ---------------------------------------------------------------------------
-- Guarded access to exu
--
-- Every entry point degrades instead of erroring: a mission that loses EXU, or
-- runs against an older build without the affector API, keeps running with
-- whatever subset is present.
-- ---------------------------------------------------------------------------

local function Has(name)
    return type(exu) == "table" and type(exu[name]) == "function"
end

local function Call(name, ...)
    if not Has(name) then
        return nil
    end
    local ok, result = pcall(exu[name], ...)
    if not ok then
        return nil
    end
    return result
end

local function Clamp(value, low, high)
    if value < low then return low end
    if value > high then return high end
    return value
end

local function Clamp01(value)
    return Clamp(value, 0.0, 1.0)
end

local function Lerp(a, b, t)
    return a + (b - a) * t
end

local function Normalize(x, y, z)
    local length = math.sqrt(x * x + y * y + z * z)
    if length < 1e-6 then
        return 0.0, 0.0, 0.0
    end
    return x / length, y / length, z / length
end

-- Ogre reads vectors and colours out of space-separated text, so this is the
-- format every generic affector parameter is written in.
local function Vec3Text(x, y, z)
    return string.format("%.4f %.4f %.4f", x, y, z)
end

-- ---------------------------------------------------------------------------
-- Profiles
--
-- A profile is what a weather state looks like AT FULL INTENSITY. Everything
-- scales from there, so SetIntensity(0.5) is a half-strength version of the
-- same weather rather than a different one.
--
-- `emitters` is keyed by the zero-based emitter index in the .particle
-- template. Those indices are positional: see the index map above each block
-- in exu_weather.particle before changing them.
-- ---------------------------------------------------------------------------

Weather.Profiles = {
    clear = {
        systems = {},
        fog = nil,
        ambientScale = 1.0,
    },

    rain_light = {
        systems = {
            {
                key = "rain",
                template = "EXU/WeatherRainLight",
                follow = "camera",
                -- Attach height is load-bearing, and not for the reason it
                -- looks. A drop lives 1.2-1.9s at 40-54/s, so it falls 48-102
                -- units and then dies. Spawn the volume much above that and
                -- most of the rain expires overhead, out of frame: measured
                -- in-game, offset 70 gave ~2 visible streaks where offset 20
                -- gave ~8-10 at an identical emission rate. Keep this within
                -- roughly one drop-lifetime of eye level.
                offset = { 0, 18, 0 },
                nonVisibleTimeout = 2.0,
                -- affector 0 is the LinearForce the template reserves for wind
                windAffector = 0,
                windScale = 0.45,
                emitters = {
                    [0] = { rate = 260 },
                },
            },
        },
        fog = { r = 0.50, g = 0.54, b = 0.58, start = 90, ending = 420 },
        ambientScale = 0.86,
    },

    rain_heavy = {
        systems = {
            {
                key = "rain",
                template = "EXU/WeatherRainHeavy",
                follow = "camera",
                -- See the note on rain_light: heavy rain is shorter lived
                -- (0.7-1.2s at 62-88/s, so 43-105 units of fall), so it needs
                -- to start lower still, not higher.
                offset = { 0, 22, 0 },
                nonVisibleTimeout = 2.0,
                windAffector = 0,
                windScale = 0.8,
                emitters = {
                    [0] = { rate = 900 },
                },
            },
            -- A thin mist layer under heavy rain is what sells the air being
            -- full of water rather than just the ground being wet.
            {
                key = "mist",
                template = "EXU/WeatherMist",
                follow = "camera",
                offset = { 0, 10, 0 },
                nonVisibleTimeout = 4.0,
                emitters = {
                    [0] = { rate = 12 },
                },
            },
        },
        fog = { r = 0.44, g = 0.48, b = 0.53, start = 45, ending = 260 },
        ambientScale = 0.70,
    },

    dust = {
        systems = {
            {
                key = "dust",
                template = "EXU/WeatherDustLocal",
                follow = "camera",
                offset = { 0, 20, 0 },
                nonVisibleTimeout = 3.0,
                windAffector = 0,
                windScale = 0.25,
                emitters = {
                    [0] = { rate = 46 },
                },
                -- Emission breathes rather than sitting flat, which is what
                -- makes a dust layer read as weather instead of as a filter.
                pulse = { amplitude = 0.35, frequency = 0.12 },
            },
        },
        fog = { r = 0.62, g = 0.46, b = 0.30, start = 70, ending = 330 },
        ambientScale = 0.82,
    },

    mist = {
        systems = {
            {
                key = "mist",
                template = "EXU/WeatherMist",
                follow = "camera",
                offset = { 0, 14, 0 },
                nonVisibleTimeout = 4.0,
                emitters = {
                    [0] = { rate = 12 },
                },
            },
        },
        fog = { r = 0.66, g = 0.69, b = 0.72, start = 55, ending = 300 },
        ambientScale = 0.92,
    },
}

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------

local state = {
    initialized = false,
    prefix = "exu_weather_",

    profileName = "clear",
    profile = Weather.Profiles.clear,

    -- Intensity is interpolated rather than snapped so a weather change is a
    -- transition the player watches, not a cut.
    intensity = 0.0,
    targetIntensity = 0.0,
    transitionRate = 0.0,

    intensityScale = 1.0,

    -- Wind is a direction plus a speed. The target moves when the mission asks
    -- for a change; the current value chases it, because real wind does not
    -- reverse in one frame.
    wind = { x = 1.0, y = 0.0, z = 0.0, speed = 0.0 },
    windTarget = { x = 1.0, y = 0.0, z = 0.0, speed = 0.0 },
    windTurnRate = 0.25,

    elapsed = 0.0,

    -- Captured once, so fog and light can be returned to what the mission
    -- authored instead of to a hard-coded default.
    baseline = nil,

    -- key -> { name, spec, created }
    systems = {},

    -- Logged at most once each, because a missing template or an older EXU is
    -- a permanent condition and one line is enough.
    warned = {},
}

local function Warn(key, message)
    if state.warned[key] then
        return
    end
    state.warned[key] = true
    print("[exu_weather] " .. message)
end

-- ---------------------------------------------------------------------------
-- Baseline capture
-- ---------------------------------------------------------------------------

local function CaptureBaseline()
    if state.baseline ~= nil then
        return
    end

    local fog = Call("GetFog")
    local ambient = Call("GetAmbientLight")
    local sunDiffuse = Call("GetSunDiffuse")

    state.baseline = {
        fog = fog,
        ambient = ambient,
        sunDiffuse = sunDiffuse,
    }
end

local function RestoreBaseline()
    local baseline = state.baseline
    if baseline == nil then
        return
    end

    if baseline.fog ~= nil then
        Call("SetFog", baseline.fog.r, baseline.fog.g, baseline.fog.b,
            baseline.fog.start, baseline.fog.ending)
    end
    if baseline.ambient ~= nil then
        Call("SetAmbientLight", baseline.ambient.r, baseline.ambient.g, baseline.ambient.b)
    end
    if baseline.sunDiffuse ~= nil then
        Call("SetSunDiffuse", baseline.sunDiffuse.r, baseline.sunDiffuse.g, baseline.sunDiffuse.b)
    end
end

-- ---------------------------------------------------------------------------
-- Particle systems
-- ---------------------------------------------------------------------------

local function SystemName(key)
    return state.prefix .. key
end

local function DestroySystem(key)
    local record = state.systems[key]
    if record == nil then
        return
    end

    Call("DetachParticleSystem", record.name)
    Call("DestroyParticleSystem", record.name)
    state.systems[key] = nil
end

local function DestroyAllSystems()
    for key in pairs(state.systems) do
        DestroySystem(key)
    end
    state.systems = {}
end

local function CreateSystem(spec)
    local name = SystemName(spec.key)

    -- A leftover system from a previous profile would otherwise collide by
    -- name and the create would fail.
    Call("DetachParticleSystem", name)
    Call("DestroyParticleSystem", name)

    if Call("CreateParticleSystem", name, spec.template) ~= true then
        Warn("template:" .. spec.template,
            "could not create '" .. spec.template ..
            "'; is exu_weather.particle present in an enabled mod?")
        return nil
    end

    if spec.nonVisibleTimeout ~= nil then
        Call("SetParticleSystemNonVisibleUpdateTimeout", name, spec.nonVisibleTimeout)
    end

    if spec.follow == "camera" then
        -- The emitter volume travels with the player; particles already
        -- emitted stay in world space, which is what stops precipitation
        -- sliding with the view.
        local offset = spec.offset or { 0, 0, 0 }
        Call("SetParticleSystemKeepLocalSpace", name, false)
        Call("AttachParticleSystemToCamera", name, offset)
    end

    local record = {
        name = name,
        spec = spec,
        -- Resolved lazily on first use: an older EXU without the affector
        -- bridge reports nil and wind simply stops being applied.
        windAffectorChecked = false,
        windAffectorUsable = false,
    }
    state.systems[spec.key] = record
    return record
end

-- Confirms the affector the profile nominated for wind really is a LinearForce
-- before writing to it. Writing force_vector to, say, a Scaler would silently
-- do nothing, and a template edit that reorders affectors is exactly the kind
-- of mistake that produces "wind stopped working" with no error.
local function ResolveWindAffector(record)
    if record.windAffectorChecked then
        return record.windAffectorUsable
    end
    record.windAffectorChecked = true

    local index = record.spec.windAffector
    if index == nil then
        return false
    end

    if not Has("SetParticleAffectorParameter") then
        Warn("affector-api",
            "this EXU build has no affector parameter API; wind will not be applied")
        return false
    end

    local kind = Call("GetParticleAffectorType", record.name, index)
    if kind ~= "LinearForce" then
        Warn("wind:" .. record.spec.template,
            "affector " .. tostring(index) .. " of '" .. record.spec.template ..
            "' is " .. tostring(kind) .. ", not LinearForce; wind disabled for it")
        return false
    end

    record.windAffectorUsable = true
    return true
end

local function ApplyWind(record, weight)
    if not ResolveWindAffector(record) then
        return
    end

    local scale = (record.spec.windScale or 1.0) * state.wind.speed * weight
    Call("SetParticleAffectorParameter", record.name, record.spec.windAffector,
        "force_vector", Vec3Text(state.wind.x * scale, state.wind.y * scale, state.wind.z * scale))
end

local function ApplyEmitters(record, weight)
    local emitters = record.spec.emitters
    if emitters == nil then
        return
    end

    local pulseFactor = 1.0
    local pulse = record.spec.pulse
    if pulse ~= nil then
        -- Deterministic: same elapsed time gives the same value, so two
        -- machines in a multiplayer session stay visually in step without
        -- anything being synchronised.
        pulseFactor = 1.0 + pulse.amplitude * math.sin(state.elapsed * pulse.frequency * 2.0 * math.pi)
    end

    for index, emitter in pairs(emitters) do
        if emitter.rate ~= nil then
            Call("SetParticleEmitterEmissionRate", record.name, index,
                math.max(0.0, emitter.rate * weight * pulseFactor))
        end

        -- Steering the emitter direction into the wind as well as pushing the
        -- particles keeps heavy precipitation from looking like it falls
        -- straight down through a crosswind.
        if emitter.windAim ~= nil and state.wind.speed > 0.0 then
            local aim = emitter.windAim
            local x, y, z = Normalize(
                state.wind.x * aim + emitter.baseDirection[1],
                emitter.baseDirection[2],
                state.wind.z * aim + emitter.baseDirection[3])
            Call("SetParticleEmitterDirection", record.name, index, { x, y, z })
        end
    end
end

-- ---------------------------------------------------------------------------
-- Environment
-- ---------------------------------------------------------------------------

local function ApplyEnvironment(weight)
    local baseline = state.baseline
    if baseline == nil then
        return
    end

    local profile = state.profile

    if profile.fog ~= nil and baseline.fog ~= nil then
        local target = profile.fog
        Call("SetFog",
            Lerp(baseline.fog.r, target.r, weight),
            Lerp(baseline.fog.g, target.g, weight),
            Lerp(baseline.fog.b, target.b, weight),
            Lerp(baseline.fog.start, target.start, weight),
            Lerp(baseline.fog.ending, target.ending, weight))
    end

    local ambientScale = profile.ambientScale or 1.0
    if ambientScale ~= 1.0 and baseline.ambient ~= nil then
        local scale = Lerp(1.0, ambientScale, weight)
        Call("SetAmbientLight",
            baseline.ambient.r * scale,
            baseline.ambient.g * scale,
            baseline.ambient.b * scale)
    end

    if ambientScale ~= 1.0 and baseline.sunDiffuse ~= nil then
        local scale = Lerp(1.0, ambientScale, weight)
        Call("SetSunDiffuse",
            baseline.sunDiffuse.r * scale,
            baseline.sunDiffuse.g * scale,
            baseline.sunDiffuse.b * scale)
    end
end

-- ---------------------------------------------------------------------------
-- Public API
-- ---------------------------------------------------------------------------

--- Prepares the controller. Safe to call again; a second call re-captures the
--- mission's fog and light baseline and drops any existing systems.
--- @param options table? optional { prefix = string, windTurnRate = number }
function Weather.Init(options)
    options = options or {}

    if state.initialized then
        Weather.Shutdown()
    end

    state.prefix = options.prefix or "exu_weather_"
    state.windTurnRate = options.windTurnRate or 0.25
    state.baseline = nil
    state.warned = {}
    state.elapsed = 0.0
    state.intensity = 0.0
    state.targetIntensity = 0.0
    state.transitionRate = 0.0
    state.profileName = "clear"
    state.profile = Weather.Profiles.clear

    CaptureBaseline()
    state.initialized = true
end

--- Switches to a named profile. Weather profiles fade in from the mission
--- baseline; clear removes the active systems and restores that baseline.
--- @param name string a key in Weather.Profiles
--- @param transitionSeconds number? optional, defaults to 6
--- @return boolean
function Weather.SetProfile(name, transitionSeconds)
    local profile = Weather.Profiles[name]
    if profile == nil then
        Warn("profile:" .. tostring(name), "unknown profile '" .. tostring(name) .. "'")
        return false
    end

    if not state.initialized then
        Weather.Init()
    end

    state.profileName = name
    state.profile = profile

    DestroyAllSystems()

    -- `clear` has no authored fog or lighting target. Leaving it to the
    -- normal profile path would therefore skip every environment write and
    -- strand the previous profile's fog/light settings indefinitely.
    if name == "clear" then
        RestoreBaseline()
        state.intensity = 0.0
        state.targetIntensity = 0.0
        state.transitionRate = 0.0
        return true
    end

    for _, spec in ipairs(profile.systems) do
        CreateSystem(spec)
    end

    -- Start silent and ramp in, so a profile change never begins with a frame
    -- of full-rate emission.
    state.intensity = 0.0
    state.targetIntensity = 1.0

    local seconds = transitionSeconds or 6.0
    state.transitionRate = (seconds > 0.0) and (1.0 / seconds) or 0.0
    if state.transitionRate == 0.0 then
        state.intensity = state.targetIntensity
    end

    return true
end

--- @return string
function Weather.GetProfile()
    return state.profileName
end

--- Scales the active profile without changing which profile it is. 0 silences
--- the weather; 1 is the profile's authored strength.
--- @param scale number
function Weather.SetIntensity(scale)
    state.intensityScale = Clamp01(scale or 1.0)
end

--- @return number
function Weather.GetIntensity()
    return state.intensityScale
end

--- Points the wind. Direction is a world vector; only its horizontal component
--- matters for precipitation. The change is eased in rather than applied
--- immediately.
--- @param x number
--- @param z number
--- @param speed number world units per second
--- @param immediate boolean? optional, skip the ease
function Weather.SetWind(x, z, speed, immediate)
    local nx, ny, nz = Normalize(x or 0.0, 0.0, z or 0.0)
    if nx == 0.0 and nz == 0.0 then
        nx, nz = 1.0, 0.0
    end

    state.windTarget.x = nx
    state.windTarget.y = ny
    state.windTarget.z = nz
    state.windTarget.speed = math.max(0.0, speed or 0.0)

    if immediate then
        state.wind.x = state.windTarget.x
        state.wind.y = state.windTarget.y
        state.wind.z = state.windTarget.z
        state.wind.speed = state.windTarget.speed
    end
end

--- @return number x, number z, number speed
function Weather.GetWind()
    return state.wind.x, state.wind.z, state.wind.speed
end

--- Advances the controller. Call once per frame with the frame delta.
--- @param dt number seconds
function Weather.Update(dt)
    if not state.initialized then
        return
    end

    dt = dt or 0.0
    if dt < 0.0 or dt ~= dt then
        return
    end

    state.elapsed = state.elapsed + dt

    -- Ease the intensity toward its target.
    if state.transitionRate > 0.0 then
        local step = state.transitionRate * dt
        if state.intensity < state.targetIntensity then
            state.intensity = math.min(state.targetIntensity, state.intensity + step)
        elseif state.intensity > state.targetIntensity then
            state.intensity = math.max(state.targetIntensity, state.intensity - step)
        end
    else
        state.intensity = state.targetIntensity
    end

    -- Ease the wind toward its target. Direction and speed both chase, so a
    -- gust builds and a shift swings round rather than snapping.
    local turn = Clamp01(state.windTurnRate * dt)
    state.wind.x = Lerp(state.wind.x, state.windTarget.x, turn)
    state.wind.y = Lerp(state.wind.y, state.windTarget.y, turn)
    state.wind.z = Lerp(state.wind.z, state.windTarget.z, turn)
    state.wind.x, state.wind.y, state.wind.z = Normalize(state.wind.x, state.wind.y, state.wind.z)
    state.wind.speed = Lerp(state.wind.speed, state.windTarget.speed, turn)

    local weight = Clamp01(state.intensity * state.intensityScale)

    for _, record in pairs(state.systems) do
        ApplyEmitters(record, weight)
        ApplyWind(record, weight)
    end

    ApplyEnvironment(weight)

    -- Camera-attached systems that could not bind to a real camera scene node
    -- are driven by EXU's own follower list, which needs one tick per frame.
    Call("UpdateParticleFollowers")
end

--- Destroys every system this controller created and restores the fog and
--- light it captured at Init. Call from mission teardown: particle systems do
--- not survive a mission change, but the captured environment has to be put
--- back before anything else reads it.
function Weather.Shutdown()
    DestroyAllSystems()
    RestoreBaseline()

    state.initialized = false
    state.intensity = 0.0
    state.targetIntensity = 0.0
    state.profileName = "clear"
    state.profile = Weather.Profiles.clear
end

return Weather
