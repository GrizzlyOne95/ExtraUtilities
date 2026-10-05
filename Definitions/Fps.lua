--- @meta exu
--- Local first-person/pilot animation convenience API for Extra Utilities.
--- This file augments Definitions/ExtraUtils.lua; it is editor metadata only.

--- @class ExuPilotState
--- @field available boolean
--- @field state "standing"|"enteringCrouch"|"crouched"|"exitingCrouch"|"unknown"
--- @field nativeState integer Raw `Person+0x228` FSM value.
--- @field transition boolean True only for enter/exit crouch states 1 and 3.
--- @field crouched boolean True only when the native FSM is fully in state 2.
--- @field grounded boolean Native ground-contact bit from the Person vehicle/control object.
--- @field sniperSelected boolean True when any selected live weapon has class signature `SNIP`.
--- @field animationIndex integer Raw `Person+0x2A8` animation index.
--- @field animationName string? Known stock name for proven indices 0,1,2,3,10,11; nil for unmapped indices.
--- @field animationHandle integer Raw `Person+0x2AC` animation handle; stock transition states wait for -1.
--- @field selectedWeaponMask integer Native Carrier selected-mask bits.
--- @field selectedWeaponSlot integer? First selected slot that currently contains a live weapon.
--- @field selectedWeaponSignature integer? Class signature for `selectedWeaponSlot`.
--- @field selectedWeaponSignatureText string? Four-character printable form of the signature.
--- @field selectedWeaponOdf string? Weapon-class ODF for `selectedWeaponSlot`.

--- @class ExuPilotInterceptStatus
--- @field installed boolean True when the verified trampoline is prepared.
--- @field active boolean True when the Person::Simulate entry currently points at EXU's hook.
--- @field observeOnly boolean False only while table overrides are in force: overrides available, single player, and a non-stock active profile. True means the hook only observes.
--- @field overridesAvailable boolean The seam is active and the native pilot clip tables passed their install-time preimage check.
--- @field hasLocalSample boolean True after a local on-foot Person has completed at least one intercepted Simulate call.
--- @field calls integer Total intercepted Person::Simulate calls, including non-local Person objects.
--- @field localCalls integer Intercepted calls where the simulated Person was exactly the current local user object.
--- @field stateChanges integer Local calls whose native FSM state changed across the stock Simulate call.
--- @field animationChanges integer Local calls whose animation index or handle changed across the stock Simulate call.
--- @field overrideCalls integer Local calls around which the clip tables were rewritten (and restored).
--- @field policyDecision "passThrough"|"override"|string? What the pilot animation policy told the seam to do for the most recent local call. nil until a local call has been intercepted. "override" for every mapped native state (0-3) while a supported non-stock profile is active; "passThrough" otherwise.
--- @field beforeNativeState integer?
--- @field afterNativeState integer?
--- @field beforeState string?
--- @field afterState string?
--- @field beforeAnimationIndex integer?
--- @field afterAnimationIndex integer?
--- @field beforeAnimationName string?
--- @field afterAnimationName string?
--- @field beforeAnimationHandle integer?
--- @field afterAnimationHandle integer?

--- @class ExuPilotTraceOptions
--- @field changesOnly boolean? Sample only calls whose native state, animation index, or animation handle changed. The clock and dwell times still include every call. Default false.

--- One local Person::Simulate call.
--- @class ExuPilotTraceSample
--- @field call integer Local calls since the trace started, counting this one (1-based).
--- @field dt number dt exactly as passed to Person::Simulate.
--- @field time number Trace clock (sum of valid dt since the trace started) at the end of this call.
--- @field beforeNativeState integer
--- @field afterNativeState integer
--- @field beforeAnimationIndex integer
--- @field afterAnimationIndex integer
--- @field beforeAnimationHandle integer
--- @field afterAnimationHandle integer

--- How long one native FSM state lasted: the sum of dt over the calls that
--- started in the state, up to and including the call that left it. Only
--- visits whose entry and exit were both observed are counted.
--- @class ExuPilotTraceDwell
--- @field nativeState integer
--- @field count integer Complete visits observed.
--- @field last number? Seconds; present when count > 0.
--- @field min number?
--- @field max number?
--- @field mean number?
--- @field lastCalls integer? Local calls in the last complete visit.

