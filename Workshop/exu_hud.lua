-- Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
-- Authored instrument slots around the native live meters. The native adapter
-- is still unqualified; numeric TextArea readouts work with current EXU.
-- Pass the required exu table explicitly; mission scope has no global exu.
local Hud = {}
local meters = { "hull", "ammo" }

-- slots: { hull={x,y,w,h}, ammo={x,y,w,h} }, relative to viewport dimensions.
-- values: optional names of already-authored TextArea elements.
function Hud.New(api, slots, values)
    assert(type(api) == "table", "EXU table required")
    local requested = {}
    for _, meter in ipairs(meters) do
        local slot = (slots or {})[meter]
        if slot then
            requested[meter] = {}
            for i = 1, 4 do
                local value = slot[i]
                assert(type(value) == "number" and value == value and math.abs(value) < math.huge,
                    "slot coordinates must be finite numbers")
                assert(i < 3 or value > 0, "slot dimensions must be positive")
                requested[meter][i] = value
            end
        end
    end

    local self = { applied = {}, status = {}, closed = false }
    local text = { hull = (values or {}).hull, ammo = (values or {}).ammo }
    local owned = {}
    local width, height
    local function round(value) return math.floor(value + 0.5) end

    function self.Update(showValues)
        if self.closed then return false end
        local w, h = api.GetGameResolution()
        if w ~= width or h ~= height then
            self.applied = {}
            width, height = w, h
        end
        local allApplied = true
        for _, meter in ipairs(meters) do
            local slot = requested[meter]
            if slot then
                local available = type(api.IsNativeHudLayoutAvailable) == "function" and
                    api.IsNativeHudLayoutAvailable(meter)
                if not available then
                    self.applied[meter] = nil
                    self.status[meter] = "unavailable"
                elseif not self.applied[meter] then
                    -- Retry once a frame exists, including a late-loaded provider
                    -- or a player who temporarily had no stock status display.
                    if api.GetNativeHudMeterDefaultRect(meter) ~= nil and w > 0 and h > 0 then
                        local ok, reason = api.SetNativeHudMeterRect(meter,
                            round(slot[1] * w), round(slot[2] * h),
                            math.max(1, round(slot[3] * w)), math.max(1, round(slot[4] * h)))
                        if ok then owned[meter] = true end
                        self.applied[meter] = ok or nil
                        self.status[meter] = ok and "accepted" or reason
                    else
                        self.status[meter] = "awaiting_frame"
                    end
                end
                if not self.applied[meter] then allApplied = false end
            end
        end

        -- Reacquire the LOCAL object on every update: ejection, vehicle changes,
        -- death and respawn can all replace it. These values use stock Lua.
        local player = GetPlayerHandle()
        local valid = player ~= nil and IsValid(player)
        if showValues == nil then
            showValues = not (type(api.IsGameUiOpen) == "function" and api.IsGameUiOpen())
        end
        for _, meter in ipairs(meters) do
            local name = text[meter]
            if name and api.HasOverlayElement(name) then
                local caption = ""
                if valid and showValues then
                    local current, maximum
                    if meter == "hull" then
                        current, maximum = GetCurHealth(player), GetMaxHealth(player)
                    else
                        current, maximum = GetCurAmmo(player), GetMaxAmmo(player)
                    end
                    caption = string.format("%.0f / %.0f", current, maximum)
                end
                api.SetOverlayCaption(name, caption)
            end
        end
        return allApplied
    end

    function self.Shutdown()
        if self.closed then return end
        for _, meter in ipairs(meters) do
            if owned[meter] and type(api.RestoreNativeHudMeter) == "function" then
                api.RestoreNativeHudMeter(meter)
            end
            local name = text[meter]
            if name and api.HasOverlayElement(name) then api.SetOverlayCaption(name, "") end
        end
        self.applied = {}
        self.closed = true
    end
    return self
end

return Hud
