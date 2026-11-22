-- In-game check for the Producer + scrap functions (exu.CancelBuild ... exu.GetCurrentWhere).
--
-- Prints results only (prefix "[PRODCHK] "); nothing asserts. Needs a mission with the
-- player's (team 1) Recycler deployed. Not shipped: copy it next to a test mission, then
--
--     local check = require("bzx_producer_check").New(exu)
--     function Update() check:Update() end
--
-- What to verify: step 1 shows the recycler building "avscav" with a time left that drops;
-- step 2 sets it to 1 s; step 3 cancels it (true) and the build is gone; step 4 finishes a
-- fresh build and prints a handle; the scavenger lines show cur/max change and clamp; the
-- command lines show the Build param "avscav" and a Goto path destination.
-- Lua 5.1; uses no io/os/debug.

local M = {}
M.__index = M

function M.New(exu, options)
	options = options or {}
	return setmetatable({ exu = exu, print = options.print or print, frame = 0, step = 0 }, M)
end

function M:Log(...)
	local parts = {}
	for i = 1, select("#", ...) do parts[#parts + 1] = tostring((select(i, ...))) end
	self.print("[PRODCHK] " .. table.concat(parts, " "))
end

function M:Update()
	local exu = self.exu
	self.frame = self.frame + 1
	if self.frame % 20 ~= 0 then return end -- a few sim updates between steps
	local rec = GetRecyclerHandle(1)
	self.step = self.step + 1
	local s = self.step
	if s == 1 then
		self:Log("recycler", rec, "deployed", IsDeployed(rec))
		self:Log("nil/invalid:", exu.GetBuildTimeLeft(GetPlayerHandle()), exu.GetBuildingOdf(GetPlayerHandle()),
			exu.CancelBuild(GetPlayerHandle()), exu.FinishBuild(GetPlayerHandle()), exu.GetCurScrapHold(rec))
		SetScrap(1, 100)
		Build(rec, "avscav", 1)
	elseif s == 2 then
		self:Log("building", exu.GetBuildingOdf(rec), "left", exu.GetBuildTimeLeft(rec),
			"param", exu.GetCurrentParam(rec), "when", exu.GetCurrentWhen(rec), "cmd", GetCurrentCommand(rec))
		self:Log("set 1s", exu.SetBuildTimeLeft(rec, 1), exu.GetBuildTimeLeft(rec),
			"bad", exu.SetBuildTimeLeft(rec, -1), exu.SetBuildTimeLeft(rec, 0 / 0))
	elseif s == 3 then
		self:Log("scrap before cancel", GetScrap(1), "cancel", exu.CancelBuild(rec), "scrap after", GetScrap(1),
			"odf now", exu.GetBuildingOdf(rec), "cancel again", exu.CancelBuild(rec))
		Build(rec, "avscav", 1)
	elseif s == 4 then
		local built = exu.FinishBuild(rec)
		self:Log("finish", built, built and GetOdf(built), "odf now", exu.GetBuildingOdf(rec))
		self.scav = built
	elseif s == 5 and self.scav and IsValid(self.scav) then
		local h = self.scav
		self:Log("scav cur/max", exu.GetCurScrapHold(h), exu.GetMaxScrapHold(h))
		self:Log("set cur 3", exu.SetCurScrapHold(h, 3), exu.GetCurScrapHold(h),
			"set cur 9999", exu.SetCurScrapHold(h, 9999), exu.GetCurScrapHold(h))
		self:Log("set max 2", exu.SetMaxScrapHold(h, 2), exu.GetMaxScrapHold(h), exu.GetCurScrapHold(h),
			"bad max 0", exu.SetMaxScrapHold(h, 0))
		local p = GetPosition(h)
		Goto(h, SetVector(p.x + 60, p.y, p.z + 60), 1)
	elseif s == 6 and self.scav and IsValid(self.scav) then
		local name, dest = exu.GetCurrentWhere(self.scav)
		self:Log("goto where", name, dest and dest.x, dest and dest.y, dest and dest.z,
			"when", exu.GetCurrentWhen(self.scav), "param", exu.GetCurrentParam(self.scav))
		self:Log("done")
	end
end

return M