--- @class ExuPilotTrace
--- @field enabled boolean
--- @field changesOnly boolean
--- @field capacity integer Maximum samples held; older samples are overwritten.
--- @field localCalls integer Local calls seen since the trace started.
--- @field recorded integer Samples written since the trace started (may exceed capacity).
--- @field time number Trace clock in seconds.
--- @field samples ExuPilotTraceSample[] Oldest first.
--- @field dwell table<"standing"|"enteringCrouch"|"crouched"|"exitingCrouch", ExuPilotTraceDwell>

--- @class ExuPilotPolicySlot
--- @field mode "stock"|"substitute" "stock" leaves the native clip for this slot untouched.
--- @field animation string? Substitute clip name; present only with mode "substitute".
--- @field completion "stock"|"animation"|"duration"|"manual"|nil Present only for enterCrouch/exitCrouch.
--- @field duration number? Seconds; present only with completion "duration".
--- @field nativeState integer? Native `Person+0x228` value the slot corresponds to. Present for stand/enterCrouch/crouched/exitCrouch (0-3); absent for jump/land, which are animation selections whose native conditions are not yet traced.

--- One slot of a profile passed to `exu.fps.SetPilotAnimationProfile`. Every
--- field is optional; omitted fields are stock. Unknown fields are an error.
--- @class ExuPilotPolicySlotConfig
--- @field mode "stock"|"substitute"|nil
--- @field animation string? Required by, and only allowed with, mode "substitute". 1-63 characters. Must exist on BOTH the pilot's world (third-person) and first-person skeletons, or the slot stays stock.
--- @field completion "stock"|"animation"|"duration"|"manual"|nil enterCrouch/exitCrouch only.
--- @field duration number? Seconds in (0, 60]. Required by, and only allowed with, completion "duration".

--- Profile passed to `exu.fps.SetPilotAnimationProfile`. Omitted slots are
--- stock; unknown slot keys are an error.
--- @class ExuPilotAnimationProfileConfig
--- @field stand ExuPilotPolicySlotConfig? Native idx 2 `idle`.
--- @field enterCrouch ExuPilotPolicySlotConfig? Native idx 0 `stand2Kneel` (state 1).
--- @field crouched ExuPilotPolicySlotConfig? Native idx 3 `fireRecoilSniper`.
--- @field exitCrouch ExuPilotPolicySlotConfig? Native idx 1 `kneel2stand` (state 3).
--- @field jump ExuPilotPolicySlotConfig? Native idx 11 `jump`.
--- @field land ExuPilotPolicySlotConfig? Native idx 10 `landParachute`.

--- Effective pilot animation profile. Read-only; one entry per policy slot.
--- @class ExuPilotAnimationProfile
--- @field stand ExuPilotPolicySlot
--- @field enterCrouch ExuPilotPolicySlot
--- @field crouched ExuPilotPolicySlot
--- @field exitCrouch ExuPilotPolicySlot
--- @field jump ExuPilotPolicySlot
--- @field land ExuPilotPolicySlot

--- Options for `exu.fps.SetLayer`. Unknown keys are an error. A field left
--- out keeps the layer's current value (its default when the layer is new).
--- @class ExuFirstPersonLayerOptions
--- @field speed number? Clip-seconds per second, 0..50 (default 1). For a 1 s one-revolution clip this is revolutions per second.
--- @field weight number? Ogre blend weight, 0..1 (default 1).
--- @field loop boolean? Wrap at the clip end (default true); false clamps at the end.
--- @field time number? Seconds >= 0: seek EXU's clock for this layer here (wrapped or clamped to the clip).
--- @field fadeIn number? Seconds (this call only): ramp from the weight being applied to the target weight.
--- @field fadeOut number? Seconds (stored): the default fade of a later `ClearLayer`.
--- @field fire ExuFirstPersonLayerFire|false? Trigger drive; `false` removes it.

--- Trigger drive for a layer: while the local fire bind is held the effective
--- speed ramps toward `speed`, and back to the base speed on release.
--- @class ExuFirstPersonLayerFire
--- @field speed number Target clip-seconds per second while the trigger is held.
--- @field spinUp number? Seconds for the whole ramp up (0 or omitted = instant).
--- @field spinDown number? Seconds for the whole ramp down (0 or omitted = instant).

