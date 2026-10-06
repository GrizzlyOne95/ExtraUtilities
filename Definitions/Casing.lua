--- @meta exu
--- Physical shell casings for Extra Utilities (exu.casing).
---
--- Lua only spawns a casing; EXU then owns it. Every rendered frame (an
--- Ogre::FrameListener, not the Lua update) EXU applies gravity and spin,
--- bounces it off the terrain and nearby objects, lays it flat when it lands,
--- lets it rest for `linger` seconds, sinks it into the ground and destroys
--- it. The simulation freezes while the game is paused.
---
--- All vectors are simulation space, the same as GetPosition/GetTransform.
--- Purely local visuals: nothing is networked and nothing touches the
--- simulation, so it is safe (but not synchronised) in multiplayer.
---
--- Collision: terrain via the engine's Terrain::HeightAt plus a 4-sample
--- normal; objects as the class model box (oriented by the object's matrix)
--- or, without one, the entity bounding sphere. The casing itself is a sphere
--- of `radius` at its centre. The owner is ignored for its first 50 ms and
--- until the casing has been outside its box once.

--- @class ExuCasingQuaternion
--- @field w number
--- @field x number
--- @field y number
--- @field z number

--- @class ExuCasingSpawn
--- @field pos Vector Spawn position (required).
--- @field vel Vector? Initial velocity, m/s (default zero). Add the shooter's velocity yourself.
--- @field spin Vector? Initial world angular velocity, rad/s (default zero).
--- @field transform Matrix? Initial orientation: the casing's long axis follows transform.front.
--- @field orientation ExuCasingQuaternion? Initial orientation as a quaternion (wins over transform).
--- @field mesh string? Ogre mesh (default "casing.mesh"). A mesh that fails to load is refused for the rest of the mission.
--- @field material string? Material override for the whole entity.
--- @field scale number? Uniform scale (default 1; 0.01..50).
--- @field owner Handle? The firing object; ignored as an obstacle until the casing has cleared it.
--- @field linger number? Seconds to rest before sinking (default 6).
--- @field restitution number? Bounciness 0..0.95 (default 0.35).
--- @field friction number? 0..1 (default 0.4): fraction of sliding speed lost per bounce, Coulomb coefficient at rest.
--- @field radius number? Contact radius in metres (default 0.06 * scale).
--- @field sinkDepth number? How far it sinks before it is removed (default 3 * radius).

--- @class ExuCasingStats
--- @field live integer
--- @field flying integer
--- @field resting integer
--- @field sinking integer
--- @field max integer
--- @field spawned integer
--- @field recycled integer Casings retired early because the pool was full.
--- @field expired integer
--- @field failed integer
--- @field forgotten integer Casings dropped because their scene went away.
--- @field frames integer Rendered frames simulated.
--- @field obstacles integer Objects considered for collision this frame.
--- @field avgFrameMicros number
--- @field maxFrameMicros number

--- @class ExuCasingCapabilities
--- @field casings boolean exu.casing works on this build.
--- @field frameDriven boolean Simulated per rendered frame.
--- @field terrainCollision boolean
--- @field objectCollision boolean
--- @field objectShape string "box" (class model box, sphere fallback).
--- @field pauseAware boolean
--- @field max integer
--- @field hardMax integer

--- @class ExuCasingApi
local casing = {}

--- Spawns one casing. When the pool is full the least important casing
--- (sinking, then longest resting, then oldest) is retired to make room.
--- @param spec ExuCasingSpawn
--- @return integer|nil id
--- @return string? error
function casing.Spawn(spec) end

--- Sets the pool size (default 32, 0..256). Shrinking retires the oldest.
--- @param max integer
--- @return integer max
function casing.SetMax(max) end

--- @return integer max
function casing.GetMax() end

--- Destroys every casing now.
--- @return integer destroyed
function casing.Clear() end

--- @return integer live
function casing.GetCount() end

--- @return ExuCasingStats
function casing.GetStats() end

--- @return ExuCasingCapabilities
function casing.GetCapabilities() end

--- @class exu
--- @field casing ExuCasingApi
