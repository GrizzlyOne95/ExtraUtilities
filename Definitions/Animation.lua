--- @meta exu
--- High-level animation API definitions for Extra Utilities.
--- This file augments Definitions/ExtraUtils.lua; it is editor metadata only.

--- @class ExuAnimationTarget
--- @field kind "gameObject"|"localFirstPerson"
--- @field handle Handle? Present for `gameObject` targets.

--- @class ExuAnimationPlayOptions
--- @field restart boolean? Reset the animation time to 0 before enabling it. Defaults to true.
--- @field loop boolean? Set the Ogre AnimationState loop flag. Defaults to false.
--- @field weight number? Animation blend weight in [0, 1]. Defaults to 1.

--- @class ExuAnimationInfo
--- @field name string
--- @field targetKind "gameObject"|"localFirstPerson"
--- @field enabled boolean
--- @field loop boolean
--- @field weight number
--- @field timePosition number
--- @field length number
--- @field normalizedTime number Current time divided by animation length when length is non-zero.
--- @field atEnd boolean True when a non-looping animation has reached or exceeded its reported length.

--- @class ExuAnimationCapabilities
--- @field gameObjectTarget boolean
--- @field localFirstPersonTarget boolean False until the `aspilo_fp` ownership/resolver path is validated live.
--- @field managedClock boolean False while Redux/Ogre remains responsible for animation time advancement.
--- @field nativeAdvancement "unvalidated"|string
--- @field firstPersonStatus string

--- @class ExuAnimationApi
local animation = {}

--- Wraps a normal BZR handle in an explicit animation-target descriptor.
--- All animation functions also accept a raw Handle directly, so this helper is optional.
--- @param h Handle
--- @return ExuAnimationTarget
function animation.Target(h) end

--- Returns the currently implemented target/clock capabilities.
--- @nodiscard
--- @return ExuAnimationCapabilities
function animation.GetCapabilities() end

--- Returns whether the target exposes a named Ogre AnimationState.
--- Unsupported target kinds fail closed and return false.
--- @nodiscard
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @return boolean
function animation.Has(target, name) end

--- Returns current state for a named animation, or nil when the target/state cannot be resolved.
--- @nodiscard
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @return ExuAnimationInfo|nil
function animation.GetInfo(target, name) end

--- Enables and configures a named Ogre animation state.
--- EXU does not install a separate animation clock; Redux/Ogre owns time advancement.
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @param options ExuAnimationPlayOptions?
--- @return boolean success
function animation.Play(target, name, options) end

--- Disables a named animation. When reset is true, its time is also returned to 0.
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @param reset boolean?
--- @return boolean success
function animation.Stop(target, name, reset) end

--- Seeks to time 0 and enables a named animation without altering loop or weight.
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @return boolean success
function animation.Restart(target, name) end

--- Enables or disables a named animation state.
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @param enabled boolean
--- @return boolean success
function animation.SetEnabled(target, name, enabled) end

--- Changes the Ogre loop flag for a named animation state.
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @param loop boolean
--- @return boolean success
function animation.SetLoop(target, name, loop) end

--- Sets animation blend weight. The high-level API accepts values in [0, 1].
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @param weight number
--- @return boolean success
function animation.SetWeight(target, name, weight) end

--- Sets the named animation time position in seconds.
--- @param target Handle|ExuAnimationTarget
--- @param name string
--- @param timePosition number Non-negative seconds.
--- @return boolean success
function animation.Seek(target, name, timePosition) end

--- @class exu
--- @field animation ExuAnimationApi
