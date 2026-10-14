-- Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
-- Segmented ring gauges drawn with EXU overlays, after Battlezone 2's status
-- ring (StatusDisplay: ten 9-degree segments per gauge, inner radius 0.6 of
-- the outer, n = ceil(ratio * 10) lit with the last one partly faded).
--
-- Every segment is an overlay Panel covering the whole ring, centred on the
-- ring centre. Its material is a clone of EXU_HUD/RingSegment, whose texture
-- holds one segment pointing straight up; the clone's texture rotation turns
-- it about the panel centre into place and its pass diffuse colours it. A
-- panel wider than it is tall (spec.aspect) stretches the ring into the
-- ellipse the BZ2 demo drew. Angles are degrees, anticlockwise on screen from
-- the positive x axis, so 90 is straight up.
--
-- A clone is configured once, before it is first drawn, and never edited
-- afterwards: lighting modes copy techniques into their render schemes on
-- first use, and a later edit would miss those copies. A colour change
-- switches the panel to another clone (one per segment and colour, made on
-- first use; the boundary segment's fade is quantised to keep them few).
--
-- Ring.NewStatus hides the stock status display's hull, ammo and weapon
-- draws with exu.SetStockStatusHudVisible (no OpenShim needed) unless
-- opts.hideStock is false. Pass the required exu table explicitly; mission
-- scope has no global exu.
local Ring = {}

local SEGMENT_MATERIAL = "EXU_HUD/RingSegment"
local PLATE_MATERIAL = "EXU_HUD/StatusPlate"
local TECHNIQUES = 3 -- SM4, SM3, GLSL in exu_hud.material
local PIXELS = 1

-- exu_hud_status_plate.png extents in vertical ring radii, drawn for a ring
-- of PLATE.aspect; keep in step with tools/generate_hud_textures.py.
Ring.PLATE = { aspect = 2.2, left = 2.35, right = 2.34, half = 1.15 }

local function Color(c, fallback)
    c = c or fallback
    return { c[1] or c.r or 0, c[2] or c.g or 0, c[3] or c.b or 0, c[4] or c.a or 1 }
end

local function Lerp(a, b, t)
    return { a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t,
             a[3] + (b[3] - a[3]) * t, a[4] + (b[4] - a[4]) * t }
end

local function Byte(v)
    return math.floor(math.max(0, math.min(1, v)) * 255 + 0.5)
end

local function ColorKey(c)
    return string.format("%02x%02x%02x%02x", Byte(c[1]), Byte(c[2]), Byte(c[3]), Byte(c[4]))
end

-- BZ2 GetHealthColor: green at half or more, yellow from a quarter, else red.
function Ring.HealthColor(ratio)
    if ratio >= 0.5 then return { 0, 1, 0, 1 } end
    if ratio >= 0.25 then return { 1, 1, 0, 1 } end
    return { 1, 0, 0, 1 }
end

Ring.AMMO_COLOR = { 0, 127 / 255, 1, 1 }

-- Per-segment colours for a fill ratio: segments below the fill take `on`,
-- the boundary segment blends from `off` by its fractional share (rounded to
-- 1/fadeSteps when given), the rest take `off`.
function Ring.SegmentColors(ratio, count, on, off, fadeSteps)
    ratio = math.max(0, math.min(1, ratio or 0))
    local scaled = ratio * count
    local lit = math.ceil(scaled)
    local colors = {}
    for i = 1, count do
        if i < lit then
            colors[i] = on
        elseif i == lit then
            local t = scaled - lit + 1
            if fadeSteps then t = math.floor(t * fadeSteps + 0.5) / fadeSteps end
            colors[i] = (t >= 1 and on) or (t <= 0 and off) or Lerp(off, on, t)
        else
            colors[i] = off
        end
    end
    return colors
end

-- Live selected-weapon bits (bit n = slot n; linked weapons set several), or
-- nil on EXU builds without exu.GetSelectedWeaponMask, where every line stays lit.
function Ring.SelectedWeaponMask(api, handle)
    if type(api.GetSelectedWeaponMask) ~= "function" then return nil end
    local ok, mask = pcall(api.GetSelectedWeaponMask, handle)
    if ok and type(mask) == "number" then return mask end
    return nil
end

function Ring.IsSlotSelected(mask, slot)
    return math.floor(mask / 2 ^ slot) % 2 == 1
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
--   x, y, radius = pixels,           -- ring centre and vertical outer radius, relative to parent
--   aspect = 1,                      -- horizontal radius / vertical radius
--   segments = 10, start = 90, step = 9,  -- first segment edge and signed width (degrees)
--   on = color, off = color,         -- {r,g,b,a} 0..1; off defaults to dim grey
--   fadeSteps = 4,                   -- boundary-segment fade levels
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
        aspect = spec.aspect or 1,
        fadeSteps = spec.fadeSteps or 4,
        on = Color(spec.on, { 1, 1, 1, 1 }),
        off = Color(spec.off, { 0.15, 0.15, 0.2, 0.6 }),
        ratio = 0,
        segments = {},
        applied = {},
    }
    assert(self.count >= 1 and self.count <= 64, "segments must be 1..64")

    for i = 1, self.count do
        local element = string.format("%s/seg%d", self.name, i)
        NewPanel(api, element, spec.parent, nil)
        -- The texture's segment points up (90); centre segment i on its slot.
        local degrees = self.start + self.step * (i - 0.5) - 90
        self.segments[i] = { element = element, degrees = degrees }
    end

    -- The clone for a segment in colour c, configured before first use. The
    -- name carries the rotation, so a reused name always means the same art.
    local function Variant(seg, c)
        local material = string.format("EXU_HUD/%s/r%d/%s", self.name,
            math.floor(seg.degrees * 10 + 0.5), ColorKey(c))
        if not api.MaterialExists(material) then
            assert(api.CloneMaterial(SEGMENT_MATERIAL, material), "could not clone " .. SEGMENT_MATERIAL)
            for technique = 0, TECHNIQUES - 1 do
                api.SetMaterialTextureRotate(material, math.rad(seg.degrees), technique)
            end
            api.SetMaterialPassColors(material,
                { diffuse = { r = c[1], g = c[2], b = c[3], a = c[4] } }, -1, -1)
        end
        return material
    end

    function self.SetGeometry(x, y, radius, aspect)
        if aspect then self.aspect = aspect end
        for _, seg in ipairs(self.segments) do
            api.SetOverlayPosition(seg.element, x - radius * self.aspect, y - radius)
            api.SetOverlayDimensions(seg.element, 2 * radius * self.aspect, 2 * radius)
        end
    end

    -- Switches only segments whose colour changed.
    local function Apply(colors)
        for i, seg in ipairs(self.segments) do
            local key = ColorKey(colors[i])
            if self.applied[i] ~= key then
                api.SetOverlayMaterial(seg.element, Variant(seg, colors[i]))
                self.applied[i] = key
            end
        end
    end

    function self.SetColors(on, off)
        if on then self.on = Color(on) end
        if off then self.off = Color(off) end
        Apply(Ring.SegmentColors(self.ratio, self.count, self.on, self.off, self.fadeSteps))
    end

    function self.SetRatio(ratio, on)
        self.ratio = ratio
        if on then self.on = Color(on) end
        Apply(Ring.SegmentColors(ratio, self.count, self.on, self.off, self.fadeSteps))
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

