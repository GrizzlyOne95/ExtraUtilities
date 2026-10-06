-- In-game check for exu.pathing (Docs/PATH_BLOCK.md).
--
-- Prints the capabilities, the mode of every object whose ODF opts in, and an
-- ASCII grid around each of them, once on the first Update and again whenever
-- a flagged object appears. Output lines are prefixed "[PATHBLK] ".
--
-- This file is not shipped. Copy it next to a test mission script, then:
--
--     local check = require("pathblock_check").New(exu)
--     function Update()
--         check:Update()
--     end
--
-- What to verify: hookedSites is 8; a flagged tunnel (bbsubtun with
-- pathBlock = "faces") shows its corridor as '.' with '#' either side; a
-- unit ordered through the tunnel (Goto to a point beyond it) drives
-- through instead of around. check:Dump(h) dumps one object on demand.
--
-- Options (second argument to New): radius (default 40), print.
--
-- Lua 5.1; uses no io/os/debug.

local M = {}
M.__index = M

function M.New(exu, options)
	options = options or {}
	local self = setmetatable({}, M)
	self.exu = exu
	self.print = options.print or print
	self.radius = options.radius or 40
	self.seen = {}
	self.started = false
	return self
end

function M:Say(text)
	for line in tostring(text):gmatch("[^\n]+") do
		self.print("[PATHBLK] " .. line)
	end
end

function M:Dump(h)
	local mode, info = self.exu.pathing.GetMode(h)
	if not mode then
		self:Say("GetMode: nil (dead handle or unsupported build)")
		return
	end
	self:Say(string.format("%s: %s (configured %s) cellsInBox=%d cellsBlocked=%d triangles=%d inGrid=%s",
		info.odf, mode, info.configured, info.cellsInBox, info.cellsBlocked, info.triangles, tostring(info.inGrid)))
	self:Say(self.exu.pathing.DumpGrid(h, self.radius) or "DumpGrid: nil")
end

function M:Update()
	local pathing = self.exu and self.exu.pathing
	if not pathing then
		if not self.started then
			self:Say("exu.pathing is missing")
			self.started = true
		end
		return
	end
	if not self.started then
		self.started = true
		local caps = pathing.GetCapabilities()
		self:Say(string.format("pathBlock=%s hookedSites=%d/%d gridReady=%s cellSize=%.2f enabled=%s",
			tostring(caps.pathBlock), caps.hookedSites, caps.hookSites, tostring(caps.gridReady),
			caps.cellSize, tostring(caps.enabled)))
		self:Say("Refresh -> " .. tostring(pathing.Refresh()))
	end
	for h in AllObjects() do
		if not self.seen[h] then
			self.seen[h] = true
			local mode, info = pathing.GetMode(h)
			if mode and info.configured ~= "box" then
				self:Dump(h)
			end
		end
	end
end

return M
