--- @meta exu
--- Skinned gibs for Extra Utilities (exu.gibs).
---
--- When a person (pilot, soldier, zombie, or any ODF with classLabel "person",
--- which includes every wildlife ODF of ISDF Chronicles) is fully fragmented,
--- EXU cuts rigid per-limb pieces from the dead body's own skinned mesh, poses
--- each one where its limb was on the frame of death, hides the body, and
--- simulates the pieces: the legacy chunk launch, then a flesh ground response
--- (they thud, slide a little and lie still, rest for `linger` seconds, sink
--- 0.4 m into the ground over two seconds and disappear; 20 s at most). Torn
--- cuts are closed with flesh caps. The simulation freezes while the game is
--- paused.
---
--- The first death of each model pays for the mesh split (watch the `prepareMs`
--- line in exu.log); the result is cached under
--- %LOCALAPPDATA%\ExtraUtilities\cache\gibs\ and later missions are free.
---
--- Off by default: a mission opts in with Enable(true). Purely local visuals:
--- nothing is networked and nothing touches the simulation, so it is safe (but
--- not synchronised) in multiplayer.
---
--- OpenShim provides the same feature. When the OpenShim provider is loaded,
--- Enable(true) returns false and `GetCapabilities().openShim` is true: use
--- OpenShim's own SkinnedGibs setting there.

--- @class ExuGibsConfig
--- @field max integer? Pool size, 12..1024 (default 160). When full, the oldest settled gib is recycled.
--- @field linger number? Seconds a gib rests before sinking, 0..60 (default 8).
--- @field force number? Scale of the random launch speed, 0..5 (default 1).

--- @class ExuGibsStats
--- @field live integer Gibs currently shown.
--- @field settled integer Of those, at rest.
--- @field sinking integer Of those, sinking into the ground.
--- @field max integer
--- @field deaths integer Persons captured this mission.
--- @field spawned integer Gibs created.
--- @field recycled integer Gibs retired early because the pool was full.
--- @field failed integer Pieces that could not be created.
--- @field models integer Models split (or loaded from the cache) this mission.
--- @field forgotten integer Gibs dropped because their scene went away.

--- @class ExuGibsCapabilities
--- @field gibs boolean Skinned gibs can run on this install (qualified build, Ogre exports present, OpenShim not providing them).
--- @field openShim boolean The OpenShim provider is live; exu.gibs stands down for it.
--- @field hook boolean The death hook is installed and active.
--- @field reason string? Why gibs are not running. Absent while they are.

--- @class ExuGibsApi
local gibs = {}

--- Turns skinned gibs on (true) or off (false). Turning them on installs the
--- death hook, registers the payload resources and starts the frame listener;
--- turning them off removes the hook (gibs already on screen finish their
--- life). Returns whether gibs are now active; when they are not,
--- GetCapabilities().reason says why.
--- @param enable boolean
--- @return boolean active
function gibs.Enable(enable) end

--- Sets the pool size, rest time and launch force. Any field may be omitted;
--- values are clamped to their ranges. Applies to gibs spawned afterwards.
--- @param config ExuGibsConfig
function gibs.Configure(config) end

--- @return boolean enabled Whether the death hook is active.
function gibs.IsEnabled() end

--- Hides every gib now (the pooled objects are kept for reuse).
function gibs.Clear() end

--- @return ExuGibsStats
function gibs.GetStats() end

--- @return ExuGibsCapabilities
function gibs.GetCapabilities() end

--- @class exu
--- @field gibs ExuGibsApi
