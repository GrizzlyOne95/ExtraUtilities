-- Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
-- Segmented ring gauges drawn with EXU overlays, after Battlezone 2's status
-- ring (StatusDisplay: ten 9-degree segments per gauge, inner radius 0.6 of
-- the outer, n = ceil(ratio * 10) lit with the last one partly faded).
--
-- Every segment is a square overlay Panel covering the whole ring, centred on
-- the ring centre. Its material is a clone of EXU_HUD/RingSegment, whose
-- texture holds one segment pointing straight up; the clone's texture
-- rotation turns it about the panel centre into place, and its pass diffuse
-- (read by EXU_HudTint_vertex) colours it. Angles are degrees, anticlockwise
-- on screen from the positive x axis, so 90 is straight up.
--
-- This draws on top of the stock HUD; it does not hide the stock hull/ammo
-- bars (that needs the unqualified native meter hook, see
-- Docs/NATIVE_HUD_LAYOUT_API.md). Pass the required exu table explicitly;
-- mission scope has no global exu.
local Ring = {}

local SEGMENT_MATERIAL = "EXU_HUD/RingSegment"
local SOLID_MATERIAL = "EXU_HUD/Solid"
local PIXELS = 1

local function Color(c, fallback)
    c = c or fallback
    return { c[1] or c.r or 0, c[2] or c.g or 0, c[3] or c.b or 0, c[4] or c.a or 1 }
end

local function Lerp(a, b, t)
    return { a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t,
             a[3] + (b[3] - a[3]) * t, a[4] + (b[4] - a[4]) * t }
end

-- SetMaterialPassColors reads keyed r/g/b/a fields.
local function Keyed(c)
    return { r = c[1], g = c[2], b = c[3], a = c[4] }
end

local function Same(a, b)
    return a ~= nil and a[1] == b[1] and a[2] == b[2] and a[3] == b[3] and a[4] == b[4]
end

-- BZ2 GetHealthColor: green at half or more, yellow from a quarter, else red.
function Ring.HealthColor(ratio)
    if ratio >= 0.5 then return { 0, 1, 0, 1 } end
    if ratio >= 0.25 then return { 1, 1, 0, 1 } end
    return { 1, 0, 0, 1 }
end

Ring.AMMO_COLOR = { 0, 127 / 255, 1, 1 }

-- Per-segment colours for a fill ratio: segments below the fill take `on`,
-- the boundary segment blends from `off` by its fractional share, the rest
-- take `off`. Exposed for tests and for callers drawing their own segments.
function Ring.SegmentColors(ratio, count, on, off)
    ratio = math.max(0, math.min(1, ratio or 0))
    local scaled = ratio * count
    local lit = math.ceil(scaled)
    local colors = {}
    for i = 1, count do
        if i < lit then
            colors[i] = on
        elseif i == lit then
            colors[i] = Lerp(off, on, scaled - lit + 1)
        else
            colors[i] = off
        end
    end
    return colors
end

local function EnsureClone(api, source, clone)
    if api.MaterialExists(clone) then return true end
    return api.CloneMaterial(source, clone)
end

local function NewPanel(api, name, parent, material)
    if not api.HasOverlayElement(name) then
        api.CreateOverlayElement("Panel", name)
    end
    api.SetOverlayMetricsMode(name, PIXELS)
    if material then api.SetOverlayMaterial(name, material) end
    if parent then api.AddOverlayElementChild(parent, name) end
end