local function OdfValue(getter, odf, section, label)
    local ok, v = pcall(getter, odf, section, label, 0)
    return ok and type(v) == "number" and v or 0
end

-- Engine ammo per trigger pull: ammoCost (the weapon's, else its ordnance's)
-- times the cannon salvoCount, as fpcontroller ShotCost. nil when the weapon
-- costs nothing or its ODF cannot be read.
local costCache = {}
function Ring.ShotCost(odfName)
    local cost = costCache[odfName]
    if cost == nil then
        cost = false
        local ok, odf = pcall(OpenODF, odfName)
        if ok and odf then
            local per = OdfValue(GetODFInt, odf, "WeaponClass", "ammoCost")
            if per <= 0 then
                local okN, ord = pcall(GetODFString, odf, "WeaponClass", "ordName", "")
                ord = okN and Clean(ord) or nil
                local okD, ordOdf = false, nil
                if ord then okD, ordOdf = pcall(OpenODF, ord) end
                if okD and ordOdf then per = OdfValue(GetODFFloat, ordOdf, "OrdnanceClass", "ammoCost") end
            end
            local salvo = math.max(1, OdfValue(GetODFInt, odf, "CannonClass", "salvoCount"))
            if per > 0 then cost = per * salvo end
        end
        costCache[odfName] = cost
    end
    return cost or nil
