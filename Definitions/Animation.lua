--- @meta exu
--- High-level animation API definitions for Extra Utilities.
--- This file augments Definitions/ExtraUtils.lua; it is editor metadata only.

--- @class ExuAnimationTarget
--- @field kind "gameObject"|"localFirstPerson"|"cockpit"
--- @field handle Handle? Present for `gameObject` and `cockpit` targets.

--- @class ExuAnimationPlayOptions
--- @field restart boolean? Reset the animation time to 0 before enabling it. Defaults to true.
--- @field loop boolean? Set the Ogre AnimationState loop flag. Defaults to false.
--- @field weight number? Animation blend weight in [0, 1]. Defaults to 1.

--- @class ExuAnimationInfo
--- @field name string
--- @field targetKind "gameObject"|"localFirstPerson"|"cockpit"
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
--- @field cockpitTarget boolean True when EXU's native supported-build render-bridge read (used by `TargetCockpit`) is available.
--- @field subEntityRenderQueue boolean True when the OgreMain SubEntity render queue exports resolved (`SetSubEntityRenderQueue`, `exu.SetSubEntityRenderQueueGroup`).
--- @field glass boolean True when a cockpit glass submesh can be drawn after the world: `subEntityRenderQueue` and `cockpitTarget` (see Docs/COCKPIT_GLASS.md).
--- @field animationInventory boolean True when `List` is available.
--- @field pilotStateInspection boolean True when the read-only local Person FSM snapshot API is compiled in.
--- @field pilotFsmIntercept boolean True when the verified observe-only Person::Simulate entry detour is active.
--- @field pilotAnimationOverrides boolean True when `exu.fps.SetPilotAnimationProfile` can apply non-stock pilot animation overrides in this session: the build supports them AND the Person::Simulate seam is active with its native clip tables qualified. Overrides are still single player only (the setter errors in multiplayer).
--- @field firstPersonLayers boolean True when `exu.fps.SetLayer` layers can be advanced: the Person::Simulate seam is active. Presentation-only, so also true in multiplayer.
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

--- Selects a craft's COCKPIT Ogre entity: the entity Redux draws in the
--- first-person cockpit view (render bridge +0xC0). For a mesh with in-mesh
--- cockpit bones (`2` at name index 3) it is a second Entity of the same mesh
--- whose `2` bones are live; for a separate `<name>_cockpit.mesh` it is that
--- entity. A plain handle selects the world entity, where `2` bones are
--- manual and scaled to 0, so cockpit clips/material swaps there are invisible.
--- Resolved afresh on every call, never cached; fails closed (false/nil) when
--- the cockpit entity does not exist (outside cockpit view, before the
--- renderer builds it, unsupported build). Works for any GameObject, no clip
--- requirements. Also accepted as argument 1 by the handle-based entity,
--- sub-entity material and `*EntityAnimation*` functions.
--- @param h Handle
--- @return ExuAnimationTarget
function animation.TargetCockpit(h) end

--- Same as `exu.SetSubEntityRenderQueueGroup`, here beside `TargetCockpit`.
--- @param target Handle|ExuAnimationTarget
--- @param selector integer|string Sub-entity index or material name
--- @param group integer
--- @param priority integer?
--- @return integer changed
--- @return integer? firstIndex
function animation.SetSubEntityRenderQueue(target, selector, group, priority) end

--- Same as `exu.GetSubEntityRenderQueueGroup`.
--- @param target Handle|ExuAnimationTarget
--- @param subEntityIndex integer
--- @return integer? group
--- @return boolean? isOwnGroup
function animation.GetSubEntityRenderQueue(target, subEntityIndex) end

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

--- @class exu
--- @field animation ExuAnimationApi
