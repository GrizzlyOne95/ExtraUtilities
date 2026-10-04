-- Host checks for Workshop/exu_ring_gauge.lua against a fake exu table.
-- Run from the repository root: lua tests/host/ring_gauge_test.lua
package.path = "Workshop/?.lua;" .. package.path
local Ring = require("exu_ring_gauge")

local function near(a, b) return math.abs(a - b) < 1e-6 end

-- Fill rule: ceil(ratio * n) segments, the boundary one blended.
do
    local on, off = { 1, 1, 1, 1 }, { 0, 0, 0, 0 }
    local c = Ring.SegmentColors(0.25, 10, on, off)
    assert(c[2] == on and near(c[3][1], 0.5) and c[4] == off, "quarter fill")
    c = Ring.SegmentColors(1, 10, on, off)
    assert(c[10] == on or near(c[10][1], 1), "full fill")
    c = Ring.SegmentColors(0, 10, on, off)
    for i = 1, 10 do assert(c[i] == off, "empty fill") end
    c = Ring.SegmentColors(2, 10, on, off)
    assert(near(c[10][1], 1), "ratio clamps high")
    local h = Ring.HealthColor(0.3)
    assert(h[1] == 1 and h[2] == 1 and h[3] == 0, "yellow band")
end

-- Fake EXU: records elements, materials, rotations and tints.
local function FakeApi()
    local api = { elements = {}, materials = { ["EXU_HUD/RingSegment"] = {}, ["EXU_HUD/StatusPlate"] = {} },
        overlays = {}, tintCalls = 0, clones = 0 }
    function api.MaterialExists(n) return api.materials[n] ~= nil end
    function api.CloneMaterial(src, n)
        if not api.materials[src] or api.materials[n] then return false end
        api.materials[n] = { rotate = {} }
        api.clones = api.clones + 1
        return true
    end
    function api.SetMaterialTextureRotate(n, r, technique)
        assert(not api.materials[n].drawn, "material edited after first use")
        api.materials[n].rotate[technique or 0] = r
        return true
    end
    function api.SetMaterialPassColors(n, colors, technique, pass)
        assert(colors.diffuse.r and colors.diffuse.a, "keyed colour required")
        assert(technique == -1 and pass == -1, "tint every technique")
        assert(not api.materials[n].drawn, "material edited after first use")
        api.materials[n].diffuse = colors.diffuse
        api.tintCalls = api.tintCalls + 1
        return true
    end
    function api.HasOverlayElement(n) return api.elements[n] ~= nil end
    function api.CreateOverlayElement(t, n) assert(not api.elements[n]); api.elements[n] = { type = t } end
    function api.DestroyOverlayElement(n) api.elements[n] = nil end
    function api.SetOverlayMetricsMode(n, m) api.elements[n].metrics = m end
    function api.SetOverlayMaterial(n, m)
        assert(api.materials[m])
        api.elements[n].material = m
        api.materials[m].drawn = true
    end
    function api.AddOverlayElementChild(p, n) assert(api.elements[p] and api.elements[n]); api.elements[n].parent = p end
    function api.RemoveOverlayElementChild(p, n) api.elements[n].parent = nil end
    function api.SetOverlayPosition(n, x, y) api.elements[n].x, api.elements[n].y = x, y end
    function api.SetOverlayDimensions(n, w, h) api.elements[n].w, api.elements[n].h = w, h end
    function api.SetOverlayParameter(n, k, v) api.elements[n][k] = v; return true end
    function api.SetOverlayTextFont(n, f) api.elements[n].font = f; return true end
    function api.SetOverlayTextColor(n, r, g, b, a) api.elements[n].color = { r, g, b, a } end
    function api.SetOverlayTextCharHeight(n, c) api.elements[n].char = c end
    function api.SetOverlayCaption(n, s) api.elements[n].caption = s end
    function api.CreateOverlay(n) api.overlays[n] = { shown = false } end
    function api.DestroyOverlay(n) api.overlays[n] = nil end
    function api.SetOverlayZOrder(n, z) api.overlays[n].z = z end
    function api.AddOverlay2D(o, c)
        local overlay = api.overlays[o]
        overlay.roots = overlay.roots or {}
        table.insert(overlay.roots, c)
        overlay.root = c
    end
    function api.ShowOverlay(n) api.overlays[n].shown = true end
    function api.HideOverlay(n) api.overlays[n].shown = false end
    function api.GetGameResolution() return 1920, 1080 end
    function api.IsGameUiOpen() return false end
    api.stock = { hull = true, ammo = true, weapons = true }
    function api.SetStockStatusHudVisible(part, visible) api.stock[part] = visible; return true end
    return api
end