end

-- Shots as at most four characters: 999, 1.2K, 12K.
function Ring.ShotText(shots)
    if shots < 1000 then return tostring(shots) end
    if shots < 10000 then return string.format("%.1fK", math.floor(shots / 100) / 10) end
    return string.format("%dK", math.floor(shots / 1000))
end

-- text cut to at most `chars` characters, at a word break when there is one.
function Ring.Fit(text, chars)
    if #text <= chars then return text end
    return text:sub(1, chars + 1):match("^(.*%S)%s") or text:sub(1, chars)
end

-- The BZ2-demo status cluster, bottom right by default: a bezelled plate,
-- the hull ring on its upper-left quarter and the ammo ring on its lower-left
-- quarter, percentages inside the rings (the hull points above the hull
-- figure). The plate's right half is a column split at the ring's centre
-- line (the plate's red rule): status lines above it, as many as fit, and one
-- line per weapon below it with its shots left (ammo / Ring.ShotCost) at the
-- right end; a weapon with no shot left turns red. The hull ring flashes pale
-- on each hit and its lit segments blink while the hull is critical.
-- opts = {
--   name = "exu_status", overlay = "exu_status",  zOrder = 600,
--   radius = vertical ring radius in pixels (default 7.5% of screen height),
--   aspect = 2.2,                      -- ring width / height
--   x, y = ring centre in pixels (default: plate in the bottom-right corner),
--   font = "CRBZoneOverlayFont", plate = true,
--   statusLines = 3,                   -- most status rows; fewer when they do not fit
--   status = function(player) -> { "text" or { text = , color = , blink = Hz }, ... } or nil,
--                                      -- in order; a blinking line dims to a quarter
--                                      -- of its alpha for the off half of each cycle
--   weaponLabel = function(odf, slot) -> prefix, name,
--   shots = function(player, slot, odf) -> shots left, or nil for ammo / Ring.ShotCost,
--   dryColor, dimDryColor = {r,g,b,a}, -- weapon lines with no shot left
--   hitFlash = 0.12,                   -- s the hull ring flashes on a hit (false: never);
--                                      -- at most one flash per 0.3 s under sustained fire
--   criticalBelow = 0.10, criticalBlink = 3,  -- hull ratio and Hz the hull ring blinks at
--   dimPrefixColor, dimWeaponColor = {r,g,b,a},  -- unselected weapon lines
--                                      (selection from exu.GetSelectedWeaponMask)
--   hideStock = true,                  -- hide the stock hull/ammo/weapon readout
-- }
function Ring.NewStatus(api, opts)
    opts = opts or {}
    local name = opts.name or "exu_status"
    local overlay = opts.overlay or name
    local root = name .. "/root"
    local aspect = opts.aspect or Ring.PLATE.aspect
    local self = { closed = false, stockHidden = {} }

    -- Older EXU builds lack the stock suppression; the ring still draws.
    local stockParts = { "hull", "ammo", "weapons" }
    if opts.hideStock ~= false and type(api.SetStockStatusHudVisible) == "function" then
        for _, part in ipairs(stockParts) do
            self.stockHidden[part] = api.SetStockStatusHudVisible(part, false) == true
        end
    end

    api.CreateOverlay(overlay)
    api.SetOverlayZOrder(overlay, opts.zOrder or 600)

    -- The plate gets its own root container, added first: an overlay orders
    -- its root containers by insertion, but a container orders its children
    -- by name, so a plate sharing the gauge root would sort over "ammo/..."
    -- and "hull..." and cover the rings and figures.
    local back, plate
    if opts.plate ~= false then
        back = name .. "/back"
        plate = name .. "/plate"
        NewPanel(api, back, nil, nil)
        api.SetOverlayPosition(back, 0, 0)
        api.AddOverlay2D(overlay, back)
        NewPanel(api, plate, back, PLATE_MATERIAL)
    end

    NewPanel(api, root, nil, nil)
    api.SetOverlayPosition(root, 0, 0)
    api.AddOverlay2D(overlay, root)

    local hull = Ring.New(api, { name = name .. "/hull", parent = root, start = 90, step = 9,
        aspect = aspect, on = Ring.HealthColor(1) })
    local ammo = Ring.New(api, { name = name .. "/ammo", parent = root, start = 270, step = -9,
        aspect = aspect, on = Ring.AMMO_COLOR })

    local texts, textSize = {}, {}
    local function NewText(key, align, color, size)
        local element = string.format("%s/%s", name, key)
        if not api.HasOverlayElement(element) then api.CreateOverlayElement("TextArea", element) end
        api.SetOverlayMetricsMode(element, PIXELS)
        api.SetOverlayTextFont(element, opts.font or "CRBZoneOverlayFont")
        api.SetOverlayParameter(element, "alignment", align)
        api.SetOverlayTextColor(element, color[1], color[2], color[3], color[4])
        api.AddOverlayElementChild(root, element)
        texts[key], textSize[key] = element, size
        return element
    end
    local figure = { 0.85, 0.88, 0.95, 1 }
    local valueColor = { 0.62, 0.66, 0.74, 1 }
    local statusColor = { 0.85, 0.88, 0.95, 1 }
    local prefixColor = { 0.95, 0.95, 0.95, 1 }
    local weaponColor = { 0.8, 0.85, 0.35, 1 }
    -- Unselected weapon lines; selected ones keep the colours above.
    local dimPrefixColor = opts.dimPrefixColor or { 0.45, 0.47, 0.5, 0.8 }
    local dimWeaponColor = opts.dimWeaponColor or { 0.4, 0.42, 0.22, 0.8 }
    local dryColor = opts.dryColor or { 1.0, 0.32, 0.26, 1 }
    local dimDryColor = opts.dimDryColor or { 0.55, 0.2, 0.17, 0.8 }
    local statusLines = opts.statusLines or 3
    NewText("hullText", "right", figure, "figure")
    NewText("hullValue", "right", valueColor, "value")
    NewText("ammoText", "right", figure, "figure")
    for line = 1, statusLines do
        NewText("status" .. line, "left", statusColor, "status")
    end
    for slot = 0, 4 do
        NewText("prefix" .. slot, "left", prefixColor, "weapon")
        NewText("weapon" .. slot, "left", weaponColor, "weapon")
        NewText("shots" .. slot, "right", weaponColor, "weapon")
    end

    -- CRBZoneOverlayFont's widest glyphs are 0.56 of the char height; the
    -- shots figure ("999", "1.5K") keeps 3.6 of them clear at the right end
    -- of a weapon line, and longer weapon names are cut at a word break.
    local GLYPH, SHOT_CHARS = 0.56, 3.6
    local layout = {}
    function self.Layout()
        local w, h = api.GetGameResolution()
        local rv = opts.radius or math.floor(h * 0.075)
        local rh = rv * aspect
        local stretch = aspect / Ring.PLATE.aspect
        local margin = math.floor(h * 0.02)
        local x = opts.x or (w - margin - Ring.PLATE.right * rv * stretch)
        local y = opts.y or (h - margin - Ring.PLATE.half * rv)
        layout = { w = w, h = h }
        hull.SetGeometry(x, y, rv)
        ammo.SetGeometry(x, y, rv)
        if plate then
            api.SetOverlayPosition(plate, x - Ring.PLATE.left * rv * stretch, y - Ring.PLATE.half * rv)
            api.SetOverlayDimensions(plate, (Ring.PLATE.left + Ring.PLATE.right) * rv * stretch,
                2 * Ring.PLATE.half * rv)
        end
        -- The right-hand column runs from just off the rule to `inner` (0.98
        -- radii; the plate's inside is 1.06) above and below it, and from the
        -- centre to ~2.0 radii right. Five weapon lines fill the lower half;
        -- the upper half takes as many status lines as fit, from the top.
        local gapY = math.floor(rv * 0.06)
        local inner = math.floor(rv * 0.98)
        local weaponChar = math.max(8, math.min(math.floor(rv * 0.19), math.floor((inner - gapY) / (5 * 1.06))))
        local sizes = {
            figure = math.max(8, math.floor(rv * 0.22)),
            value = math.max(8, math.floor(rv * 0.17)),
            status = math.max(8, math.floor(rv * 0.19)),
            weapon = weaponChar,
        }
        for key, element in pairs(texts) do
            api.SetOverlayTextCharHeight(element, sizes[textSize[key]])
        end
        local char = sizes.figure
        local figureX = x - 0.03 * rh
        api.SetOverlayPosition(texts.hullText, figureX, y - gapY - char)
        api.SetOverlayPosition(texts.hullValue, figureX, y - gapY - char - math.floor(rv * 0.04) - sizes.value)
        api.SetOverlayPosition(texts.ammoText, figureX, y + gapY)

        local lineX = x + 0.11 * rv * stretch
        local shotsX = x + 1.95 * rv * stretch
        local statusPitch = math.ceil(sizes.status * 1.06)
        layout.statusFit = math.min(statusLines, math.floor((inner - gapY) / statusPitch))
        for line = 1, statusLines do
            api.SetOverlayPosition(texts["status" .. line], lineX, y - inner + (line - 1) * statusPitch)
        end
        local weaponPitch = math.ceil(weaponChar * 1.06)
        local nameX = lineX + math.floor(weaponChar * 1.1)
        for slot = 0, 4 do
            local lineY = y + gapY + slot * weaponPitch
            api.SetOverlayPosition(texts["prefix" .. slot], lineX, lineY)
            api.SetOverlayPosition(texts["weapon" .. slot], nameX, lineY)
            api.SetOverlayPosition(texts["shots" .. slot], shotsX, lineY)
        end
        layout.nameChars = math.max(4, math.floor((shotsX - nameX) / (GLYPH * weaponChar) - SHOT_CHARS))
        layout.statusChars = math.max(4, math.floor((shotsX - lineX) / (GLYPH * sizes.status)))
    end

    -- Text colour changes only when a line's selection or dryness does.
    local lineState = {}
    local function SetLineState(line, selected, dry)
        local state = (selected and "s" or "-") .. (dry and "d" or "-")
        if lineState[line] == state then return end
        lineState[line] = state
        local p = selected and prefixColor or dimPrefixColor
        local w
        if dry then w = selected and dryColor or dimDryColor
        else w = selected and weaponColor or dimWeaponColor end
        api.SetOverlayTextColor(texts["prefix" .. line], p[1], p[2], p[3], p[4])
        api.SetOverlayTextColor(texts["weapon" .. line], w[1], w[2], w[3], w[4])
        api.SetOverlayTextColor(texts["shots" .. line], w[1], w[2], w[3], w[4])
    end

    -- Status lines from opts.status, clamped to the lines that fit; a line's
    -- colour is set only when it changes (a blink changes it twice a cycle).
    local statusShown = {}
    local function UpdateStatus(player)
        local entries
        if type(opts.status) == "function" then entries = opts.status(player) end
        local now = type(GetTime) == "function" and GetTime() or 0
        for line = 1, statusLines do
            local entry = line <= layout.statusFit and type(entries) == "table" and entries[line] or nil
            local text = type(entry) == "table" and entry.text or entry
            local c = Color(type(entry) == "table" and entry.color or nil, statusColor)
            local blink = type(entry) == "table" and entry.blink
            if type(blink) == "number" and blink > 0 and math.floor(now * blink * 2) % 2 == 1 then
                c = { c[1], c[2], c[3], c[4] * 0.25 }
            end
            local key = ColorKey(c)
            if statusShown[line] ~= key then
                statusShown[line] = key
                api.SetOverlayTextColor(texts["status" .. line], c[1], c[2], c[3], c[4])
            end
            api.SetOverlayCaption(texts["status" .. line],
                type(text) == "string" and Ring.Fit(text, layout.statusChars) or "")
        end
    end

    local function Ratio(cur, max)
        if type(cur) ~= "number" or type(max) ~= "number" or max <= 0 then return nil end
        return math.max(0, math.min(1, cur / max))
    end

    local function Percent(ratio)
        return ratio and string.format("%d%%", math.floor(ratio * 100 + 0.5)) or "--"
    end

    -- The hull ring's colour: pale for a moment after a hit, dark for the off
    -- half of each blink while critical, else Ring.HealthColor.
    local WHITE = { 1, 1, 1, 1 }
    local hit = { handle = nil, hull = nil, flashUntil = 0, nextFlash = 0 }
    local function HullColor(player, cur, max, ratio, now)
        local c = Ring.HealthColor(ratio)
        local flash = opts.hitFlash == nil and 0.12 or opts.hitFlash
        if flash and type(cur) == "number" then
            local drop = hit.handle == player and hit.hull and hit.hull - cur or 0
            if drop > math.max(0.5, (max or 0) * 0.002) and now >= hit.nextFlash then
                hit.flashUntil, hit.nextFlash = now + flash, now + math.max(0.3, flash * 2)
            end
            hit.handle, hit.hull = player, cur
            if now < hit.flashUntil then return Lerp(c, WHITE, 0.75) end
        end
        local blink = opts.criticalBlink or 3
        if ratio < (opts.criticalBelow or 0.10) and blink > 0 and math.floor(now * blink * 2) % 2 == 1 then
            return { c[1] * 0.3, c[2] * 0.3, c[3] * 0.3, c[4] }
        end
        return c
    end

    -- Call once per frame. Reacquires the local player every time: ejection,
    -- vehicle changes, death and respawn replace it.
    function self.Update()
        if self.closed then return end
        local w, h = api.GetGameResolution()
        if w ~= layout.w or h ~= layout.h then self.Layout() end

        local hidden = type(api.IsGameUiOpen) == "function" and api.IsGameUiOpen()
        local player = GetPlayerHandle()
        if hidden or player == nil or not IsValid(player) then
            api.HideOverlay(overlay)
            return
        end
        api.ShowOverlay(overlay)

        local now = type(GetTime) == "function" and GetTime() or 0
        local curHull, curAmmo = GetCurHealth(player), GetCurAmmo(player)
        local maxHull = GetMaxHealth(player)
        local hullRatio = Ratio(curHull, maxHull)
        local ammoRatio = Ratio(curAmmo, GetMaxAmmo(player))
        hull.SetRatio(hullRatio or 0, HullColor(player, curHull, maxHull, hullRatio or 0, now))
        ammo.SetRatio(ammoRatio or 0)
        api.SetOverlayCaption(texts.hullText, Percent(hullRatio))
        api.SetOverlayCaption(texts.hullValue,
            type(curHull) == "number" and tostring(math.floor(curHull + 0.5)) or "")
        api.SetOverlayCaption(texts.ammoText, Percent(ammoRatio))
        UpdateStatus(player)

        local label = opts.weaponLabel or function(odf) return Ring.WeaponLabel(odf) end
        local selectedMask = Ring.SelectedWeaponMask(api, player)
        local line = 0
        for slot = 0, 4 do
            local odf = Clean(GetWeaponClass(player, slot))
            if odf then
                local prefix, weapon = label(odf, slot)
                local selected = selectedMask == nil or Ring.IsSlotSelected(selectedMask, slot)
                local n = type(opts.shots) == "function" and opts.shots(player, slot, odf) or nil
                if type(n) ~= "number" then
                    local cost = Ring.ShotCost(odf)
                    n = cost and type(curAmmo) == "number" and math.floor(curAmmo / cost) or nil
                end
                api.SetOverlayCaption(texts["prefix" .. line], prefix or "")
                api.SetOverlayCaption(texts["weapon" .. line], Ring.Fit(weapon or "", layout.nameChars))
                api.SetOverlayCaption(texts["shots" .. line], n and Ring.ShotText(math.max(0, n)) or "")
                SetLineState(line, selected, n ~= nil and n <= 0)
                line = line + 1
            end
        end
        for slot = line, 4 do
            api.SetOverlayCaption(texts["prefix" .. slot], "")
            api.SetOverlayCaption(texts["weapon" .. slot], "")
            api.SetOverlayCaption(texts["shots" .. slot], "")
        end
    end

    function self.Destroy()
        if self.closed then return end
        self.closed = true
        for part, hidden in pairs(self.stockHidden) do
            if hidden then api.SetStockStatusHudVisible(part, true) end
        end
        hull.Destroy()
        ammo.Destroy()
        for _, element in pairs(texts) do
            if api.HasOverlayElement(element) then api.DestroyOverlayElement(element) end
        end
        if plate and api.HasOverlayElement(plate) then api.DestroyOverlayElement(plate) end
        api.DestroyOverlay(overlay)
        if api.HasOverlayElement(root) then api.DestroyOverlayElement(root) end
        if back and api.HasOverlayElement(back) then api.DestroyOverlayElement(back) end
    end

    self.hull, self.ammo = hull, ammo
    self.Layout()
    return self
end

return Ring
