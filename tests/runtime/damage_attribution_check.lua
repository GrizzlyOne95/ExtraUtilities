-- Opt-in isdftest stimuli for native damage-attribution research.
-- Pair with a guarded native observer. Logs alone do not prove attribution.
-- Creates its own actors; never changes player possession or input.
local Check = {}
Check.__index = Check

function Check.New(options)
    options = options or {}
    return setmetatable({ options = options, owned = {}, actions = {}, cases = {},
        nextAction = 1, stopped = false, emit = options.emit or print }, Check)
end

function Check:Log(label, h, detail)
    self.emit(string.format("[DMGCHECK] time=%.3f action=%s handle=%s hp=%s %s",
        GetTime(), label, tostring(h),
        tostring(h and IsValid(h) and GetCurHealth(h) or "invalid"), detail or ""))
end

function Check:Build(team, x, z, health)
    local p = SetVector(x, 0, z)
    p.y = (GetTerrainHeightAndNormal(p) or 0) + 2
    local h = BuildObject("ivscout", team, p)
    assert(h and IsValid(h), "damage check could not build ivscout")
    self.owned[#self.owned + 1] = h
    SetIndependence(h, 0)
    for slot = 0, 4 do GiveWeapon(h, nil, slot) end
    SetMaxHealth(h, health)
    SetCurHealth(h, health)
    return h
end

function Check:Begin()
    local player = GetPlayerHandle()
    assert(player and IsValid(player), "damage check needs a live player")
    local origin = GetPosition(player)
    local x, z = origin.x + 150, origin.z + 150
    self.started = GetTime()
    local function at(delay, action)
        self.actions[#self.actions + 1] = { time = delay, run = action }
    end
    local function shot(label, dx, distance, weapon, ammo, health, owned)
        local target = self:Build(2, x + dx, z + distance, health)
        local shooter = self:Build(1, x + dx, z, 100000)
        assert(GiveWeapon(shooter, weapon, 0), "damage check needs " .. weapon)
        SetWeaponMask(shooter, 1)
        SetMaxAmmo(shooter, ammo)
        SetCurAmmo(shooter, ammo) -- one projectile, no sustained AI firing
        if owned then
            local owner = self:Build(1, x + dx, z - 30, 100000)
            SetOwner(shooter, owner)
            self:Log(label .. ":owner", owner)
            assert(GetOwner(shooter) == owner, "damage check owner assignment failed")
        end
        local p, q = GetPosition(shooter), GetPosition(target)
        SetTransform(shooter, BuildDirectionalMatrix(p,
            SetVector(q.x - p.x, q.y - p.y, q.z - p.z)))
        self:Log(label .. ":target", target)
        self:Log(label .. ":fire_request", shooter,
            "target=" .. tostring(target) .. " weapon=" .. weapon)
        SetTarget(shooter, target)
        Attack(shooter, target)
        FireAt(shooter, target)
        self.cases[#self.cases + 1] = { label = label, target = target, shooter = shooter }
        return shooter
    end
    local orphan
    at(0.5, function() shot("direct", 0, 25, "gatstb1", 14, 10000, false) end)
    -- This is an explicit Lua-assigned owner, not a naturally deployed weapon.
    at(3.5, function() shot("assigned_owner", 45, 25, "gatstb1", 14, 10000, true) end)
    at(6.5, function() shot("fatal", 90, 25, "gatstb1", 14, 100, false) end)
    at(9.5, function()
        orphan = shot("removed_shooter", 135, 100, "gianbaz", 12, 10000, false)
    end)
    at(9.8, function()
        self:Log("removed_shooter:remove", orphan)
        if IsValid(orphan) then RemoveObject(orphan) end
    end)
    at(17.0, function()
        for _, case in ipairs(self.cases) do
            self:Log(case.label .. ":result", case.target,
                "shooter=" .. tostring(case.shooter))
        end
        self:Log("complete")
        self:Stop()
    end)
end

function Check:Stop()
    if self.stopped then return end
    self.stopped = true
    for _, h in ipairs(self.owned) do
        if IsValid(h) then RemoveObject(h) end
    end
    self.owned, self.actions, self.cases = {}, {}, {}
end

function Check:Update()
    if self.stopped then return end
    local ok, err = pcall(function()
        if not self.started then
            self.readyAt = self.readyAt or (GetTime() + (self.options.delay or 15))
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
        self.emit("[DMGCHECK] FAIL " .. tostring(err))
        self:Stop()
    end
end

return Check