-- spec = {
--   name = "unique_prefix",          -- element and material names derive from it
--   parent = "container",            -- an existing Panel to attach segments to
--   x, y, radius = pixels,           -- ring centre and outer radius, relative to parent
--   segments = 10, start = 90, step = 9,  -- first segment edge and signed width (degrees)
--   on = color, off = color,         -- {r,g,b,a} 0..1; off defaults to dim grey
-- }
function Ring.New(api, spec)
    assert(type(api) == "table", "EXU table required")
    assert(type(spec) == "table" and type(spec.name) == "string", "spec.name required")
    assert(type(spec.parent) == "string", "spec.parent container required")
    local self = {
        name = spec.name,
        count = spec.segments or 10,
        start = spec.start or 90,
        step = spec.step or 9,
        on = Color(spec.on, { 1, 1, 1, 1 }),
        off = Color(spec.off, { 0.15, 0.15, 0.2, 0.6 }),
        ratio = 0,
        segments = {},
        applied = {},
    }
    assert(self.count >= 1 and self.count <= 64, "segments must be 1..64")

    for i = 1, self.count do
        local element = string.format("%s/seg%d", self.name, i)
        local material = string.format("EXU_HUD/%s/seg%d", self.name, i)
        assert(EnsureClone(api, SEGMENT_MATERIAL, material), "could not clone " .. SEGMENT_MATERIAL)
        NewPanel(api, element, spec.parent, material)
        -- The texture's segment points up (90); centre segment i on its slot.
        local centre = self.start + self.step * (i - 0.5)
        api.SetMaterialTextureRotate(material, math.rad(centre - 90))
        self.segments[i] = { element = element, material = material }
    end

    function self.SetGeometry(x, y, radius)
        local size = 2 * radius
        for _, seg in ipairs(self.segments) do
            api.SetOverlayPosition(seg.element, x - radius, y - radius)
            api.SetOverlayDimensions(seg.element, size, size)
        end
    end

    -- Applies colours; only segments whose colour changed touch the material.
    local function Apply(colors)
        for i, seg in ipairs(self.segments) do
            local c = colors[i]
            if not Same(self.applied[i], c) then
                api.SetMaterialPassColors(seg.material, { diffuse = Keyed(c) })
                self.applied[i] = c
            end
        end
    end

    function self.SetColors(on, off)
        if on then self.on = Color(on) end
        if off then self.off = Color(off) end
        Apply(Ring.SegmentColors(self.ratio, self.count, self.on, self.off))
    end

    function self.SetRatio(ratio, on)
        self.ratio = ratio
        if on then self.on = Color(on) end
        Apply(Ring.SegmentColors(ratio, self.count, self.on, self.off))
    end

    function self.Destroy()
        for _, seg in ipairs(self.segments) do
            if api.HasOverlayElement(seg.element) then
                if spec.parent and api.HasOverlayElement(spec.parent) then
                    api.RemoveOverlayElementChild(spec.parent, seg.element)
                end
                api.DestroyOverlayElement(seg.element)
            end
        end
        self.segments = {}
    end

    self.SetGeometry(spec.x or 0, spec.y or 0, spec.radius or 100)
    self.SetRatio(spec.ratio or 0)
    return self
end

local function Clean(s)
    if type(s) ~= "string" then return nil end
    s = s:gsub("%z.*", "")
    if s == "" then return nil end
    return s
end

-- Default weapon line: wpnCategory initial plus upper-cased wpnName, as the
-- BZ2 demo showed ("R FAF MSL"); falls back to the ODF name.
local odfCache = {}
function Ring.WeaponLabel(odfName)
    local entry = odfCache[odfName]
    if entry == nil then
        local odf = OpenODF(odfName)
        local name = odf and Clean((GetODFString(odf, "WeaponClass", "wpnName", ""))) or nil
        local category = odf and Clean((GetODFString(odf, "WeaponClass", "wpnCategory", ""))) or nil
        entry = { prefix = category and category:sub(1, 1):upper() or "", name = (name or odfName):upper() }
        odfCache[odfName] = entry
    end
    return entry.prefix, entry.name
end