--- Options for `exu.fps.PlayLayer`. Unknown keys are an error.
--- @class ExuFirstPersonPlayLayerOptions
--- @field speed number? Clip-seconds per second, 0..50 (sticky).
--- @field weight number? Ogre blend weight, 0..1 (sticky).
--- @field fadeIn number? Seconds to ramp in for this play.
--- @field fadeOut number? Seconds to ramp out so the weight reaches 0 at the clip end.
--- @field clearOnEnd boolean? Default true: disable when finished; false holds the last frame.

--- One entry of `exu.fps.GetLayers()`, as of the most recent local pilot tick.
--- @class ExuFirstPersonLayer
--- @field name string
--- @field speed number
--- @field weight number
--- @field loop boolean
--- @field time number EXU's clock for the layer (seconds into the clip).
--- @field length number Clip length in seconds; 0 until the clip has been found.
--- @field active boolean True when EXU applied the layer on the most recent local tick.
--- @field reason "pending"|"missing"|"engineOwned"|"noFirstPersonEntity"|"faulted"|"unavailable"|nil Why it is not active (nil when active).
--- @field blendMode "average"|"cumulative"|"unknown" Blend mode of the first-person skeleton. Layers need "cumulative": under "average" Ogre rescales every enabled clip by 1/total weight once weights sum past 1, distorting the whole pose.

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

--- Returns a read-only snapshot of the current local on-foot Person animation
--- FSM and its inputs. Returns nil when the current user object is not a Person
--- or the qualified native read fails.
--- @nodiscard
--- @return ExuPilotState|nil
function fps.GetPilotState() end

--- Returns the effective mission-scoped pilot animation profile: what EXU's
--- policy layer does for each pilot animation slot (mode, animation,
--- completion, duration, nativeState). Stock by default and again at every
--- mission/Lua-state boundary. Changes nothing.
--- @nodiscard
--- @return ExuPilotAnimationProfile
function fps.GetPilotAnimationProfile() end

--- Replaces the mission-scoped pilot animation profile. `nil` or `{}`
--- restores stock and is always allowed. Raises a Lua error, leaving the
--- active profile unchanged, when the profile is invalid (unknown slot or
--- field, wrong type, missing `animation`/`duration`, ...), in multiplayer
--- ("single player only"), or when `GetCapabilities().pilotAnimationOverrides`
--- is false.
---
--- Applied natively for the local pilot only, by rewriting Person::Simulate's
--- per-clip tables around each local call:
--- * A substitute must exist on both the world and the first-person
---   skeleton; otherwise that slot stays stock (logged once per name). It
---   takes effect the next time the engine applies that slot's clip.
--- * Stock crouch transitions last endTime/rate = 0.967/0.5 = about 1.934 s.
---   `completion = "animation"`: one play of the clip at authored speed (end =
---   clip length, rate 1). `"duration"`: lasts `duration` seconds (rate =
---   end/duration; end = 0.967-capped stock clip, or the whole substitute).
---   `"manual"`: the clip plays at stock speed and holds at its end until
---   `exu.fps.CompleteTransition()`.
--- @param profile ExuPilotAnimationProfileConfig?
function fps.SetPilotAnimationProfile(profile) end

--- Finishes the current crouch transition on the next pilot update. Returns
--- true only when overrides are available, the session is single player, the
--- local pilot is entering or exiting crouch (native state 1 or 3), and that
--- transition's profile completion is "manual". One-shot.
--- @return boolean requested
function fps.CompleteTransition() end

--- Returns diagnostics for EXU's verified Person::Simulate interception seam.
--- The seam always calls the stock trampoline; for the local pilot under a
--- non-stock profile (single player) it rewrites the native clip tables
--- around that call and restores them straight after. It also records local
--- pre/post state transitions.
--- @nodiscard
--- @return ExuPilotInterceptStatus
function fps.GetPilotInterceptStatus() end

--- Starts (or restarts) the read-only pilot FSM timing trace, discarding any
--- earlier trace data. The trace is off by default, off again in every new
--- mission/Lua state, and writes nothing to the game. Unknown option keys are
--- an error. Returns whether the interception seam is active, i.e. whether
--- samples can actually arrive.
--- @param options ExuPilotTraceOptions?
--- @return boolean seamActive
function fps.StartPilotTrace(options) end

