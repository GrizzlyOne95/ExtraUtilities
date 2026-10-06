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
--- @field localFirstPersonTarget boolean True when either the optional OpenShim resolver or EXU's native BZR resolver is available.
--- @field animationInventory boolean True when `List` is available.
--- @field pilotStateInspection boolean True when the read-only local Person FSM snapshot API is compiled in.
--- @field pilotFsmIntercept boolean True when the verified observe-only Person::Simulate entry detour is active.
--- @field pilotAnimationOverrides boolean True when `exu.fps.SetPilotAnimationProfile` can apply non-stock pilot animation overrides in this session: the build supports them AND the Person::Simulate seam is active with its native clip tables qualified. Overrides are still single player only (the setter errors in multiplayer).
--- @field firstPersonLayers boolean True when `exu.fps.SetLayer` layers can be advanced: the Person::Simulate seam is active. Presentation-only, so also true in multiplayer.
--- @field personLongClips boolean True when `exu.animation.SetPersonLongClips` can let long run/idle clips play: the Person::Simulate seam is active and its clip table entries qualified. Presentation-only, so also true in multiplayer.
--- @field transitionBlend boolean True when `exu.fps.SetTransitionBlend` can cross-fade pilot FSM clip switches: the Person::Simulate seam is active. Presentation-only, so also true in multiplayer.
--- @field managedClock boolean False while Redux/Ogre remains responsible for advancing the clips it plays (EXU clocks only `exu.fps.SetLayer` layers).
--- @field nativeAdvancement "unvalidated"|string
--- @field firstPersonStatus string

--- @class ExuAnimationApi
local animation = {}

--- Wraps a normal BZR handle in an explicit animation-target descriptor.
--- All animation functions also accept a raw Handle directly, so this helper is optional.
--- @param h Handle
--- @return ExuAnimationTarget
function animation.Target(h) end

--- Resolves the local first-person pilot entity (`aspilo_fp`) as an animation
--- target. OpenShim remains the preferred resolver when installed; otherwise EXU
--- resolves the current Person render bridge directly. The Ogre pointer is
--- re-resolved on every operation and never cached. See Docs/ANIMATION_API.md.
--- @return ExuAnimationTarget
function animation.TargetLocalFirstPerson() end

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

--- Returns a deterministic name-sorted snapshot of every Ogre AnimationState
--- currently exposed by the target. Returns nil when the target or state set
--- cannot be resolved; a valid target with no states returns an empty array.
--- @nodiscard
--- @param target Handle|ExuAnimationTarget
--- @return ExuAnimationInfo[]|nil
function animation.List(target) end

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

--- @class ExuPersonLongClipsOptions
--- @field runs boolean? Let run clips (runForward/Backward/Left/Right) longer than 0.967 s keep looping. Default true.
--- @field idle boolean? Loop a WORLD idle longer than 0.967 s instead of freezing it. Default true.

--- @class ExuPersonLongClips
--- @field runs boolean
--- @field idle boolean
--- @field available boolean The Person::Simulate seam is active and the clip table entries qualified.
--- @field faulted boolean A table write faulted; off for this Lua state.
--- @field raisedCalls integer Person::Simulate calls made with a raised end time this mission.
--- @field idleLoops integer Long idles looped this mission.

--- Lets every Person (pilots, creatures) play clips longer than the engine's hardcoded
--- end time. Person::Simulate stops advancing a clip once timePosition + dt*rate reaches
--- 0.967 s for idle and the runs, looped or not, so a long walk cycle freezes mid-stride
--- and the body slides. EXU raises that end time around each Simulate call of a Person
--- playing such a clip and restores it right after; short stock clips are unaffected.
--- The kneel, death and air clips keep their stock timing (they drive the FSM).
--- Presentation only, so also in multiplayer. Mission-scoped; off by default.
--- `true` = both on; a table replaces the setting (omitted keys true); nil/false = stock.
--- See Docs/Research/PERSON_LONG_CLIPS_RE_20261005.md.
--- @param options ExuPersonLongClipsOptions|boolean|nil
--- @return boolean available
function animation.SetPersonLongClips(options) end

--- Returns the long-clip settings and live counters.
--- @nodiscard
--- @return ExuPersonLongClips
function animation.GetPersonLongClips() end

--- @class exu
--- @field animation ExuAnimationApi
