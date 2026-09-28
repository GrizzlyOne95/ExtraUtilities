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