--- Stops recording. Recorded data stays readable until the next start or the
--- end of the mission/Lua state.
function fps.StopPilotTrace() end

--- Returns the timing trace: at most `limit` of the newest samples (default:
--- all held) plus per-state dwell times. Returns nil only if the native
--- writer kept rewriting the data during every read attempt; retry later.
--- @nodiscard
--- @param limit integer? Positive integer.
--- @return ExuPilotTrace|nil
function fps.GetPilotTrace(limit) end

--- Creates or updates an EXU-clocked first-person layer: a clip on the local
--- pilot's FIRST-PERSON skeleton that EXU keeps enabled and advances by
--- dt * speed on every local Person::Simulate call, whatever the pilot FSM is
--- doing (stand/crouch/run/jump). Presentation-only: writes no gameplay
--- state, touches only the local first-person entity, and is allowed in
--- multiplayer. At most 8 layers; name 1..63 characters. Raises a Lua error
--- on an invalid name or option, an unknown option key, or a ninth layer.
--- Layers are cleared at every mission/Lua-state boundary.
---
--- Rig contract: the clip exists only on the first-person skeleton, keys only
--- its own bone(s) (e.g. a barrel bone), stock FSM clips have no tracks for
--- those bones, the skeleton uses blendmode "cumulative", and a looping clip
--- is seamless (last key == first key, e.g. 360 degrees).
---
--- If the FSM itself is playing a clip of the same name this tick, EXU leaves
--- it alone (reason "engineOwned") instead of advancing it twice.
--- @param name string Ogre animation name.
--- @param options ExuFirstPersonLayerOptions?
--- @return true
function fps.SetLayer(name, options) end

--- Sets a layer's speed (clip-seconds per second, 0..50). Raises a Lua error
--- if no layer has that name or the speed is invalid.
--- @param name string
--- @param speed number
--- @return true
function fps.SetLayerSpeed(name, speed) end

--- Sets a layer's blend weight (0..1), optionally fading to it. Raises a Lua
--- error if no layer has that name or an argument is invalid.
--- @param name string
--- @param weight number
--- @param fadeSeconds number?
--- @return true
function fps.SetLayerWeight(name, weight, fadeSeconds) end

--- Creates or restarts a non-looping one-shot layer at time 0. Each call bumps
--- the layer's `playCount`; `finishedCount == playCount` means this play is
--- done. Raises on an invalid name or option, or a ninth live layer.
--- @param name string
--- @param options ExuFirstPersonPlayLayerOptions?
--- @return true
function fps.PlayLayer(name, options) end

