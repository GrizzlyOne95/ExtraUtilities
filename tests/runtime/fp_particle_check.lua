-- In-game check for first-person particles (exu.fps.AttachParticleToBone).
--
-- Creates one managed particle system, keeps it attached to a bone of the
-- local pilot's first-person entity every Update, emits while the trigger is
-- held (or always, with alwaysEmit), and prints every state change:
-- capability, attach failures, re-attaches (respawn, leaving a vehicle) and
-- the target generation. Output lines are prefixed "[FPPART] ".
--
-- This file is not shipped. Copy it next to a test mission script, then:
--
--     local check = require("fp_particle_check").New(exu,
--         { template = "<an Ogre particle template>", bone = "asp11GC1" })
--     function Update()
--         check:Update()
--     end
--
-- What to verify: the effect sits at the muzzle and follows the gun through
-- idle, run, crouch and EXU layers; it disappears in a vehicle and comes back
-- (with a "reattached" line) after hopping out or respawning; no crash at
-- mission end. check:Stop() detaches and destroys the system.
--
-- Options (second argument to New):
--   template    particle template (required)
--   bone        FP bone (default "asp11GC1", the stock American muzzle)
--   offset      { x, y, z } in the bone frame (default { 0, 0, 0 })
--   name        system name (default "exu_fp_particle_check")
--   alwaysEmit  emit regardless of the trigger (default false)
--   print       injectable (default the global)
--
-- Lua 5.1; uses no io/os/debug.

local M = {}
M.__index = M

function M.New(exu, options)
	options = options or {}
	local self = setmetatable({}, M)
	self.exu = exu
	self.print = options.print or print
	self.name = options.name or "exu_fp_particle_check"
	self.bone = options.bone or "asp11GC1"
	self.offset = options.offset or { 0, 0, 0 }
	self.alwaysEmit = options.alwaysEmit == true
	self.supported = exu.fps.GetCapabilities().firstPersonParticles == true
	self.created = false
	self.lastAttached = nil
	self.lastEmitting = nil
	self:Log("capability firstPersonParticles=" .. tostring(self.supported))
	if not options.template then
		self:Log("no template option given; nothing to do")
		self.supported = false
		return self
	end
	if self.supported then
		self.created = exu.CreateParticleSystem(self.name, options.template) == true
		self:Log("CreateParticleSystem(" .. options.template .. ")=" .. tostring(self.created))
		if self.created then
			exu.SetParticleSystemEmitting(self.name, false)
		end
	end
	return self
end

function M:Log(text)
	self.print("[FPPART] " .. text)
end

function M:Update()
	if not (self.supported and self.created) then
		return
	end
	local exu = self.exu
	local o = self.offset
	local attached, reattached = exu.fps.AttachParticleToBone(self.name, self.bone, o[1], o[2], o[3])
	if attached ~= self.lastAttached then
		self:Log("attached=" .. tostring(attached) .. " bone=" .. self.bone)
		self.lastAttached = attached
	end
	if reattached then
		self:Log("reattached generation=" .. tostring(exu.fps.GetParticleTargetGeneration()))
	end
	local emitting = attached and (self.alwaysEmit or exu.fps.IsTriggerHeld())
	if emitting ~= self.lastEmitting then
		exu.SetParticleSystemEmitting(self.name, emitting)
		self.lastEmitting = emitting
	end
end

function M:Stop()
	if self.created then
		self:Log("DetachParticle=" .. tostring(self.exu.fps.DetachParticle(self.name)))
		self.exu.DestroyParticleSystem(self.name)
		self.created = false
	end
end

return M