-- A BZ2-demo status cluster: hull ring on the upper-left quarter, ammo ring on
-- the lower-left quarter, percentages inside the ring and weapon lines to the
-- right of the centre line. opts = {
--   name = "exu_status", overlay = "exu_status",  zOrder = 600,
--   x, y = ring centre in pixels (default: lower-left of the screen),
--   radius = outer radius in pixels (default: 17% of screen height),
--   font = "CRBZoneOverlayFont", backplate = color or false,
--   weaponLabel = function(odf, slot) -> prefix, name,
-- }
function Ring.NewStatus(api, opts)
    opts = opts or {}
    local name = opts.name or "exu_status"
    local overlay = opts.overlay or name
    local root = name .. "/root"
    local self = { closed = false }

    api.CreateOverlay(overlay)
    api.SetOverlayZOrder(overlay, opts.zOrder or 600)
    NewPanel(api, root, nil, nil)
    api.SetOverlayPosition(root, 0, 0)
    api.AddOverlay2D(overlay, root)

    local backplate
    if opts.backplate ~= false then
        backplate = name .. "/backplate"
        local material = "EXU_HUD/" .. backplate
        assert(EnsureClone(api, SOLID_MATERIAL, material), "could not clone " .. SOLID_MATERIAL)
        NewPanel(api, backplate, root, material)
        api.SetMaterialPassColors(material, { diffuse = Keyed(Color(opts.backplate, { 0.02, 0.04, 0.16, 0.75 })) })
    end

    local hull = Ring.New(api, { name = name .. "/hull", parent = root, start = 90, step = 9,
        on = Ring.HealthColor(1) })
    local ammo = Ring.New(api, { name = name .. "/ammo", parent = root, start = 270, step = -9,
        on = Ring.AMMO_COLOR })

    local texts = {}
    local function NewText(key, align, color)
        local element = string.format("%s/%s", name, key)
        if not api.HasOverlayElement(element) then api.CreateOverlayElement("TextArea", element) end
        api.SetOverlayMetricsMode(element, PIXELS)
        api.SetOverlayTextFont(element, opts.font or "CRBZoneOverlayFont")
        api.SetOverlayParameter(element, "alignment", align)
        api.SetOverlayTextColor(element, color[1], color[2], color[3], color[4])
        api.AddOverlayElementChild(root, element)
        texts[key] = element
        return element
    end
    local white = { 0.85, 0.9, 1, 1 }
    local weaponColor = { 0.85, 0.85, 0.35, 1 }
    NewText("hullText", "right", white)
    NewText("ammoText", "right", white)
    for slot = 0, 4 do
        NewText("prefix" .. slot, "left", white)
        NewText("weapon" .. slot, "left", weaponColor)
    end

    local layout = {}
    function self.Layout(x, y, radius)
        local w, h = api.GetGameResolution()
        radius = radius or opts.radius or math.floor(h * 0.17)
        x = x or opts.x or math.floor(radius + h * 0.03)
        y = y or opts.y or math.floor(h - radius - h * 0.03)
        layout = { x = x, y = y, radius = radius, w = w, h = h }
        hull.SetGeometry(x, y, radius)
        ammo.SetGeometry(x, y, radius)
        if backplate then
            api.SetOverlayPosition(backplate, x - radius * 1.03, y - radius * 1.03)
            api.SetOverlayDimensions(backplate, radius * 2.2, radius * 2.06)
        end
        local char = math.max(8, math.floor(radius * 0.13))
        local gap = math.floor(radius * 0.03)
        for key, element in pairs(texts) do
            api.SetOverlayTextCharHeight(element, char)
        end
        api.SetOverlayPosition(texts.hullText, x - gap, y - char - gap)
        api.SetOverlayPosition(texts.ammoText, x - gap, y + gap)
        for slot = 0, 4 do
            local lineY = y + gap + slot * char
            api.SetOverlayPosition(texts["prefix" .. slot], x + gap, lineY)
            api.SetOverlayPosition(texts["weapon" .. slot], x + gap + char, lineY)
        end
    end

    local function Ratio(cur, max)
        if type(cur) ~= "number" or type(max) ~= "number" or max <= 0 then return nil end
        return math.max(0, math.min(1, cur / max))
    end

    local function Percent(ratio)
        return ratio and string.format("%d%%", math.floor(ratio * 100 + 0.5)) or "--"
    end

    -- Call once per frame. Reacquires the local player every time: ejection,
    -- vehicle changes, death and respawn replace it.
    function self.Update()
        if self.closed then return end
        local w, h = api.GetGameResolution()
        if w ~= layout.w or h ~= layout.h then self.Layout(opts.x, opts.y, opts.radius) end

        local hidden = type(api.IsGameUiOpen) == "function" and api.IsGameUiOpen()
        local player = GetPlayerHandle()
        if hidden or player == nil or not IsValid(player) then
            api.HideOverlay(overlay)
            return
        end
        api.ShowOverlay(overlay)

        local hullRatio = Ratio(GetCurHealth(player), GetMaxHealth(player))
        local ammoRatio = Ratio(GetCurAmmo(player), GetMaxAmmo(player))
        hull.SetRatio(hullRatio or 0, Ring.HealthColor(hullRatio or 0))
        ammo.SetRatio(ammoRatio or 0)
        api.SetOverlayCaption(texts.hullText, Percent(hullRatio))
        api.SetOverlayCaption(texts.ammoText, Percent(ammoRatio))

        local label = opts.weaponLabel or function(odf) return Ring.WeaponLabel(odf) end
        local line = 0
        for slot = 0, 4 do
            local odf = Clean(GetWeaponClass(player, slot))
            if odf then
                local prefix, weapon = label(odf, slot)
                api.SetOverlayCaption(texts["prefix" .. line], prefix or "")
                api.SetOverlayCaption(texts["weapon" .. line], weapon or "")
                line = line + 1
            end
        end
        for slot = line, 4 do
            api.SetOverlayCaption(texts["prefix" .. slot], "")
            api.SetOverlayCaption(texts["weapon" .. slot], "")
        end
    end

    function self.Destroy()
        if self.closed then return end
        self.closed = true
        hull.Destroy()
        ammo.Destroy()
        for _, element in pairs(texts) do
            if api.HasOverlayElement(element) then api.DestroyOverlayElement(element) end
        end
        if backplate and api.HasOverlayElement(backplate) then api.DestroyOverlayElement(backplate) end
        api.DestroyOverlay(overlay)
        if api.HasOverlayElement(root) then api.DestroyOverlayElement(root) end
    end

    self.hull, self.ammo = hull, ammo
    self.Layout(opts.x, opts.y, opts.radius)
    return self
end

return Ring