--- Removes a layer. On the next local pilot tick that has a first-person
--- entity, EXU disables the Ogre state and resets its weight to 1 and time to
--- 0 (if the state still exists). Returns whether the layer existed.
--- @param name string
--- @param fadeSeconds number? Fade to 0 first (default: the layer's `fadeOut`).
--- @return boolean existed
function fps.ClearLayer(name, fadeSeconds) end

--- Removes every layer (each is disabled and reset as in `ClearLayer`).
function fps.ClearLayers() end

--- Returns the layers in creation order with the hook's most recent result
--- for each. Changes nothing.
--- @nodiscard
--- @return ExuFirstPersonLayer[]
function fps.GetLayers() end

--- Ramps the weight EXU applies to the clip the pilot FSM is currently playing
--- (default 1; reset to 1 when the FSM moves on and at mission boundaries).
--- @param weight number 0..1
--- @param fadeSeconds number?
--- @return true
function fps.SetBaseWeight(weight, fadeSeconds) end

--- Returns the base clip weight as `current, target`.
--- @nodiscard
--- @return number current
--- @return number target
function fps.GetBaseWeight() end

--- @class ExuTransitionBlendOptions
--- @field enabled boolean? Default true when a table is given.
--- @field time number? Blend seconds, 0..2 (default 0.15; 0 = hard cut).
--- @field phaseCarry boolean? Line up strides on run<->run switches (default true).
--- @field fp boolean? Fade the local pilot's first-person entity (default true).
--- @field world boolean? Fade every Person's world (third-person) entity (default true).
--- @field death boolean? Also fade into death1 (default true).

--- @class ExuTransitionBlend
--- @field enabled boolean
--- @field time number
--- @field phaseCarry boolean
--- @field fp boolean
--- @field world boolean
--- @field death boolean
--- @field available boolean The Person::Simulate seam is active.
--- @field faulted boolean An Ogre call faulted; the blend is off for this Lua state.
--- @field transitions integer Fades started this mission.
--- @field phaseCarries integer Run<->run switches whose stride phase was carried.
--- @field activeFades integer Entities fading right now.
--- @field evictions integer Fades dropped because the 32-entity table was full.

--- Cross-fades the clip switches of the native Person animation FSM (every
--- Person's world entity and the local first-person entity) instead of the
--- stock hard cut. A table replaces the whole setting (omitted keys take their
--- defaults); `nil`/`false` turns it off. Off by default and at every mission
--- boundary. Presentation only (weights and the outgoing clip's clock), so it
--- also works in multiplayer. Returns whether the seam is active.
--- @param options ExuTransitionBlendOptions|false|nil
--- @return boolean available
function fps.SetTransitionBlend(options) end

--- Returns the transition blend settings and live counters.
--- @nodiscard
--- @return ExuTransitionBlend
function fps.GetTransitionBlend() end

--- @class ExuDeathCamera
--- @field mode "first"|"stock"
--- @field available boolean The build qualified for native patches.
--- @field patched boolean Both call-site redirects are in place.
--- @field armed boolean A death1 is playing in first person right now.
--- @field probe boolean Diagnostic logging to exu.log while armed.
--- @field kept integer Deaths kept in first person this mission.
--- @field forced integer Times the guard handed the camera back early.
--- @field declined integer Deaths left stock because a condition failed.

--- Keeps the camera on the local pilot's POV bone while `death1` plays after
--- a snipe (`"first"`), instead of the stock free-eye switch (`"stock"`/nil).
--- Single player only (checked at the moment of death); ordinary chunk deaths
--- are unaffected. When death1 ends, the stock camera switch runs as usual.
--- Mission-scoped: stock at every mission boundary. Returns whether first
--- person is active.
--- @param mode "first"|"stock"|nil
--- @param options { probe: boolean? }?
--- @return boolean active
function fps.SetDeathCamera(mode, options) end

--- Returns the death camera mode and counters.
--- @nodiscard
--- @return ExuDeathCamera
function fps.GetDeathCamera() end

--- Returns whether the local fire/auto-fire bind was held at the engine's last
--- poll. False when `firstPersonTrigger` is false.
--- @nodiscard
--- @return boolean
function fps.IsTriggerHeld() end

--- Attaches a particle system (made with `exu.CreateParticleSystem`) to a bone
--- of the local first-person skeleton. Idempotent; call it every Update while
--- the effect should show. Returns `attached, reattached`; an unknown bone or
--- missing FP entity returns `false` without changes.
--- @param name string
--- @param boneName string
--- @param offset Vector|number? Offset in the bone's local frame (vector, or x then y, z).
--- @param y number?
--- @param z number?
--- @return boolean attached
--- @return boolean reattached
function fps.AttachParticleToBone(name, boneName, offset, y, z) end

--- Returns whether the named system is currently bound to a first-person bone.
--- @nodiscard
--- @param name string
--- @return boolean
function fps.IsParticleAttached(name) end

--- Drops the first-person binding of a named particle system. Returns whether
--- a binding existed.
--- @param name string
--- @return boolean hadBinding
function fps.DetachParticle(name) end

--- Returns a counter that increments each time a binding lands on a different
--- first-person entity than the previous binding.
--- @nodiscard
--- @return integer
function fps.GetParticleTargetGeneration() end

--- Returns true only for the fully crouched native FSM state (state 2).
--- Entering/exiting crouch return false; unavailable pilot state returns nil.
--- @nodiscard
--- @return boolean|nil
function fps.IsCrouched() end

--- Returns the native grounded flag for the current local Person, or nil when
--- no readable local Person exists.
--- @nodiscard
--- @return boolean|nil
function fps.IsGrounded() end

--- Returns whether any selected live weapon is a sniper-class weapon (SNIP),
--- or nil when no readable local Person exists.
--- @nodiscard
--- @return boolean|nil
function fps.IsSniperSelected() end

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
