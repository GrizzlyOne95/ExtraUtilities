-- Opt-in stock-engine stimuli for EXU runtime-event research in ISDFC isdftest.
-- This does not implement or emulate exu.events. Pair its labeled actions with
-- a qualified native observer; missing native observations are not a pass.
-- Copy beside isdftest.lua, then call New():Update() from mission Update.
-- No player possession/input changes. Stop() removes only this check's objects.
local Check = {}
Check.__index = Check

function Check.New(options)
    options = options or {}
    return setmetatable({ options = options, owned = {}, actions = {},
        nextAction = 1, stopped = false, emit = options.emit or print }, Check)
end

function Check:Log(label, h, detail)
    local health = h and IsValid(h) and GetCurHealth(h) or "invalid"
    self.emit(string.format("[EVTCHECK] time=%.3f action=%s handle=%s hp=%s %s",
        GetTime(), label, tostring(h), tostring(health), detail or ""))
end

function Check:Build(odf, team, x, z)
    local position = SetVector(x, 0, z)
    position.y = (GetTerrainHeightAndNormal(position) or 0) + 2
    local h = BuildObject(odf, team, position)
    assert(h and IsValid(h), "event check could not build " .. odf)
    self.owned[#self.owned + 1] = h
    SetIndependence(h, 0)
    self:Log("build:" .. odf, h)
    return h
end

function Check:Begin()
    local player = GetPlayerHandle()
    assert(player and IsValid(player), "event check needs a live player")
    local p = GetPosition(player)
    local x, z = p.x + 80, p.z + 80
    self.started = GetTime()
    local function person(dx)
        local h = self:Build("ispilo", 1, x + dx, z)
        SetMaxHealth(h, 100)
        SetCurHealth(h, 100)
        return h
    end
    local hurt, zero, fatal = person(0), person(8), person(16)
    local boarder = person(30)
    local empty = self:Build("ivscout", 0, x + 33, z)
    local hop = self:Build("ivscout", 1, x + 50, z)
    local occupant = self:Build("ivscout", 1, x + 65, z)
    local target = self:Build("ivscout", 2, x, z + 80)
    SetMaxHealth(target, 1000000)
    SetCurHealth(target, 1000000)
    for slot = 0, 4 do GiveWeapon(target, nil, slot) end
    local function at(delay, action)
        self.actions[#self.actions + 1] = { time = delay, run = action }
    end
    local function damage(label, h, amount)
        self:Log(label .. ":before", h, "incoming=" .. amount)
        Damage(h, amount)
        self:Log(label .. ":after", h)
    end
    local function shot(label, ammo)
        -- A fresh actor isolates each ammo case from prior AI/physics/removal.
        assert(IsValid(target), "event check target disappeared")
        local shooter = self:Build("isdoomx", 1, x, z + 35)
        for slot = 0, 4 do GiveWeapon(shooter, nil, slot) end
        assert(GiveWeapon(shooter, "gdbshot", 0), "event check needs gdbshot")
        SetWeaponMask(shooter, 1)
        SetMaxAmmo(shooter, 2000)
        SetCurAmmo(shooter, ammo)
        self:Log(label, shooter, "ammo=" .. GetCurAmmo(shooter))
        FireAt(shooter, target)
    end
    at(0.5, function() damage("nonfatal", hurt, 5) end)
    at(1.0, function() damage("healing", hurt, -3) end)
    at(1.5, function() damage("exact_zero", zero, 100) end)
    at(2.0, function() damage("below_zero", fatal, 101) end)
    at(2.5, function()
        damage("boarder_hurt", boarder, 5)
        self:Log("get_in_request", boarder, "vehicle=" .. tostring(empty))
        GetIn(boarder, empty)
        -- Exercise real contact now; AI navigation to the craft is a separate
        -- behavior and must not make this retirement fixture inconclusive.
        SetPosition(boarder, GetPosition(empty))
        SetVelocity(boarder, SetVector(0, -0.1, 0))
    end)
    at(4.0, function() shot("shotgun_full_1", 2000) end)
    at(5.5, function() shot("shotgun_full_2", 2000) end)
    at(7.0, function() shot("shotgun_partial", self.options.partialAmmo or 5) end)
    at(8.5, function() shot("shotgun_empty", 0) end)
    at(10.0, function()
        self:Log("hop_out_request", hop)
        HopOut(hop)
    end)
    at(11.0, function()
        self:Log("kill_pilot_request", occupant)
        KillPilot(occupant)
        self:Log("kill_pilot_return", occupant)
    end)
    at(13.0, function()
        self:Log("boarder_result", boarder, "vehicle=" .. tostring(empty))
        self:Log("complete")
        self:Stop()
    end)
end

function Check:Stop()
    if self.stopped then return end
    self.stopped = true
    -- Only exact handles created here; a retired boarder must not identify the
    -- new occupant or an unrelated object that reused its arena slot.
    for _, h in ipairs(self.owned) do
        if IsValid(h) then RemoveObject(h) end
    end
    self.owned, self.actions = {}, {}
end

function Check:Update()
    if self.stopped then return end
    local ok, err = pcall(function()
        if not self.started then
            -- Leave time to attach the native observer after range startup.
            self.readyAt = self.readyAt or (GetTime() + (self.options.delay or 20))
            if GetTime() < self.readyAt then return end
            self:Begin()
        end
        local elapsed = GetTime() - self.started
        while not self.stopped do
            local action = self.actions[self.nextAction]
            if not action or elapsed < action.time then break end
            self.nextAction = self.nextAction + 1
            action.run()
        end
    end)
    if not ok then
        self.emit("[EVTCHECK] FAIL " .. tostring(err))
        self:Stop()
    end
end

return Check