-- Ring geometry: each segment panel is centred on the ring centre, stretched
-- by the aspect, and its material rotates segment i to start + step * (i - 0.5).
do
    local api = FakeApi()
    api.CreateOverlayElement("Panel", "root")
    local ring = Ring.New(api, { name = "g", parent = "root", x = 300, y = 400, radius = 100,
        start = 270, step = -9, aspect = 2 })
    local first = api.elements["g/seg1"]
    assert(first.x == 100 and first.y == 300 and first.w == 400 and first.h == 200, "stretched panel")
    local mat = api.materials[first.material]
    for t = 0, 2 do
        assert(near(mat.rotate[t], math.rad(270 - 4.5 - 90)), "segment rotation on every technique")
    end
    assert(near(api.materials[api.elements["g/seg10"].material].rotate[0], math.rad(180 + 4.5 - 90)), "last rotation")
    local clones = api.clones
    ring.SetRatio(0)
    assert(api.clones == clones, "unchanged colours do not reclone")
    ring.SetRatio(0.5)
    assert(api.elements["g/seg5"].material ~= api.elements["g/seg6"].material, "lit and unlit differ")
    ring.SetRatio(0)
    ring.SetRatio(0.5)
    local reused = api.clones
    ring.SetRatio(0)
    assert(api.clones == reused, "variants are reused, never edited")
    -- Fade quantised to quarters: 0.53 puts segment 6 at 0.3 -> 0.25.
    local c = Ring.SegmentColors(0.53, 10, { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, 4)
    assert(near(c[6][1], 0.25), "quantised fade")
    ring.Destroy()
    assert(not api.elements["g/seg1"], "destroy removes panels")
    -- A second mission reuses the cloned materials.
    Ring.New(api, { name = "g", parent = "root" })
end

-- Status cluster with a stubbed player.
do
    local api = FakeApi()
    _G.GetPlayerHandle = function() return "player" end
    _G.IsValid = function(h) return h == "player" end
    _G.GetCurHealth = function() return 990 end
    _G.GetMaxHealth = function() return 1000 end
    _G.GetCurAmmo = function() return 95 end
    _G.GetMaxAmmo = function() return 100 end
    _G.GetWeaponClass = function(_, slot)
        if slot == 0 or slot == 2 then return "gfafmsl\0\0" end
        return ""
    end
    _G.OpenODF = function(n) return n end
    _G.GetODFString = function(_, section, key)
        assert(section == "WeaponClass")
        if key == "wpnName" then return "FAF Msl", true end
        if key == "wpnCategory" then return "ROCK", true end
        return "", false
    end
    local status = Ring.NewStatus(api, { name = "st" })
    assert(not api.stock.hull and not api.stock.ammo and not api.stock.weapons, "stock readout hidden")
    status.Update()
    assert(api.overlays.st.shown, "shown with a player")
    assert(api.elements["st/hullText"].caption == "99%", "hull percent")
    assert(api.elements["st/ammoText"].caption == "95%", "ammo percent")
    assert(api.elements["st/prefix0"].caption == "R" and api.elements["st/weapon0"].caption == "FAF MSL", "weapon line")
    assert(api.elements["st/weapon1"].caption == "FAF MSL" and api.elements["st/weapon2"].caption == "", "lines pack")
    local hullTop = api.materials[api.elements["st/hull/seg10"].material].diffuse
    assert(hullTop.g > 0.8 and hullTop.r < 0.05, "hull green, last segment mostly lit")
    local plate = api.elements["st/plate"]
    assert(plate.material == "EXU_HUD/StatusPlate" and plate.x + plate.w <= 1920 and plate.y + plate.h <= 1080,
        "plate in the bottom-right corner")
    assert(plate.x > 1920 / 2 and plate.y > 1080 / 2, "plate bottom right")
    -- Root containers draw in insertion order; the plate's must come first.
    local roots = api.overlays.st.roots
    assert(#roots == 2 and roots[1] == plate.parent and roots[2] == api.elements["st/hullText"].parent,
        "plate under the gauge")
    -- Without GetSelectedWeaponMask every line stays lit.
    local lit = api.elements["st/weapon0"].color
    assert(lit == nil or lit[1] > 0.7, "lines lit without a selection mask")
    -- Slot 2 selected: line 1 (slot 2) lit, line 0 (slot 0) dimmed.
    function api.GetSelectedWeaponMask(h) assert(h == "player"); return 4 end
    status.Update()
    assert(api.elements["st/weapon1"].color[1] > 0.7, "selected weapon lit")
    assert(api.elements["st/weapon0"].color[1] < 0.5 and api.elements["st/prefix0"].color[1] < 0.5,
        "unselected weapon dimmed")
    -- Linked selection lights both.
    function api.GetSelectedWeaponMask() return 5 end
    status.Update()
    assert(api.elements["st/weapon0"].color[1] > 0.7 and api.elements["st/weapon1"].color[1] > 0.7, "linked lit")
    _G.IsValid = function() return false end
    status.Update()
    assert(not api.overlays.st.shown, "hidden without a player")
    status.Destroy()
    assert(api.overlays.st == nil and not api.elements["st/hullText"], "destroy cleans up")
    assert(api.stock.hull and api.stock.ammo and api.stock.weapons, "stock readout restored")
    -- Opting out leaves the stock readout alone; an older EXU without the call still works.
    local keep = FakeApi()
    Ring.NewStatus(keep, { name = "keep", hideStock = false })
    assert(keep.stock.hull, "hideStock = false keeps stock")
    local old = FakeApi()
    old.SetStockStatusHudVisible = nil
    Ring.NewStatus(old, { name = "old" })
end

print("ring gauge checks passed")
