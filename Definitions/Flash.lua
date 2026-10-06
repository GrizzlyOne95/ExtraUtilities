--- @meta exu
--- BZ2-style 3D muzzle flashes for Extra Utilities (exu.flash).
---
--- Battlezone II drew a weapon's flashName as a draw_geom mesh (g_sflash: two
--- crossed cards along the barrel; g_fflash: a forward cone) placed on the
--- muzzle for flashDuration seconds. Redux's own draw_geom does not work, so
--- EXU draws it: Lua spawns a flash (typically from exu.BulletInit, with the
--- shot transform); EXU then owns it. Every rendered frame (an
--- Ogre::FrameListener) it is carried along with `owner`, scaled from
--- startScale to finishScale, and destroyed when `duration` seconds of
--- SIMULATION time have passed (it freezes while paused).
---
--- The mesh's local -Z is the muzzle's front (EXU mirrors render Z against
--- simulation Z), so a flash mesh points down -Z. Purely local visuals: nothing
--- is networked and nothing touches the simulation.
---
--- The timing and pose policy (src/Game/MuzzleFlashCore.h) is shared with
--- BZR-OpenShim's native weapon presentation.

--- @class ExuFlashSpawn
--- @field transform Matrix Muzzle pose (required): posit is the muzzle, front the barrel direction.
--- @field mesh string Ogre mesh (required). A mesh that fails to load is refused for the rest of the mission.
--- @field material string? Material override for the whole entity.
--- @field owner Handle? The firing object; the flash keeps its pose relative to it. If it dies the flash finishes in place.
--- @field duration number? Seconds of simulation time (default 0.1; 0.005..5).
--- @field startScale number? Uniform scale at spawn (default 1; BZ2 startRadius).
--- @field finishScale number? Uniform scale at the end (default startScale; BZ2 finishRadius).
--- @field roll number? Radians about the barrel axis (default 0). Pass a random roll per shot for variety.

--- @class ExuFlashStats
--- @field live integer
--- @field max integer
--- @field spawned integer
--- @field recycled integer Flashes retired early because the pool was full.
--- @field expired integer
--- @field failed integer
--- @field forgotten integer Flashes dropped because their scene went away.
--- @field orphaned integer Flashes whose owner died first.
--- @field frames integer Rendered frames updated.
--- @field avgFrameMicros number
--- @field maxFrameMicros number

--- @class ExuFlashCapabilities
--- @field flash boolean exu.flash works on this build.
--- @field followsOwner boolean
--- @field pauseAware boolean
--- @field max integer
--- @field hardMax integer

--- @class ExuFlashApi
local flash = {}

--- Spawns one flash. When the pool is full the flash nearest its end is retired.
--- @param spec ExuFlashSpawn
--- @return integer|nil id
--- @return string? error
function flash.Spawn(spec) end

--- Sets the pool size (default 48, 0..256). Shrinking retires the oldest.
--- @param max integer
--- @return integer max
function flash.SetMax(max) end

--- @return integer max
function flash.GetMax() end

--- Destroys every flash now.
--- @return integer destroyed
function flash.Clear() end

--- @return integer live
function flash.GetCount() end

--- @return ExuFlashStats
function flash.GetStats() end

--- @return ExuFlashCapabilities
function flash.GetCapabilities() end

--- @class exu
--- @field flash ExuFlashApi
