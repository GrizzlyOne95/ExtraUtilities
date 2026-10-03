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
