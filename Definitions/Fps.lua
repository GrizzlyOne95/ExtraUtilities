--- @meta exu
--- Local first-person/pilot animation convenience API for Extra Utilities.
--- This file augments Definitions/ExtraUtils.lua; it is editor metadata only.

--- @class ExuFpsApi
local fps = {}

--- Returns true only when the local first-person animation target can be
--- resolved right now. This can change as the player enters/leaves pilot mode
--- or across mission/scene transitions.
--- @nodiscard
--- @return boolean
function fps.IsAvailable() end

--- Returns the underlying animation capability/status table used by the FPS
--- facade. The facade does not have a separate resolver or animation runtime.
--- @nodiscard
--- @return ExuAnimationCapabilities
function fps.GetCapabilities() end

--- Returns a deterministic name-sorted snapshot of every Ogre AnimationState
--- exposed by the current local first-person target. Returns nil when the
--- target is unavailable or enumeration fails.
--- @nodiscard
--- @return ExuAnimationInfo[]|nil
function fps.ListAnimations() end

--- Returns whether the current local first-person target exposes a named
--- animation state.
--- @nodiscard
--- @param name string
--- @return boolean
function fps.HasAnimation(name) end

--- Returns current state for a named local first-person animation, or nil when
--- the target/state cannot be resolved.
--- @nodiscard
--- @param name string
--- @return ExuAnimationInfo|nil
function fps.GetInfo(name) end

--- Enables and configures a named local first-person Ogre animation state.
--- EXU does not install a separate animation clock.
--- @param name string
--- @param options ExuAnimationPlayOptions?
--- @return boolean success
function fps.Play(name, options) end

--- Disables a named local first-person animation. When reset is true, its time
--- is also returned to 0.
--- @param name string
--- @param reset boolean?
--- @return boolean success
function fps.Stop(name, reset) end

--- Seeks to time 0 and enables a named local first-person animation without
--- altering loop or weight.
--- @param name string
--- @return boolean success
function fps.Restart(name) end

--- Sets the named local first-person animation time position in seconds.
--- @param name string
--- @param timePosition number Non-negative seconds.
--- @return boolean success
function fps.Seek(name, timePosition) end

--- @class exu
--- @field fps ExuFpsApi
