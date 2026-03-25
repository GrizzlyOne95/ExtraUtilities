--[[
    Example terrain/environment "planet swap" helper for EXU.

    This keeps the current map's HG2/TRN tile painting intact and only swaps
    the terrain material texture set plus any environment values you want to
    pair with it.
--]]

local exu = require("exu")

local PLANET_PRESETS = {
    Ice = {
        terrain = {
            diffuse = "trn_ice_diffuse.dds",
            detail = "trn_ice_detail.dds",
            normal = "trn_ice_normal.dds",
            specular = "trn_ice_specular.dds",
            emissive = "black.dds",
        },
        fog = { r = 0.66, g = 0.76, b = 0.88, start = 120, ending = 1200 },
        ambient = { r = 0.55, g = 0.60, b = 0.70, a = 1.0 },
        sunDiffuse = { r = 0.80, g = 0.87, b = 1.00, a = 1.0 },
        sunSpecular = { r = 0.60, g = 0.70, b = 0.90, a = 1.0 },
        sunDirection = SetVector(-0.30, -0.95, 0.08),
        timeOfDay = 930,
        shadowFarDistance = 850,
    },
}

local function ApplyPlanetPreset(preset)
    local ok, terrainMaterial = exu.SetTerrainTextureSet(preset.terrain)
    if not ok then
        print("Terrain swap failed")
        return false
    end

    print("Terrain material: " .. terrainMaterial)

    if preset.fog then
        exu.SetFog(preset.fog)
    end

    if preset.ambient then
        exu.SetAmbientLight(preset.ambient)
    end

    if preset.sunDiffuse then
        exu.SetSunDiffuse(preset.sunDiffuse)
    end

    if preset.sunSpecular then
        exu.SetSunSpecular(preset.sunSpecular)
    end

    if preset.sunDirection then
        exu.SetSunDirection(preset.sunDirection)
    end

    if preset.timeOfDay then
        exu.SetTimeOfDay(preset.timeOfDay, true)
    end

    if preset.shadowFarDistance then
        exu.SetSunShadowFarDistance(preset.shadowFarDistance)
    end

    return true
end

function Start()
    ApplyPlanetPreset(PLANET_PRESETS.Ice)
end
