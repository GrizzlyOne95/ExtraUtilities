--- @meta exu
--- This file provides the lua definitions for Extra Utilities version 1.3.3
--- These definitions also require the stock definitions found in `scriptutils.lua`
--- for basic types like Handle and Vector

error("This is a definition file, use require(\"exu\")")

--- @diagnostic disable: unused-local

--- Types

--- @alias GameObject* lightuserdata pointer the underlying object of a Handle
--- @alias Ordnance* lightuserdata pointer to the ordnance object

--- Structs

--- Note the inconsistent capitalization, this is intentional since it's the way
--- the game does it internally
--- @class CameraOrigins
--- @field Orig_x number Half of your horizontal resolution in pixels
--- @field Orig_y number Half of your vertical resolution in pixels
--- @field Const_x number Normalized coordinates
--- @field Const_y number Normalized coordinates (Const_x negated)

--- @class Color
--- @field r number
--- @field g number
--- @field b number
--- @field a number

--- @class Fog
--- @field r number
--- @field g number
--- @field b number
--- @field start number meters
--- @field ending number meters

--- @class SkyBoxParams
--- @field distance number

--- @class SkyDomeParams
--- @field curvature number
--- @field distance number
--- @field tiling number
--- @field xsegments integer
--- @field ysegments integer
--- @field ysegments_keep integer

--- @class SkyPlaneParams
--- @field bow number
--- @field scale number
--- @field tiling number
--- @field xsegments integer
--- @field ysegments integer

--- @class TerrainTextureSet
--- @field material string? Optional explicit terrain material name. When omitted, EXU resolves the current map's TRN `[Atlases] MaterialName`.
--- @field materialName string? Alias for `material`.
--- @field trn string? Optional TRN filename or relative path to resolve instead of the current map's TRN.
--- @field trnFilename string? Alias for `trn`.
--- @field resourceGroup string? Optional Ogre resource group. Defaults to `"General"`.
--- @field techniqueIndex integer? Optional material technique index. Defaults to `0`.
--- @field technique integer? Alias for `techniqueIndex`.
--- @field passIndex integer? Optional material pass index. Defaults to `0`.
--- @field pass integer? Alias for `passIndex`.
--- @field diffuse string? Terrain diffuse/base texture.
--- @field diffuseMap string? Alias for `diffuse`.
--- @field detail string? Terrain detail texture.
--- @field detailMap string? Alias for `detail`.
--- @field normal string? Terrain normal map.
--- @field normalMap string? Alias for `normal`.
--- @field specular string? Terrain specular map.
--- @field specularMap string? Alias for `specular`.
--- @field emissive string? Terrain emissive map.
--- @field emissiveMap string? Alias for `emissive`.

--- @class AnimationTarget
--- @field kind "gameObject"|"localFirstPerson"
--- @field handle Handle? Present only for a game-object target.

--- @class AnimationPlayOptions
--- @field restart boolean? Reset to time zero before enabling. Defaults to true.
--- @field loop boolean? Defaults to false.
--- @field weight number? Blend weight in [0, 1]. Defaults to 1.

--- @class AnimationInfo
--- @field name string
--- @field targetKind "gameObject"|"localFirstPerson"
--- @field enabled boolean
--- @field loop boolean
--- @field weight number
--- @field timePosition number
--- @field length number
--- @field normalizedTime number
--- @field atEnd boolean

--- @class AnimationCapabilities
--- @field gameObjectTarget boolean
--- @field localFirstPersonTarget boolean
--- @field animationInventory boolean
--- @field pilotStateInspection boolean
--- @field pilotFsmIntercept boolean
--- @field pilotAnimationOverrides boolean
--- @field firstPersonLayers boolean
--- @field transitionBlend boolean
--- @field managedClock boolean
--- @field nativeAdvancement string
--- @field firstPersonStatus string

--- @class AnimationApi
--- @field Target fun(handle: Handle): AnimationTarget
--- @field TargetLocalFirstPerson fun(): AnimationTarget
--- @field GetCapabilities fun(): AnimationCapabilities
--- @field Has fun(target: Handle|AnimationTarget, animationName: string): boolean
--- @field GetInfo fun(target: Handle|AnimationTarget, animationName: string): AnimationInfo?
--- @field List fun(target: Handle|AnimationTarget): AnimationInfo[]?
--- @field Play fun(target: Handle|AnimationTarget, animationName: string, options?: AnimationPlayOptions): boolean
--- @field Stop fun(target: Handle|AnimationTarget, animationName: string, reset?: boolean): boolean
--- @field Restart fun(target: Handle|AnimationTarget, animationName: string): boolean
--- @field SetEnabled fun(target: Handle|AnimationTarget, animationName: string, enabled: boolean): boolean
--- @field SetLoop fun(target: Handle|AnimationTarget, animationName: string, loop: boolean): boolean
--- @field SetWeight fun(target: Handle|AnimationTarget, animationName: string, weight: number): boolean
--- @field Seek fun(target: Handle|AnimationTarget, animationName: string, timePosition: number): boolean

--- @class PilotStateInfo
--- @field available boolean
--- @field state "standing"|"enteringCrouch"|"crouched"|"exitingCrouch"|"unknown"
--- @field nativeState integer
--- @field transition boolean
--- @field crouched boolean
--- @field grounded boolean
--- @field sniperSelected boolean
--- @field animationIndex integer
--- @field animationName string?
--- @field animationHandle integer
--- @field selectedWeaponMask integer
--- @field selectedWeaponSlot integer?
--- @field selectedWeaponSignature integer?
--- @field selectedWeaponSignatureText string?
--- @field selectedWeaponOdf string?

--- @class PilotInterceptStatus
--- @field installed boolean
--- @field active boolean
--- @field observeOnly boolean
--- @field overridesAvailable boolean
--- @field hasLocalSample boolean
--- @field calls integer
--- @field localCalls integer
--- @field stateChanges integer
--- @field animationChanges integer
--- @field overrideCalls integer
--- @field policyDecision "passThrough"|"override"|string?
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

--- @class PilotTraceOptions
--- @field changesOnly boolean?

--- @class PilotTraceSample
--- @field call integer
--- @field dt number
--- @field time number
--- @field beforeNativeState integer
--- @field afterNativeState integer
--- @field beforeAnimationIndex integer
--- @field afterAnimationIndex integer
--- @field beforeAnimationHandle integer
--- @field afterAnimationHandle integer

--- @class PilotTraceDwell
--- @field nativeState integer
--- @field count integer
--- @field last number?
--- @field min number?
--- @field max number?
--- @field mean number?
--- @field lastCalls integer?

--- @class PilotTrace
--- @field enabled boolean
--- @field changesOnly boolean
--- @field capacity integer
--- @field localCalls integer
--- @field recorded integer
--- @field time number
--- @field samples PilotTraceSample[]
--- @field dwell table<"standing"|"enteringCrouch"|"crouched"|"exitingCrouch", PilotTraceDwell>

--- @class PilotPolicySlotInfo
--- @field mode "stock"|"substitute"
--- @field animation string?
--- @field completion "stock"|"animation"|"duration"|"manual"|nil
--- @field duration number?
--- @field nativeState integer?

--- @class PilotPolicySlotConfig
--- @field mode "stock"|"substitute"|nil
--- @field animation string?
--- @field completion "stock"|"animation"|"duration"|"manual"|nil
--- @field duration number?

--- @class PilotAnimationProfileConfig
--- @field stand PilotPolicySlotConfig?
--- @field enterCrouch PilotPolicySlotConfig?
--- @field crouched PilotPolicySlotConfig?
--- @field exitCrouch PilotPolicySlotConfig?
--- @field jump PilotPolicySlotConfig?
--- @field land PilotPolicySlotConfig?

--- @class PilotAnimationProfile
--- @field stand PilotPolicySlotInfo
--- @field enterCrouch PilotPolicySlotInfo
--- @field crouched PilotPolicySlotInfo
--- @field exitCrouch PilotPolicySlotInfo
--- @field jump PilotPolicySlotInfo
--- @field land PilotPolicySlotInfo

--- @class FirstPersonLayerOptions
--- @field speed number?
--- @field weight number?
--- @field loop boolean?
--- @field time number?

--- @class FirstPersonLayer
--- @field name string
--- @field speed number
--- @field weight number
--- @field loop boolean
--- @field time number
--- @field length number
--- @field active boolean
--- @field reason string?
--- @field blendMode string

--- @class FpsAnimationApi
--- @field IsAvailable fun(): boolean
--- @field GetCapabilities fun(): AnimationCapabilities
--- @field GetPilotState fun(): PilotStateInfo?
--- @field GetPilotAnimationProfile fun(): PilotAnimationProfile
--- @field SetPilotAnimationProfile fun(profile?: PilotAnimationProfileConfig)
--- @field CompleteTransition fun(): boolean
--- @field GetPilotInterceptStatus fun(): PilotInterceptStatus
--- @field StartPilotTrace fun(options?: PilotTraceOptions): boolean
--- @field StopPilotTrace fun()
--- @field GetPilotTrace fun(limit?: integer): PilotTrace?
--- @field SetLayer fun(name: string, options?: FirstPersonLayerOptions): true
--- @field SetLayerSpeed fun(name: string, speed: number): true
--- @field SetLayerWeight fun(name: string, weight: number): true
--- @field ClearLayer fun(name: string): boolean
--- @field ClearLayers fun()
--- @field GetLayers fun(): FirstPersonLayer[]
--- @field IsCrouched fun(): boolean?
--- @field IsGrounded fun(): boolean?
--- @field IsSniperSelected fun(): boolean?
--- @field ListAnimations fun(): AnimationInfo[]?
--- @field HasAnimation fun(animationName: string): boolean
--- @field GetInfo fun(animationName: string): AnimationInfo?
--- @field Play fun(animationName: string, options?: AnimationPlayOptions): boolean
--- @field Stop fun(animationName: string, reset?: boolean): boolean
--- @field Restart fun(animationName: string): boolean
--- @field Seek fun(animationName: string, timePosition: number): boolean

--- @class exu
--- @field Origins CameraOrigins
--- @field animation AnimationApi
--- @field fps FpsAnimationApi
local exu = {}

--- Enums
--- DO NOT USE THE RAW VALUES YOUR CODE WILL BE UNREADABLE

--- @class CameraState
exu.CAMERA = {
    -- Camera types
    FIRST_PERSON = 0x100,
    THIRD_PERSON = 0x101,

    -- View modes
    COCKPIT = 1, -- F1
    NO_COCKPIT = 2, -- F2
    CHASE = 4, -- F3
    ORBIT = 5, -- F4
    NO_HUD = 3, -- F5
    EDITOR = 6, -- F9
    CHEAT_SATELLITE = 7, -- F10
    FREECAM = 9, -- F11

    TOGGLE_SATELLITE = 8, -- this code is used when satellite is activated with the 9 key
    TERRAIN_EDIT = 0x2A, -- CTRL+E
}

--- @class Defaults
exu.DEFAULTS = {
    CAMERA_MIN_ZOOM = 0.99,
    CAMERA_MAX_ZOOM = 1.6,
    COEFF_BALLISTIC = 4.9,
    GRAVITY_VECTOR = SetVector(0, -9.8, 0),
    RETICLE_RANGE = 200,
    SAT_MAX_ZOOM = 8,
    SAT_MIN_ZOOM = 2,
    SAT_PAN_SPEED = 1250
}

--- @class Difficulty
exu.DIFFICULTY = {
    VERY_EASY = 0,
    EASY = 1,
    MEDIUM = 2,
    HARD = 3,
    VERY_HARD = 4
}

--- @class Ogre
exu.OGRE = {
    HEADLIGHT_COLOR = {
        R = 1.0,
        G = 1.0,
        B = 1.0
    },
    HEADLIGHT_RANGE = {
        INNER_ANGLE = 0.1745,
        OUTER_ANGLE = 0.3490,
        FALLOFF = 1.0
    }
}

--- @class OverlayMetricsMode
exu.OVERLAY_METRICS = {
    RELATIVE = 0,
    PIXELS = 1,
    RELATIVE_ASPECT_ADJUSTED = 2
}

--- @class OrdnanceAttributes
exu.ORDNANCE = {
    ODF = 0,            -- The odf that corresponds to the ordnance
    TRANSFORM = 1,      -- The current transform of the ordnance
    INIT_TRANSFORM = 2, -- The initial transform of the ordnance when it was spawned/shot
    OWNER = 3,          -- The handle of the ordnance's owner (usually who shot it unless it's been spawned manually, then it could be anything)
    INIT_TIME = 4,      -- The time that the ordnance was spawned/shot
    VELOCITY = 5,       -- The current velocity vector of the ordnance
    LIFE_TIME = 6,      -- How long the ordnance has existed
}

--- @class RadarState
exu.RADAR= {
    MINIMAP = 0,
    RADAR = 1
}

--- @class SatelliteState
exu.SATELLITE = {
    DISABLED = 0,
    ENABLED = 1
}

--- Camera
---
--- These functions are related to the camera in-game and its attributes

--- Gets the values of the camera origins that can be useful for computing perspective transformations. 
--- ```lua
--- local origins = exu.GetCameraOrigins()
--- print(origins.Orig_x, origins.Orig_y)
--- ```
--- @nodiscard
--- @return CameraOrigins
function exu.GetCameraOrigins() end

--- Gets the transform matrix of the current camera.
--- ```lua
--- local cameraTransformMatrix = exu.GetCameraTransformMatrix()
--- ```
--- @nodiscard
--- @return Matrix
function exu.GetCameraTransformMatrix() end

--- Gets the view matrix of the current camera.
--- ```lua
--- local cameraViewMatrix = exu.GetCameraViewMatrix()
--- ```
--- @nodiscard
--- @return Matrix
function exu.GetCameraViewMatrix() end

--- Gets the maximum zoom factor for all cameras.
--- ```lua
--- local cameraMaxZoom = exu.GetCameraMaxZoom()
--- ```
--- @nodiscard
--- @return number
function exu.GetCameraMaxZoom() end

--- Sets the maximum zoom factor for all cameras.
--- ```lua
--- exu.SetCameraMaxZoom(1.6)
--- ```
--- @param zoom number
function exu.SetCameraMaxZoom(zoom) end

--- Gets the minimum zoom factor for all cameras.
--- ```lua
--- local cameraMinZoom = exu.GetCameraMinZoom()
--- ```
--- @nodiscard
--- @return number
function exu.GetCameraMinZoom() end

--- Sets the minimum zoom factor for all cameras.
--- ```lua
--- exu.SetCameraMinZoom(0.99)
--- ```
--- @param zoom number
function exu.SetCameraMinZoom(zoom) end

--- Gets the current camera view mode. See the camera enum for options.
--- ```lua
--- local isChase = exu.GetCameraView() == exu.CAMERA.CHASE
--- ```
--- @nodiscard
--- @return CameraState
function exu.GetCameraView() end

--- Gets the current camera field of view in radians.
--- ```lua
--- local cameraFOV = exu.GetCameraFOV()
--- ```
--- @nodiscard
--- @return number
function exu.GetCameraFOV() end

--- Gets the current active Ogre camera clip distances.
--- ```lua
--- local nearClip, farClip = exu.GetCameraClipDistances()
--- if not nearClip then return end
--- ```
--- @nodiscard
--- @return number | nil, number | nil
function exu.GetCameraClipDistances() end

--- Sets the current active Ogre camera clip distances.
--- ```lua
--- exu.SetCameraClipDistances(0.5, 4000)
--- ```
--- @param nearClip number
--- @param farClip number
function exu.SetCameraClipDistances(nearClip, farClip) end

--- Gets the current active Ogre camera aspect ratio.
--- ```lua
--- local cameraAspectRatio = exu.GetCameraAspectRatio()
--- if not cameraAspectRatio then return end
--- ```
--- @nodiscard
--- @return number | nil
function exu.GetCameraAspectRatio() end

--- Sets the current active Ogre camera aspect ratio.
--- ```lua
--- exu.SetCameraAspectRatio(16 / 9)
--- ```
--- @param ratio number
function exu.SetCameraAspectRatio(ratio) end

--- Gets the current active Ogre camera projection type.
--- 0 = orthographic, 1 = perspective.
--- ```lua
--- local cameraProjectionType = exu.GetCameraProjectionType()
--- if not cameraProjectionType then return end
--- ```
--- @nodiscard
--- @return integer | nil
function exu.GetCameraProjectionType() end

--- Sets the current active Ogre camera projection type.
--- 0 = orthographic, 1 = perspective.
--- ```lua
--- exu.SetCameraProjectionType(1)
--- ```
--- @param projectionType integer
function exu.SetCameraProjectionType(projectionType) end

--- Gets the current active Ogre camera polygon mode.
--- 1 = points, 2 = wireframe, 3 = solid.
--- ```lua
--- local cameraPolygonMode = exu.GetCameraPolygonMode()
--- if not cameraPolygonMode then return end
--- ```
--- @nodiscard
--- @return integer | nil
function exu.GetCameraPolygonMode() end

--- Sets the current active Ogre camera polygon mode.
--- 1 = points, 2 = wireframe, 3 = solid.
--- ```lua
--- exu.SetCameraPolygonMode(3)
--- ```
--- @param polygonMode integer
function exu.SetCameraPolygonMode(polygonMode) end

--- Sets the current camera view mode.
--- ```lua
--- exu.SetCameraView(exu.CAMERA.CHASE)
--- ```
--- @param view CameraState | any
function exu.SetCameraView(view) end

--- Gets the zoom factor of the given camera. See the camera enum for options.
--- ```lua
--- local cameraZoom = exu.GetCameraZoom(exu.CAMERA.THIRD_PERSON)
--- ```
--- @nodiscard
--- @param camera number
--- @return number
function exu.GetCameraZoom(camera) end

--- Sets the zoom factor of the given camera.
--- Note: This will override the min/max zoom until the player tries to zoom with -+.
--- ```lua
--- exu.SetCameraZoom(exu.CAMERA.THIRD_PERSON, 1.2)
--- ```
--- @param camera number
--- @param zoom number
--- @return nil
function exu.SetCameraZoom(camera, zoom) end

--- Control Panel
---
--- These functions control the selection that is handled through the control panel.

--- Gets the live pixel offset applied to the stock scrap/pilot HUD text.
--- This moves the stock text draw positions without editing the HUD textures.
--- ```lua
--- local offsetX, offsetY = exu.GetScrapPilotHudOffset()
--- ```
--- @nodiscard
--- @return integer x
--- @return integer y
function exu.GetScrapPilotHudOffset() end

--- Sets the live pixel offset applied to the stock scrap/pilot HUD text.
--- Negative X moves it left and negative Y moves it upward.
--- ```lua
--- exu.SetScrapPilotHudOffset(0, 0)
--- ```
--- @param x integer
--- @param y integer
function exu.SetScrapPilotHudOffset(x, y) end

--- Restores the stock scrap/pilot HUD text positions captured before any EXU layout overrides.
--- ```lua
--- local restored = exu.RestoreScrapPilotHudDefault()
--- ```
--- @return boolean
function exu.RestoreScrapPilotHudDefault() end

--- Gets the current top-left anchor of the stock scrap/pilot HUD text cluster.
--- This is useful when positioning it absolutely instead of as a relative offset.
--- ```lua
--- local left, top = exu.GetScrapPilotHudTopLeft()
--- if not left then return end
--- ```
--- @nodiscard
--- @return integer | nil x
--- @return integer | nil y
function exu.GetScrapPilotHudTopLeft() end

--- Sets the top-left anchor of the stock scrap/pilot HUD text cluster.
--- This keeps the stock relative spacing between the scrap/pilot text entries.
--- ```lua
--- exu.SetScrapPilotHudTopLeft(0, 0)
--- ```
--- @param x integer
--- @param y integer
function exu.SetScrapPilotHudTopLeft(x, y) end

--- Gets the current top-left anchor of the stock Scrap HUD text.
--- ```lua
--- local left, top = exu.GetScrapHudTopLeft()
--- if not left then return end
--- ```
--- @nodiscard
--- @return integer | nil x
--- @return integer | nil y
function exu.GetScrapHudTopLeft() end

--- Sets the top-left anchor of the stock Scrap HUD text.
--- ```lua
--- exu.SetScrapHudTopLeft(0, 0)
--- ```
--- @param x integer
--- @param y integer
function exu.SetScrapHudTopLeft(x, y) end

--- Gets the current top-left anchor of the stock Pilot HUD text.
--- ```lua
--- local left, top = exu.GetPilotHudTopLeft()
--- if not left then return end
--- ```
--- @nodiscard
--- @return integer | nil x
--- @return integer | nil y
function exu.GetPilotHudTopLeft() end

--- Sets the top-left anchor of the stock Pilot HUD text.
--- ```lua
--- exu.SetPilotHudTopLeft(0, 0)
--- ```
--- @param x integer
--- @param y integer
function exu.SetPilotHudTopLeft(x, y) end

--- Gets the ARGB color used for the stock Scrap HUD text.
--- The integer format is 0xAARRGGBB.
--- ```lua
--- local color = exu.GetScrapHudColor()
--- ```
--- @nodiscard
--- @return integer color
function exu.GetScrapHudColor() end

--- Sets the ARGB color used for the stock Scrap HUD text.
--- The integer format is 0xAARRGGBB.
--- ```lua
--- exu.SetScrapHudColor(0xFFFFCC00) -- 0xAARRGGBB
--- ```
--- @param color integer
function exu.SetScrapHudColor(color) end

--- Gets the ARGB color used for the stock Pilot HUD text.
--- The integer format is 0xAARRGGBB.
--- ```lua
--- local color = exu.GetPilotHudColor()
--- ```
--- @nodiscard
--- @return integer color
function exu.GetPilotHudColor() end

--- Sets the ARGB color used for the stock Pilot HUD text.
--- The integer format is 0xAARRGGBB.
--- ```lua
--- exu.SetPilotHudColor(0xFF00CCFF) -- 0xAARRGGBB
--- ```
--- @param color integer
function exu.SetPilotHudColor(color) end

--- Gets the live command-menu button footprint in screen pixels.
--- Returns the current left, top, right, and bottom bounds of the stock top-left command menu.
--- ```lua
--- local left, top, right, bottom = exu.GetCommandMenuRect()
--- if not left then return end
--- ```
--- @nodiscard
--- @return integer | nil left
--- @return integer | nil top
--- @return integer | nil right
--- @return integer | nil bottom
function exu.GetCommandMenuRect() end

--- Opts into BZ2-style native command/target panels, command icons and rotating
--- target/objective markers. Native input, visibility, labels, sounds and gameplay
--- remain active. Rendering is self-contained in EXU. Optional OpenShim yields
--- command palette/fading through the ownership protocol. False means an
--- unsupported build or conflicting native hook; no external renderer is needed.
--- Closing the mission Lua state releases presentation, including a pinned DLL.
--- ```lua
--- local accepted = exu.SetBz2HudPresentationEnabled(true)
--- if not accepted then print("BZ2 HUD presentation unavailable") end
--- ```
--- @param enabled boolean
--- @return boolean accepted
function exu.SetBz2HudPresentationEnabled(enabled) end

--- Adds the given unit to the local player's selection.
--- ```lua
--- exu.SelectAdd(GetPlayerHandle())
--- ```
--- @param h Handle
function exu.SelectAdd(h) end

--- Clears the local player's selection (equivalent to pressing tab with stock keybinds).
--- ```lua
--- exu.SelectNone()
--- ```
function exu.SelectNone() end

--- Sets the local player's selection to the given handle and de-selects everything else.
--- ```lua
--- exu.SelectOne(GetPlayerHandle())
--- ```
--- @param h Handle
function exu.SelectOne(h) end

--- Command Replacement
---
--- These functions replace a stock command slot on a specific unit with Lua-driven behavior.

--- Registers a stock command replacement on a specific handle.
--- Current automatic interception is limited to selected-unit `Hunt` through a native `Wingman::SetActiveMode` hook.
--- ```lua
--- exu.ReplaceStockCmd(GetHandle("tank1"), "Hunt", "my_hunt", function(unit, stockCommand, replacementLabel, origin)
---     return true -- true marks the command as handled
--- end)
--- ```
--- @param h Handle
--- @param stockCommandName string
--- @param replacementLabel string
--- @param callback fun(unit: Handle, stockCommand: string, replacementLabel: string, origin: string): boolean|nil
function exu.ReplaceStockCmd(h, stockCommandName, replacementLabel, callback) end

--- Removes a previously registered stock command replacement.
--- ```lua
--- local ok = exu.RemoveStockCmdReplacement(GetPlayerHandle(), "Hunt")
--- if not ok then print("exu.RemoveStockCmdReplacement failed") end
--- ```
--- @param h Handle
--- @param stockCommandName string
--- @return boolean
function exu.RemoveStockCmdReplacement(h, stockCommandName) end

--- Returns whether a replacement exists for the given unit and stock command.
--- ```lua
--- local hasStockCmdReplacement = exu.HasStockCmdReplacement(GetPlayerHandle(), "Hunt")
--- ```
--- @param h Handle
--- @param stockCommandName string
--- @return boolean
function exu.HasStockCmdReplacement(h, stockCommandName) end

--- Returns replacement metadata for the given unit and stock command.
--- ```lua
--- local info = exu.GetStockCmdReplacement(GetPlayerHandle(), "Hunt")
--- if info then print(info.replacementLabel) end
--- ```
--- @param h Handle
--- @param stockCommandName string
--- @return {stockCommand: string, replacementLabel: string}|nil
function exu.GetStockCmdReplacement(h, stockCommandName) end

--- Manually dispatches a registered replacement callback.
--- ```lua
--- local ok = exu.TriggerStockCmdReplacement(GetPlayerHandle(), "Hunt")
--- if not ok then print("exu.TriggerStockCmdReplacement failed") end
--- ```
--- @param h Handle
--- @param stockCommandName string
--- @return boolean
function exu.TriggerStockCmdReplacement(h, stockCommandName) end

--- Maintains command replacement state such as contextual label overrides.
--- If the native hook is unavailable, this also falls back to polling-based Hunt interception.
--- ```lua
--- exu.UpdateCommandReplacements()
--- ```
function exu.UpdateCommandReplacements() end

--- Environment
---
--- These functions can query and modify attributes about the environment like gravity and lighting.

--- Returns a table with the current map fog parameters.
--- Returns nil while the scene is not ready (for example during loading).
--- ```lua
--- local fog = exu.GetFog()
--- if fog then print(fog.start, fog.ending) end
--- ```
--- @nodiscard
--- @return Fog?
function exu.GetFog() end

--- Sets the current map fog parameters. Can take either five number parameters or a fog table.
--- ```lua
--- exu.SetFog(0.6, 0.6, 0.7, 200, 1500)
--- ```
--- @param r number
--- @param g number
--- @param b number
--- @param start number
--- @param ending number
function exu.SetFog(r, g, b, start, ending) end

--- Sets the current map fog parameters. Can take either five number parameters or a fog table.
--- ```lua
--- exu.SetFog({ r = 0.6, g = 0.6, b = 0.7, start = 200, ending = 1500 })
--- ```
--- @param newFog Fog
function exu.SetFog(newFog) end

--- Gets the current value of the local player's gravity velocity vector.
--- ```lua
--- local g = exu.GetGravity()
--- ```
--- @nodiscard
--- @return Vector
function exu.GetGravity() end

--- Sets the local player's current gravity velocity vector. This affects all units owned by the local player.
--- Can take either three number parameters or a bz vector userdata.
--- ```lua
--- exu.SetGravity(0, -4.9, 0)
--- ```
--- @param x number
--- @param y number
--- @param z number
function exu.SetGravity(x, y, z) end

--- Sets the local player's current gravity velocity vector. This affects all units owned by the local player.
--- Can take either three number parameters or a bz vector userdata.
---
--- Multiplayer remarks: this will affect ONLY local units, to keep the game looking consistent you should set this to the
--- same value across all clients if it's been changed from stock.
--- ```lua
--- exu.SetGravity(SetVector(0, -4.9, 0))
--- ```
--- @param v Vector
function exu.SetGravity(v) end

--- Returns a table with the current scene ambient light parameters.
--- Returns nil while the scene is not ready (for example during loading).
--- ```lua
--- local c = exu.GetAmbientLight()
--- if c then print(c.r, c.g, c.b) end
--- ```
--- @nodiscard
--- @return Color?
function exu.GetAmbientLight() end

--- Sets the current scene ambient light parameters. Can take either three or four number parameters or a color table.
--- ```lua
--- exu.SetAmbientLight(0.3, 0.3, 0.4)
--- ```
--- @param r number
--- @param g number
--- @param b number
--- @param a number? optional
function exu.SetAmbientLight(r, g, b, a) end

--- Sets the current scene ambient light parameters. Can take either three or four number parameters or a color table.
--- ```lua
--- exu.SetAmbientLight({ r = 0.3, g = 0.3, b = 0.4 })
--- ```
--- @param newColor Color
function exu.SetAmbientLight(newColor) end

--- Returns a table with the current sun ambient parameters.
--- Returns nil while the scene is not ready (for example during loading).
--- ```lua
--- local c = exu.GetSunAmbient()
--- if c then print(c.r, c.g, c.b) end
--- ```
--- @nodiscard
--- @return Color?
function exu.GetSunAmbient() end

--- Sets the current map sun ambient parameters. Can take either three number parameters or a color table.
--- ```lua
--- exu.SetSunAmbient(0.3, 0.3, 0.4)
--- ```
--- @param r number
--- @param g number
--- @param b number
function exu.SetSunAmbient(r, g, b) end

--- Sets the current map sun ambient parameters. Can take either three number parameters or a color table.
--- ```lua
--- exu.SetSunAmbient({ r = 0.3, g = 0.3, b = 0.4 })
--- ```
--- @param newColor Color
function exu.SetSunAmbient(newColor) end

--- Returns a table with the current sun diffuse parameters.
--- Returns nil while the scene is not ready (for example during loading).
--- ```lua
--- local c = exu.GetSunDiffuse()
--- if c then print(c.r, c.g, c.b) end
--- ```
--- @nodiscard
--- @return Color?
function exu.GetSunDiffuse() end

--- Sets the current map sun diffuse parameters. Can take either three number parameters or a color table.
--- ```lua
--- exu.SetSunDiffuse(1.0, 0.9, 0.8)
--- ```
--- @param r number
--- @param g number
--- @param b number
function exu.SetSunDiffuse(r, g, b) end

--- Sets the current map sun diffuse parameters. Can take either three number parameters or a color table.
--- ```lua
--- exu.SetSunDiffuse({ r = 1.0, g = 0.9, b = 0.8 })
--- ```
--- @param newColor Color
function exu.SetSunDiffuse(newColor) end

--- Returns a table with the current sun specular parameters.
--- Returns nil while the scene is not ready (for example during loading).
--- ```lua
--- local c = exu.GetSunSpecular()
--- if c then print(c.r, c.g, c.b) end
--- ```
--- @nodiscard
--- @return Color?
function exu.GetSunSpecular() end

--- Sets the current map sun specular parameters. Can take either three number parameters or a color table.
--- ```lua
--- exu.SetSunSpecular(1.0, 1.0, 1.0)
--- ```
--- @param r number
--- @param g number
--- @param b number
function exu.SetSunSpecular(r, g, b) end

--- Sets the current map sun specular parameters. Can take either three number parameters or a color table.
--- ```lua
--- exu.SetSunSpecular({ r = 1.0, g = 1.0, b = 1.0 })
--- ```
--- @param newColor Color
function exu.SetSunSpecular(newColor) end

--- Returns the current sun direction vector.
--- Returns nil while the scene is not ready (for example during loading).
--- ```lua
--- local dir = exu.GetSunDirection()
--- if not dir then return end
--- ```
--- @nodiscard
--- @return Vector?
function exu.GetSunDirection() end

--- Sets the current sun direction vector. Can take either a vector or three number parameters.
--- ```lua
--- exu.SetSunDirection(0.3, -0.8, 0.5)
--- ```
--- @param x number
--- @param y number
--- @param z number
function exu.SetSunDirection(x, y, z) end

--- Sets the current sun direction vector. Can take either a vector or three number parameters.
--- ```lua
--- exu.SetSunDirection(SetVector(0.3, -0.8, 0.5))
--- ```
--- @param direction Vector
function exu.SetSunDirection(direction) end

--- Sets the current terrain master light direction directly through Ogre.
--- This is an Ogre-only override and does not update the native Battlezone time-of-day state.
--- Can take either a vector or three number parameters.
--- ```lua
--- exu.SetOgreSunDirection(0.3, -0.8, 0.5)
--- ```
--- @param x number
--- @param y number
--- @param z number
function exu.SetOgreSunDirection(x, y, z) end

--- Sets the current terrain master light direction directly through Ogre.
--- This is an Ogre-only override and does not update the native Battlezone time-of-day state.
--- Can take either a vector or three number parameters.
--- ```lua
--- exu.SetOgreSunDirection(SetVector(0.3, -0.8, 0.5))
--- ```
--- @param direction Vector
function exu.SetOgreSunDirection(direction) end

--- Sets the native map time-of-day using the TRN HHMM format, such as 600, 1200, or 1830.
--- Minutes are stored, but the native light-model path currently advances in hour-sized steps.
--- When `refreshSun` is true or omitted, the terrain master light is rebuilt immediately.
--- ```lua
--- exu.SetTimeOfDay(1830) -- 18:30; the sun is rebuilt unless refreshSun is false
--- ```
--- @param timeOfDay integer
--- @param refreshSun boolean|nil
function exu.SetTimeOfDay(timeOfDay, refreshSun) end

--- Returns the current sun power scale multiplier.
--- ```lua
--- local sunPowerScale = exu.GetSunPowerScale()
--- ```
--- @nodiscard
--- @return number
function exu.GetSunPowerScale() end

--- Sets the current sun power scale multiplier.
--- ```lua
--- exu.SetSunPowerScale(1.5)
--- ```
--- @param powerScale number
function exu.SetSunPowerScale(powerScale) end

--- Returns the current sun shadow far distance.
--- ```lua
--- local sunShadowFarDistance = exu.GetSunShadowFarDistance()
--- ```
--- @nodiscard
--- @return number
function exu.GetSunShadowFarDistance() end

--- Sets the current sun shadow far distance.
--- ```lua
--- exu.SetSunShadowFarDistance(100)
--- ```
--- @param distance number
function exu.SetSunShadowFarDistance(distance) end

--- Returns the current skybox generation parameters if a skybox node exists.
--- ```lua
--- local params = exu.GetSkyBoxParams()
--- if params then print(params.distance) end
--- ```
--- @nodiscard
--- @return SkyBoxParams | nil
function exu.GetSkyBoxParams() end

--- Returns the current skydome generation parameters if a skydome node exists.
--- ```lua
--- local params = exu.GetSkyDomeParams()
--- if params then print(params.curvature) end
--- ```
--- @nodiscard
--- @return SkyDomeParams | nil
function exu.GetSkyDomeParams() end

--- Returns the current skyplane generation parameters if a skyplane node exists.
--- ```lua
--- local params = exu.GetSkyPlaneParams()
--- if params then print(params.scale) end
--- ```
--- @nodiscard
--- @return SkyPlaneParams | nil
function exu.GetSkyPlaneParams() end

--- Returns whether Ogre currently has an enabled skybox.
--- ```lua
--- local skyBoxEnabled = exu.GetSkyBoxEnabled()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetSkyBoxEnabled() end

--- Enables or disables the current skybox node.
--- ```lua
--- exu.SetSkyBoxEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetSkyBoxEnabled(enabled) end

--- Rebuilds the current skybox using the given material.
--- The distance defaults to 5000, drawFirst defaults to true, and the resource group defaults to "General".
--- ```lua
--- local ok = exu.SetSkyBox("my_skybox", 5000)
--- if not ok then print("SetSkyBox failed") end
--- ```
--- @param materialName string
--- @param distance number? optional
--- @param drawFirst boolean? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetSkyBox(materialName, distance, drawFirst, resourceGroup) end

--- Returns whether Ogre currently has an enabled skydome.
--- ```lua
--- local skyDomeEnabled = exu.GetSkyDomeEnabled()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetSkyDomeEnabled() end

--- Enables or disables the current skydome node.
--- ```lua
--- exu.SetSkyDomeEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetSkyDomeEnabled(enabled) end

--- Rebuilds the current skydome using the given material.
--- Defaults: curvature 10, tiling 8, distance 4000, drawFirst true, xsegments 16, ysegments 16, ysegmentsKeep -1.
--- The resource group defaults to "General".
--- ```lua
--- local ok = exu.SetSkyDome("my_skydome", 10, 8, 4000)
--- if not ok then print("SetSkyDome failed") end
--- ```
--- @param materialName string
--- @param curvature number? optional
--- @param tiling number? optional
--- @param distance number? optional
--- @param drawFirst boolean? optional
--- @param xsegments integer? optional
--- @param ysegments integer? optional
--- @param ysegmentsKeep integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetSkyDome(materialName, curvature, tiling, distance, drawFirst, xsegments, ysegments, ysegmentsKeep, resourceGroup) end

--- Returns whether Ogre currently has an enabled skyplane.
--- ```lua
--- local skyPlaneEnabled = exu.GetSkyPlaneEnabled()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetSkyPlaneEnabled() end

--- Enables or disables the current skyplane node.
--- ```lua
--- exu.SetSkyPlaneEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetSkyPlaneEnabled(enabled) end

--- Rebuilds the current skyplane using the given material and plane definition.
--- The plane table requires `d` or `distance`, and may optionally include `normal = Vector(...)`.
--- Defaults: scale 1000, tiling 10, drawFirst true, bow 0, xsegments 1, ysegments 1.
--- The resource group defaults to "General".
--- ```lua
--- local ok = exu.SetSkyPlane("my_clouds", { d = 500 }, 1000, 10)
--- if not ok then print("SetSkyPlane failed") end
--- ```
--- @param materialName string
--- @param plane table
--- @param scale number? optional
--- @param tiling number? optional
--- @param drawFirst boolean? optional
--- @param bow number? optional
--- @param xsegments integer? optional
--- @param ysegments integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetSkyPlane(materialName, plane, scale, tiling, drawFirst, bow, xsegments, ysegments, resourceGroup) end

--- Returns the colour the game clears its viewports to every frame, which is what shows
--- wherever no sky geometry draws (below a sky plane, past a sky dome's rim). This is the
--- terrain's TRN fog colour (`[Color] FogColor`, else the palette default), as 0..1 floats.
--- Returns nil on an unsupported game build.
--- ```lua
--- local c = exu.GetSkyClearColor()
--- if c then print(c.r, c.g, c.b) end
--- ```
--- @nodiscard
--- @return Color?
function exu.GetSkyClearColor() end

--- Sets the colour the game clears its viewports to every frame. Can take either three
--- numbers (0..1) or a color table. Use it with a custom sky so the background matches the
--- sky's horizon, typically the same colour as `exu.SetFog`: SetFog moves only the rendered
--- fog, so without this the TRN fog colour keeps showing below the horizon.
--- This writes the terrain's stored fog colour, exactly as a TRN `[Color] FogColor` key
--- would, so it also tints the engine's own cloud fade and, unless `exu.SetFog` has taken
--- fog over, the fog the engine applies on its next view change. The value is stored as
--- bytes, so channels round to 1/255. Every terrain load restores the TRN value, and closing
--- the Lua state puts it back too; re-apply it from Load() after a saved game is loaded.
--- Returns false (and changes nothing) on an unsupported game build.
--- ```lua
--- local ok = exu.SetSkyClearColor(0.1, 0.1, 0.2)
--- if not ok then print("SetSkyClearColor failed") end
--- ```
--- @param r number
--- @param g number
--- @param b number
--- @return boolean
function exu.SetSkyClearColor(r, g, b) end

--- Sets the colour the game clears its viewports to every frame from a color table.
--- ```lua
--- local ok = exu.SetSkyClearColor({ r = 0.1, g = 0.1, b = 0.2 })
--- if not ok then print("SetSkyClearColor failed") end
--- ```
--- @param color Color
--- @return boolean
function exu.SetSkyClearColor(color) end

--- Restores the terrain's own sky clear colour after exu.SetSkyClearColor.
--- ```lua
--- exu.ResetSkyClearColor()
--- ```
function exu.ResetSkyClearColor() end

--- Returns whether a named Ogre particle system currently exists.
--- ```lua
--- local hasSystem = exu.HasParticleSystem("exhaust1")
--- ```
--- @nodiscard
--- @param name string
--- @return boolean
function exu.HasParticleSystem(name) end

--- Creates a named Ogre particle system from a stock particle template and attaches it to an EXU-owned scene node.
--- The optional position defaults to `SetVector(0, 0, 0)`.
--- Returns false if the particle system already exists or if its reserved EXU node name is already in use.
--- ```lua
--- local ok = exu.CreateParticleSystem("exhaust1", "my_smoke_template", GetPosition(GetPlayerHandle()))
--- if not ok then print("particle system exists or could not be created") end
--- ```
--- @param name string
--- @param templateName string
--- @param position Vector? optional Battlezone simulation-space position; defaults to the world origin
--- @return boolean
function exu.CreateParticleSystem(name, templateName, position) end

--- Parse an in-memory Ogre script only when the mission needs it. No file I/O.
--- Resources belong to Ogre; callers must avoid duplicate declarations.
--- Returns false for unavailable runtime, invalid input or a native exception.
--- A true result means parsing completed; Ogre's log reports script diagnostics.
--- Script text must be nonempty, NUL-free and at most 1 MiB.
--- ```lua
--- local ok = exu.ParseResourceScript("material my_material\n{\n}\n", "my_mod_materials")
--- if not ok then print("script parse failed") end
--- ```
---@param text string
---@param source string Diagnostic source name, not a filesystem path to open.
---@param group? string Resource group (default "Modable").
---@return boolean completed
function exu.ParseResourceScript(text, source, group) end

--- True when Ogre already holds a particle_system template with this name.
--- Templates outlive mission Lua states, so check before reparsing a script.
--- ```lua
--- local hasTemplate = exu.HasParticleTemplate("my_smoke_template")
--- ```
---@param name string
---@return boolean
function exu.HasParticleTemplate(name) end

--- Destroys a named Ogre particle system and its EXU-owned scene node if present.
--- ```lua
--- local ok = exu.DestroyParticleSystem("exhaust1")
--- if not ok then print("exu.DestroyParticleSystem failed") end
--- ```
--- @param name string
--- @return boolean
function exu.DestroyParticleSystem(name) end

--- Moves the EXU-owned scene node for a named particle system to a Battlezone simulation-space position.
--- ```lua
--- local ok = exu.SetParticleSystemPosition("exhaust1", GetPosition(GetPlayerHandle()))
--- if not ok then print("exu.SetParticleSystemPosition failed") end
--- ```
--- @param name string
--- @param position Vector
--- @return boolean
function exu.SetParticleSystemPosition(name, position) end

--- Rotates the EXU-owned scene node for a named particle system to face the given Battlezone simulation-space direction.
--- ```lua
--- local ok = exu.SetParticleSystemDirection("exhaust1", SetVector(0, -1, 0))
--- if not ok then print("exu.SetParticleSystemDirection failed") end
--- ```
--- @param name string
--- @param direction Vector
--- @return boolean
function exu.SetParticleSystemDirection(name, direction) end

--- Enables or disables emission on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemEmitting("exhaust1", true)
--- if not ok then print("exu.SetParticleSystemEmitting failed") end
--- ```
--- @param name string
--- @param enabled boolean
--- @return boolean
function exu.SetParticleSystemEmitting(name, enabled) end

--- Enables or disables visibility on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemVisible("exhaust1", true)
--- if not ok then print("exu.SetParticleSystemVisible failed") end
--- ```
--- @param name string
--- @param enabled boolean
--- @return boolean
function exu.SetParticleSystemVisible(name, enabled) end

--- Sets the playback speed multiplier on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemSpeedFactor("exhaust1", 0.5)
--- if not ok then print("exu.SetParticleSystemSpeedFactor failed") end
--- ```
--- @param name string
--- @param speed number
--- @return boolean
function exu.SetParticleSystemSpeedFactor(name, speed) end

--- Controls whether particles remain in local space on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemKeepLocalSpace("exhaust1", true)
--- if not ok then print("exu.SetParticleSystemKeepLocalSpace failed") end
--- ```
--- @param name string
--- @param enabled boolean
--- @return boolean
function exu.SetParticleSystemKeepLocalSpace(name, enabled) end

--- Overrides the material on a named particle system.
--- The resource group defaults to "General".
--- ```lua
--- local ok = exu.SetParticleSystemMaterial("exhaust1", "my_smoke_material")
--- if not ok then print("SetParticleSystemMaterial failed") end
--- ```
--- @param name string
--- @param materialName string
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetParticleSystemMaterial(name, materialName, resourceGroup) end

--- Sets the render queue group on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemRenderQueueGroup("exhaust1", 60)
--- if not ok then print("exu.SetParticleSystemRenderQueueGroup failed") end
--- ```
--- @param name string
--- @param queueGroup integer
--- @return boolean
function exu.SetParticleSystemRenderQueueGroup(name, queueGroup) end

--- Raises the particle quota on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemParticleQuota("exhaust1", 200)
--- if not ok then print("exu.SetParticleSystemParticleQuota failed") end
--- ```
--- @param name string
--- @param quota integer
--- @return boolean
function exu.SetParticleSystemParticleQuota(name, quota) end

--- Sets the default billboard dimensions on a named particle system.
--- ```lua
--- local ok = exu.SetParticleSystemDefaultDimensions("exhaust1", 2, 2)
--- if not ok then print("SetParticleSystemDefaultDimensions failed") end
--- ```
--- @param name string
--- @param width number
--- @param height number
--- @return boolean
function exu.SetParticleSystemDefaultDimensions(name, width, height) end

--- Sets how long a named particle system keeps simulating after it leaves view.
--- Weather volumes should set a small timeout so an off-screen storm stops costing CPU.
--- ```lua
--- local ok = exu.SetParticleSystemNonVisibleUpdateTimeout("exhaust1", 2.0)
--- if not ok then print("exu.SetParticleSystemNonVisibleUpdateTimeout failed") end
--- ```
--- @param name string
--- @param seconds number
--- @return boolean
function exu.SetParticleSystemNonVisibleUpdateTimeout(name, seconds) end

--- Parents a named particle system's EXU-owned scene node to the active camera so the
--- emitter travels with the view without any per-frame Lua positioning.
--- The node does not inherit the camera's orientation, so a precipitation volume stays
--- world-aligned while the player looks around.
--- If the engine drives its Ogre camera without a scene node, the system is registered as a
--- native follower instead and `exu.UpdateParticleFollowers` must be called once per frame.
--- The offset is in camera-local units and defaults to `SetVector(0, 0, 0)`.
--- ```lua
--- local ok = exu.AttachParticleSystemToCamera("exhaust1")
--- if not ok then print("exu.AttachParticleSystemToCamera failed") end
--- ```
--- @param name string
--- @param offset Vector? optional
--- @return boolean
function exu.AttachParticleSystemToCamera(name, offset) end

--- Parents a named particle system's EXU-owned scene node to a game object's scene node.
--- The node inherits the object's orientation, so the offset is body-relative.
--- ```lua
--- local ok = exu.AttachParticleSystemToObject("exhaust1", GetPlayerHandle())
--- if not ok then print("exu.AttachParticleSystemToObject failed") end
--- ```
--- @param name string
--- @param h Handle
--- @param offset Vector? optional
--- @return boolean
function exu.AttachParticleSystemToObject(name, h, offset) end

--- Attaches a named particle system to a bone on a game object's skeleton.
--- Returns false when the object's mesh has no skeleton or no bone by that name.
--- ```lua
--- local ok = exu.AttachParticleSystemToBone("exhaust1", GetPlayerHandle(), "muzzle")
--- if not ok then print("attach failed") end
--- ```
--- @param name string
--- @param h Handle
--- @param boneName string
--- @param offset Vector? optional
--- @return boolean
function exu.AttachParticleSystemToBone(name, h, boneName, offset) end

--- Undoes any camera, object, or bone attachment and returns the particle system to its
--- own EXU-owned scene node under the scene root.
--- ```lua
--- local ok = exu.DetachParticleSystem("exhaust1")
--- if not ok then print("exu.DetachParticleSystem failed") end
--- ```
--- @param name string
--- @return boolean
function exu.DetachParticleSystem(name) end

--- Moves every camera-following particle system to the current camera position.
--- Call once per frame. Returns the number of systems moved, which is zero when every
--- camera attachment resolved to a real scene-graph parent and nothing needs chasing.
--- ```lua
--- local followerCount = exu.UpdateParticleFollowers() -- call from Update
--- ```
--- @return integer
function exu.UpdateParticleFollowers() end

--- Returns the number of emitters on a named particle system, or nil if it does not exist.
--- ```lua
--- local particleSystemEmitterCount = exu.GetParticleSystemEmitterCount("exhaust1")
--- if not particleSystemEmitterCount then return end
--- ```
--- @nodiscard
--- @param name string
--- @return integer?
function exu.GetParticleSystemEmitterCount(name) end

--- Returns the current emission rate of one emitter, or nil if it does not exist.
--- Emitter indices are zero based.
--- ```lua
--- local rate = exu.GetParticleEmitterEmissionRate("exhaust1", 0)
--- if not rate then return end
--- ```
--- @nodiscard
--- @param name string
--- @param emitterIndex integer
--- @return number?
function exu.GetParticleEmitterEmissionRate(name, emitterIndex) end

--- Enables or disables one emitter on a named particle system.
--- ```lua
--- local ok = exu.SetParticleEmitterEnabled("exhaust1", 0, true)
--- if not ok then print("exu.SetParticleEmitterEnabled failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param enabled boolean
--- @return boolean
function exu.SetParticleEmitterEnabled(name, emitterIndex, enabled) end

--- Sets the emission rate of one emitter, in particles per second.
--- This is how a single authored system varies storm intensity smoothly instead of
--- swapping between several near-identical templates.
--- ```lua
--- local ok = exu.SetParticleEmitterEmissionRate("exhaust1", 0, 25)
--- if not ok then print("exu.SetParticleEmitterEmissionRate failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param rate number
--- @return boolean
function exu.SetParticleEmitterEmissionRate(name, emitterIndex, rate) end

--- Sets the emission direction of one emitter. Used to drive precipitation from a wind vector.
--- ```lua
--- local ok = exu.SetParticleEmitterDirection("exhaust1", 0, SetVector(0, -1, 0))
--- if not ok then print("exu.SetParticleEmitterDirection failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param direction Vector
--- @return boolean
function exu.SetParticleEmitterDirection(name, emitterIndex, direction) end

--- Sets the emitter's position relative to the particle system's node.
--- ```lua
--- local ok = exu.SetParticleEmitterPosition("exhaust1", 0, GetPosition(GetPlayerHandle()))
--- if not ok then print("exu.SetParticleEmitterPosition failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param position Vector
--- @return boolean
function exu.SetParticleEmitterPosition(name, emitterIndex, position) end

--- Sets the emitter's particle velocity range. Omit the maximum for a fixed velocity.
--- ```lua
--- local ok = exu.SetParticleEmitterVelocity("exhaust1", 0, 2, 5)
--- if not ok then print("SetParticleEmitterVelocity failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param minVelocity number
--- @param maxVelocity number? optional
--- @return boolean
function exu.SetParticleEmitterVelocity(name, emitterIndex, minVelocity, maxVelocity) end

--- Sets the emitter's spread angle, in degrees, matching .particle script units.
--- ```lua
--- local ok = exu.SetParticleEmitterAngle("exhaust1", 0, 15)
--- if not ok then print("exu.SetParticleEmitterAngle failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param degrees number
--- @return boolean
function exu.SetParticleEmitterAngle(name, emitterIndex, degrees) end

--- Sets the emitter's particle lifetime range in seconds. Omit the maximum for a fixed lifetime.
--- ```lua
--- local ok = exu.SetParticleEmitterTimeToLive("exhaust1", 0, 1, 2)
--- if not ok then print("SetParticleEmitterTimeToLive failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param minTimeToLive number
--- @param maxTimeToLive number? optional
--- @return boolean
function exu.SetParticleEmitterTimeToLive(name, emitterIndex, minTimeToLive, maxTimeToLive) end

--- Sets the emitter's particle colour range. Pass one colour table for a flat colour, or two
--- for a random range. Also accepts r, g, b[, a] numbers for a flat colour.
--- ```lua
--- local ok = exu.SetParticleEmitterColor("exhaust1", 0, { r = 1, g = 0.5, b = 0, a = 1 }, { r = 0.2, g = 0.2, b = 0.2, a = 0 })
--- if not ok then print("SetParticleEmitterColor failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param startColor Color
--- @param endColor Color? optional
--- @return boolean
function exu.SetParticleEmitterColor(name, emitterIndex, startColor, endColor) end

--- Returns how many lightning bolts are active: the TRN `[LightningBolt] Bolts` count
--- (at most 16) unless `exu.StartLightning` or `exu.StopLightning` changed it. nil on failure.
--- ```lua
--- print("bolts", exu.GetLightningBolts())
--- ```
--- @nodiscard
--- @return integer?
function exu.GetLightningBolts() end

--- Returns the lightning colour as r, g, b, a integers 0-255, or nil on failure.
--- @nodiscard
--- @return integer? r
--- @return integer? g
--- @return integer? b
--- @return integer? a
function exu.GetLightningColor() end

--- Sets the lightning colour; channels are integers 0-255 and alpha defaults to 255.
--- Unlike BZ 1.5's palette index, Redux stores a packed ARGB colour.
--- Local only: in multiplayer each machine that should see it must call it.
--- ```lua
--- exu.SetLightningColor(160, 180, 255)
--- ```
--- @param r integer
--- @param g integer
--- @param b integer
--- @param a integer? optional, default 255
--- @return boolean
function exu.SetLightningColor(r, g, b, a) end

--- Returns the lightning strike distance in world units, or nil on failure.
--- @nodiscard
--- @return number?
function exu.GetLightningDistance() end

--- Sets the lightning strike distance in world units (finite, 0 or more).
--- @param distance number
--- @return boolean
function exu.SetLightningDistance(distance) end

--- Returns how long each bolt stays lit, in milliseconds, or nil on failure.
--- @nodiscard
--- @return number?
function exu.GetLightningDuration() end

--- Sets how long each bolt stays lit, in milliseconds (0 or more), as TRN `Duration` does.
--- A bolt picks up the new value at its next strike.
--- @param ms number
--- @return boolean
function exu.SetLightningDuration(ms) end

--- Returns the longest wait between strikes, in milliseconds, or nil on failure.
--- @nodiscard
--- @return number?
function exu.GetLightningMaxDelay() end

--- Sets the longest wait between strikes, in milliseconds (0 or more).
--- Each bolt draws a new wait between the min and max delay after it strikes.
--- Keep it at or above the min delay; with min, max and duration all 0 a bolt strikes every frame.
--- @param ms number
--- @return boolean
function exu.SetLightningMaxDelay(ms) end

--- Returns the shortest wait between strikes, in milliseconds, or nil on failure.
--- @nodiscard
--- @return number?
function exu.GetLightningMinDelay() end

--- Sets the shortest wait between strikes, in milliseconds (0 or more).
--- @param ms number
--- @return boolean
function exu.SetLightningMinDelay(ms) end

--- Returns the sound file each strike plays ("" for none), or nil on failure.
--- @nodiscard
--- @return string?
function exu.GetLightningSound() end

--- Sets the sound file each strike plays, at most 15 characters; "" is silent.
--- ```lua
--- exu.SetLightningSound("thunder.wav")
--- ```
--- @param sound string
--- @return boolean
function exu.SetLightningSound(sound) end

--- Starts lightning with `bolts` bolts (1-16). Without it, uses the TRN bolt count, or 1
--- when the TRN has none. New bolts wait a random min-max delay before their first strike.
--- Set colour, sound, distance and timing first on a TRN with no `[LightningBolt]` section:
--- they are 0 there. The stock renderer draws bolts with the TRN `Name` material or the
--- engine default. Local only: in multiplayer strikes are not synchronised.
--- ```lua
--- exu.SetLightningMinDelay(2000); exu.SetLightningMaxDelay(6000); exu.SetLightningDuration(150)
--- exu.StartLightning(4)
--- ```
--- @param bolts integer? optional, 1-16
--- @return boolean
function exu.StartLightning(bolts) end

--- Stops all lightning. `exu.StartLightning()` resumes the same bolt count.
--- The next mission's TRN sets lightning up again.
--- @return boolean
function exu.StopLightning() end

--- Makes every idle bolt strike on the next frame. Returns how many were triggered
--- (0 when lightning is stopped), or nil on failure.
--- @return integer?
function exu.FireLightning() end

--- Turns lava back on after `exu.StopLava`, or keeps the current lava. `damage` sets the
--- craft lava damage scale (stock 40, finite and 0 or more) until the mission ends.
--- Returns false when there is no lava material to restore (use `exu.SetLava`).
--- Changes craft damage only: AI pathing and terrain zones keep the TRN lava from load time.
--- Local only: in multiplayer each machine applies damage to its own craft, so every
--- machine should make the same call.
--- ```lua
--- exu.StartLava(80) -- twice the stock damage
--- ```
--- @param damage number? optional, default unchanged
--- @return boolean
function exu.StartLava(damage) end

--- Turns lava damage off (lava material -1) until `exu.StartLava` or the next terrain load.
--- @return boolean
function exu.StopLava() end

--- Makes terrain material `textureType` (0-15, the TRN `[NormalView] Lava` index) the lava
--- material for craft damage, and turns lava on. Same caveats as `exu.StartLava`.
--- @param textureType integer
--- @return boolean
function exu.SetLava(textureType) end

--- Adds `amount` (finite, 0 to 1000) of white screen glare, the overlay the sun lens flare
--- uses. It lasts one frame: the engine draws alpha min(total, 1) * 224 and clears it, so
--- call it every frame for a lasting effect. Not drawn in some camera modes.
--- Local only: it affects only the calling machine's screen.
--- ```lua
--- function Update() exu.SetGlare(0.5) end
--- ```
--- @param amount number
--- @return boolean
function exu.SetGlare(amount) end

--- Returns the Ogre type name of one emitter ("Box", "Point", "Ring", "HollowEllipsoid", ...),
--- or nil if it does not exist. The type determines which parameter names
--- `exu.SetParticleEmitterParameter` will accept.
--- ```lua
--- local particleEmitterType = exu.GetParticleEmitterType("exhaust1", 0)
--- if not particleEmitterType then return end
--- ```
--- @nodiscard
--- @param name string
--- @param emitterIndex integer
--- @return string?
function exu.GetParticleEmitterType(name, emitterIndex) end

--- Returns the list of parameter names one emitter publishes, or nil if it does not exist.
--- Use this to discover the exact spelling a given Ogre build registers rather than
--- assuming a name from documentation.
--- ```lua
--- local names = exu.GetParticleEmitterParameterNames("exhaust1", 0)
--- if names then print(table.concat(names, ", ")) end
--- ```
--- @nodiscard
--- @param name string
--- @param emitterIndex integer
--- @return string[]?
function exu.GetParticleEmitterParameterNames(name, emitterIndex) end

--- Reads one type-specific emitter parameter as the text Ogre stores it in.
--- Returns nil if the system, emitter, or Ogre entry point is unavailable; returns an
--- empty string for a parameter the emitter type does not publish.
--- ```lua
--- local particleEmitterParameter = exu.GetParticleEmitterParameter("exhaust1", 0, "width")
--- if not particleEmitterParameter then return end
--- ```
--- @nodiscard
--- @param name string
--- @param emitterIndex integer
--- @param parameter string
--- @return string?
function exu.GetParticleEmitterParameter(name, emitterIndex, parameter) end

--- Sets one type-specific emitter parameter, for properties the typed setters above do not
--- cover: Box `width`/`height`/`depth`, Ring `inner_width`/`inner_height`, and so on.
--- Numbers and booleans are converted to the text Ogre expects; strings pass through, so
--- vectors and colours are written as space-separated text ("20 0 -5").
--- Returns false for an unknown parameter or a value the emitter type refuses to parse.
--- ```lua
--- local ok = exu.SetParticleEmitterParameter("exhaust1", 0, "width", 20)
--- if not ok then print("exu.SetParticleEmitterParameter failed") end
--- ```
--- @param name string
--- @param emitterIndex integer
--- @param parameter string
--- @param value string|number|boolean
--- @return boolean
function exu.SetParticleEmitterParameter(name, emitterIndex, parameter, value) end

--- Returns the number of affectors on a named particle system, or nil if it does not exist.
--- Affectors are the `LinearForce`, `ColourInterpolator`, `Scaler`, `Rotator` and
--- `DirectionRandomiser` blocks authored into the .particle template.
--- ```lua
--- local particleSystemAffectorCount = exu.GetParticleSystemAffectorCount("exhaust1")
--- if not particleSystemAffectorCount then return end
--- ```
--- @nodiscard
--- @param name string
--- @return integer?
function exu.GetParticleSystemAffectorCount(name) end

--- Returns the Ogre type name of one affector ("LinearForce", "ColourInterpolator",
--- "Scaler", ...), or nil if it does not exist. Affector indices are zero based.
--- ```lua
--- local particleAffectorType = exu.GetParticleAffectorType("exhaust1", 0)
--- if not particleAffectorType then return end
--- ```
--- @nodiscard
--- @param name string
--- @param affectorIndex integer
--- @return string?
function exu.GetParticleAffectorType(name, affectorIndex) end

--- Returns the list of parameter names one affector publishes, or nil if it does not exist.
--- ```lua
--- local names = exu.GetParticleAffectorParameterNames("exhaust1", 0)
--- if names then print(table.concat(names, ", ")) end
--- ```
--- @nodiscard
--- @param name string
--- @param affectorIndex integer
--- @return string[]?
function exu.GetParticleAffectorParameterNames(name, affectorIndex) end

--- Reads one affector parameter as the text Ogre stores it in.
--- ```lua
--- local particleAffectorParameter = exu.GetParticleAffectorParameter("exhaust1", 0, "force_vector")
--- if not particleAffectorParameter then return end
--- ```
--- @nodiscard
--- @param name string
--- @param affectorIndex integer
--- @param parameter string
--- @return string?
function exu.GetParticleAffectorParameter(name, affectorIndex, parameter) end

--- Sets one affector parameter. This is how wind, colour-over-life and scale-over-life are
--- driven at runtime: `force_vector` on a LinearForce, `colour0`/`time0` on a
--- ColourInterpolator, `rate` on a Scaler.
--- Returns false for an unknown parameter or a value the affector type refuses to parse.
--- ```lua
--- local ok = exu.SetParticleAffectorParameter("exhaust1", 0, "force_vector", "0 5 0")
--- if not ok then print("exu.SetParticleAffectorParameter failed") end
--- ```
--- @param name string
--- @param affectorIndex integer
--- @param parameter string
--- @param value string|number|boolean
--- @return boolean
function exu.SetParticleAffectorParameter(name, affectorIndex, parameter, value) end

--- Returns whether scene bounding boxes are currently shown.
--- ```lua
--- local showBoundingBoxes = exu.GetShowBoundingBoxes()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetShowBoundingBoxes() end

--- Enables or disables scene bounding boxes.
--- ```lua
--- exu.SetShowBoundingBoxes(true)
--- ```
--- @param enabled boolean
function exu.SetShowBoundingBoxes(enabled) end

--- Returns whether debug shadows are currently shown.
--- ```lua
--- local showDebugShadows = exu.GetShowDebugShadows()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetShowDebugShadows() end

--- Enables or disables debug shadows.
--- ```lua
--- exu.SetShowDebugShadows(true)
--- ```
--- @param enabled boolean
function exu.SetShowDebugShadows(enabled) end

--- Returns whether the current active viewport renders shadows.
--- ```lua
--- local viewportShadowsEnabled = exu.GetViewportShadowsEnabled()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetViewportShadowsEnabled() end

--- Enables or disables shadow rendering on the current active viewport.
--- ```lua
--- exu.SetViewportShadowsEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetViewportShadowsEnabled(enabled) end

--- Returns the active viewport lighting mode: `default`, `enhanced`, or `retro`.
--- ```lua
--- local lightingMode = exu.GetLightingMode()
--- ```
--- @nodiscard
--- @return string
function exu.GetLightingMode() end

--- Sets the active viewport lighting mode.
--- Accepts `default`, `enhanced`, `retro`, or numeric aliases `1`, `2`, `3`.
--- ```lua
--- exu.SetLightingMode("enhanced")
--- ```
--- @param mode string|integer
function exu.SetLightingMode(mode) end

--- Returns whether the current active viewport is using the OG retro lighting material scheme.
--- ```lua
--- local retroLightingMode = exu.GetRetroLightingMode()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetRetroLightingMode() end

--- Switches the current active viewport between the normal material scheme and the `og-*` retro lighting scheme.
--- ```lua
--- exu.SetRetroLightingMode(true)
--- ```
--- @param enabled boolean
function exu.SetRetroLightingMode(enabled) end

--- Re-asserts the lighting mode last set via SetLightingMode/SetRetroLightingMode
--- on any active viewport whose material scheme has drifted (e.g. after satellite
--- view, sniper scope, or a resolution/scene reload rebuilds the viewport).
--- Idempotent and cheap — a no-op unless a viewport actually reverted — so it is
--- safe to call every frame from the mod's Update loop to keep retro/enhanced
--- lighting from flashing back to the default scheme.
--- ```lua
--- exu.EnforceLightingMode()
--- ```
function exu.EnforceLightingMode() end

--- Requests a render profile for this mission/session through OpenShim's
--- canonical renderer ownership. Accepts `inherit` (follow the player's
--- openshim.ini preference), `retro`, `redux`, or `enhanced`.
--- Overrides are cleared automatically when the mission ends; they never leak
--- into unrelated content. Requires OpenShim with the render-profile bridge;
--- without it the request maps onto the legacy local lighting-mode path.
--- ```lua
--- local applied = exu.RequestRenderProfile("enhanced")
--- if not applied then print("render profile request not applied") end
--- ```
--- @param profile string|integer  `inherit`|`retro`|`redux`|`enhanced` (or 0..3)
--- @return boolean applied
function exu.RequestRenderProfile(profile) end

--- Returns the content render profile currently requested for this session:
--- `inherit` when no content override is active, otherwise the override.
--- Returns `unknown` when OpenShim lacks the render-profile bridge.
--- ```lua
--- local requestedRenderProfile = exu.GetRequestedRenderProfile()
--- ```
--- @nodiscard
--- @return string
function exu.GetRequestedRenderProfile() end

--- Returns OpenShim's effective render profile: `retro`, `redux`, or
--- `enhanced`. This is what the renderer is actually honoring after resolving
--- user preference, content overrides, and compatibility constraints.
--- Returns `unknown` when OpenShim lacks the render-profile bridge.
--- ```lua
--- local effectiveRenderProfile = exu.GetEffectiveRenderProfile()
--- ```
--- @nodiscard
--- @return string
function exu.GetEffectiveRenderProfile() end

--- Returns the player's own render-profile preference from openshim.ini:
--- `retro`, `redux`, or `enhanced`; `unknown` without the bridge.
--- ```lua
--- local userRenderProfile = exu.GetUserRenderProfile()
--- ```
--- @nodiscard
--- @return string
function exu.GetUserRenderProfile() end

--- Returns whether OpenShim reports the requested profile as supported on the
--- active renderer backend. Enhanced and Retro are capability-gated; Redux is
--- always available. Without the bridge this returns false.
--- ```lua
--- local canEnhance = exu.SupportsRenderProfile("enhanced")
--- ```
--- @nodiscard
--- @param profile string|integer
--- @return boolean
function exu.SupportsRenderProfile(profile) end

--- Returns a table of OpenShim Enhanced renderer capabilities, e.g.
--- `{ schemeRewrite=true, normalSharpening=true, linearLighting=false,
---    terrainEnhanced=false, objectEnhanced=false, modernPssm=false,
---    lightSelection=false, iblResources=false, mask=3 }`.
--- DX9 Enhanced intentionally reports its frozen legacy subset; absence of a
--- DX11-only bit is expected, not an error. All false + mask=0 means the shim
--- predates the render-profile bridge.
--- ```lua
--- local caps = exu.GetRenderCapabilities()
--- if caps.terrainEnhanced then print("enhanced terrain available") end
--- ```
--- @nodiscard
--- @return table
function exu.GetRenderCapabilities() end

--- Asks OpenShim to turn a renderer effect on or off for this mission.
---
--- These are requests, not commands. EXU owns no rendering: it forwards the intent and
--- OpenShim decides whether the effect is possible on the current renderer and build.
--- Returns true when the request was *recorded*, which is not the same as the effect
--- running - use `exu.GetRenderEffectStatus` for that.
---
--- With no OpenShim present, or one that predates the render-effect ABI, this is a clean
--- no-op returning false and the mission carries on.
---
--- Effects: `"ssao"`, `"depth_haze"`, `"soft_particles"`. An unknown name is an error.
--- ```lua
--- local ok = exu.SetRenderEffectEnabled("ssao", true)
--- if not ok then print("exu.SetRenderEffectEnabled failed") end
--- ```
--- @param effect string
--- @param enabled boolean
--- @return boolean
function exu.SetRenderEffectEnabled(effect, enabled) end

--- Sets a tuning value on a renderer effect. OpenShim owns the valid range and clamps to
--- it, so an out-of-range value is corrected rather than refused.
---
--- Parameters are semantic: `"strength"`, `"radius"`, `"fade_start"`, `"fade_end"`,
--- `"quality"`. There is deliberately no way to name a shader constant, sampler or
--- compositor pass from Lua. An unknown name is an error.
--- ```lua
--- local ok = exu.SetRenderEffectFloat("ssao", "strength", 0.5)
--- if not ok then print("exu.SetRenderEffectFloat failed") end
--- ```
--- @param effect string
--- @param parameter string
--- @param value number
--- @return boolean
function exu.SetRenderEffectFloat(effect, parameter, value) end

--- Returns `requested, supported, effective, reason` for one renderer effect.
---
--- Three flags, not one, because they answer three different questions: whether this
--- mission asked for the effect, whether this machine can do it, and whether it is
--- actually running. A single boolean cannot tell a DX9 machine from a machine where
--- nobody enabled the effect.
---
--- `reason` is one of `"effective"`, `"not-requested"`, `"unsupported-renderer"`,
--- `"scene-depth-unavailable"`, `"unsupported-build"`, `"not-implemented"`,
--- `"unknown-effect"`, `"pending"`, `"openshim-unavailable"`, or `"unknown"` for a code
--- this build does not recognise.
---
--- NOTE: no renderer effect is implemented yet, so today every effect answers
--- `supported = false` with `"not-implemented"` (or `"openshim-unavailable"`). Missions
--- should be written to carry on regardless; they will start working unchanged once a
--- renderer feature ships.
--- ```lua
--- local requested, supported, effective, reason = exu.GetRenderEffectStatus("ssao")
--- if not effective then print(reason) end
--- ```
--- @nodiscard
--- @param effect string
--- @return boolean requested, boolean supported, boolean effective, string reason
function exu.GetRenderEffectStatus(effect) end

--- Drops every renderer-effect request this mission made.
---
--- OpenShim also clears these from its own mission lifecycle, so this is belt and braces
--- rather than the only defence against one mission's requests leaking into the next.
--- ```lua
--- local reset = exu.ResetRenderEffects()
--- ```
--- @return boolean
function exu.ResetRenderEffects() end

--- Returns the current scene visibility mask.
--- ```lua
--- local sceneVisibilityMask = exu.GetSceneVisibilityMask()
--- if not sceneVisibilityMask then return end
--- ```
--- @nodiscard
--- @return integer | nil
function exu.GetSceneVisibilityMask() end

--- Sets the current scene visibility mask.
--- ```lua
--- exu.SetSceneVisibilityMask(0)
--- ```
--- @param mask integer
function exu.SetSceneVisibilityMask(mask) end

--- Returns whether the scene manager currently has a skybox node.
--- ```lua
--- local hasSkyBoxNode = exu.HasSkyBoxNode()
--- ```
--- @nodiscard
--- @return boolean
function exu.HasSkyBoxNode() end

--- Returns whether the scene manager currently has a skydome node.
--- ```lua
--- local hasSkyDomeNode = exu.HasSkyDomeNode()
--- ```
--- @nodiscard
--- @return boolean
function exu.HasSkyDomeNode() end

--- Returns whether the scene manager currently has a skyplane node.
--- ```lua
--- local hasSkyPlaneNode = exu.HasSkyPlaneNode()
--- ```
--- @nodiscard
--- @return boolean
function exu.HasSkyPlaneNode() end

--- Overlay
---
--- These functions expose Ogre overlays for custom HUD and UI work.

--- Creates a new named overlay.
--- ```lua
--- if not exu.CreateOverlay("myOverlay") then print("overlay exists or manager unavailable") end
--- ```
--- @param name string
--- @return boolean
function exu.CreateOverlay(name) end

--- Destroys an existing named overlay.
--- ```lua
--- exu.DestroyOverlay("myOverlay")
--- ```
--- @param name string
function exu.DestroyOverlay(name) end

--- Shows an existing named overlay.
--- ```lua
--- exu.ShowOverlay("myOverlay")
--- ```
--- @param name string
function exu.ShowOverlay(name) end

--- Hides an existing named overlay.
--- ```lua
--- exu.HideOverlay("myOverlay")
--- ```
--- @param name string
function exu.HideOverlay(name) end

--- Sets the z-order of an overlay.
--- Valid range is 0 to 650.
--- ```lua
--- exu.SetOverlayZOrder("myOverlay", 100)
--- ```
--- @param name string
--- @param zOrder integer
function exu.SetOverlayZOrder(name, zOrder) end

--- Sets the scroll offset on an overlay.
--- ```lua
--- exu.SetOverlayScroll("myOverlay", 0.1, 0.0)
--- ```
--- @param name string
--- @param x number
--- @param y number
function exu.SetOverlayScroll(name, x, y) end

--- Creates a new overlay element.
--- Useful built-in element types include `"Panel"`, `"BorderPanel"`, and `"TextArea"`.
--- ```lua
--- if not exu.CreateOverlayElement("Panel", "myPanel") then print("element not created") end
--- ```
--- @param typeName string
--- @param instanceName string
--- @return boolean
function exu.CreateOverlayElement(typeName, instanceName) end

--- Destroys an existing overlay element by name.
--- ```lua
--- exu.DestroyOverlayElement("myPanel")
--- ```
--- @param name string
function exu.DestroyOverlayElement(name) end

--- Returns whether an overlay element exists by name.
--- ```lua
--- local exists = exu.HasOverlayElement("myPanel")
--- ```
--- @nodiscard
--- @param name string
--- @return boolean
function exu.HasOverlayElement(name) end

--- Attaches a container element to an overlay as a 2D root.
--- The container must have been created through `CreateOverlayElement` as `"Panel"` or `"BorderPanel"`.
--- ```lua
--- exu.AddOverlay2D("myOverlay", "myPanel")
--- ```
--- @param overlayName string
--- @param containerName string
function exu.AddOverlay2D(overlayName, containerName) end

--- Removes a container element from an overlay's 2D roots.
--- ```lua
--- exu.RemoveOverlay2D("myOverlay", "myPanel")
--- ```
--- @param overlayName string
--- @param containerName string
function exu.RemoveOverlay2D(overlayName, containerName) end

--- Adds a child element to a parent container.
--- The parent container must have been created through `CreateOverlayElement` as `"Panel"` or `"BorderPanel"`.
--- ```lua
--- exu.AddOverlayElementChild("myPanel", "myText")
--- ```
--- @param parentName string
--- @param childName string
function exu.AddOverlayElementChild(parentName, childName) end

--- Removes a child element from a parent container by child name.
--- ```lua
--- exu.RemoveOverlayElementChild("myPanel", "myText")
--- ```
--- @param parentName string
--- @param childName string
function exu.RemoveOverlayElementChild(parentName, childName) end

--- Shows an overlay element by name.
--- ```lua
--- exu.ShowOverlayElement("myPanel")
--- ```
--- @param name string
function exu.ShowOverlayElement(name) end

--- Hides an overlay element by name.
--- ```lua
--- exu.HideOverlayElement("myPanel")
--- ```
--- @param name string
function exu.HideOverlayElement(name) end

--- Sets the metrics mode for an overlay element.
--- See `exu.OVERLAY_METRICS`.
--- ```lua
--- exu.SetOverlayMetricsMode("myOverlay", exu.OVERLAY_METRICS.PIXELS)
--- ```
--- @param name string
--- @param mode OverlayMetricsMode | integer
function exu.SetOverlayMetricsMode(name, mode) end

--- Sets the position of an overlay element.
--- ```lua
--- exu.SetOverlayPosition("myPanel", 0.05, 0.05)
--- ```
--- @param name string
--- @param left number
--- @param top number
function exu.SetOverlayPosition(name, left, top) end

--- Sets the dimensions of an overlay element.
--- ```lua
--- exu.SetOverlayDimensions("myPanel", 0.2, 0.1)
--- ```
--- @param name string
--- @param width number
--- @param height number
function exu.SetOverlayDimensions(name, width, height) end

--- Sets the material name of an overlay element.
--- ```lua
--- exu.SetOverlayMaterial("myPanel", "my_panel_material")
--- ```
--- @param name string
--- @param materialName string
function exu.SetOverlayMaterial(name, materialName) end

--- Sets a raw Ogre overlay parameter by name.
--- This is the escape hatch for element-specific options not wrapped by dedicated helpers.
--- Value may be a string, finite number, or boolean and is converted to Ogre's string parameter form.
--- Useful examples include:
--- `Panel`: `uv_coords`, `tiling`, `transparent`
--- `BorderPanel`: `border_size`, `border_material`, `border_*_uv`
--- `TextArea`: `alignment`, `space_width`, `colour_top`, `colour_bottom`
--- Base element params: `horz_align`, `vert_align`, `visible`, `caption`
--- ```lua
--- local ok = exu.SetOverlayParameter("myPanel", "horz_align", "center")
--- if not ok then print("parameter rejected") end
--- ```
--- @param name string
--- @param parameterName string
--- @param value string | number | boolean
--- @return boolean success True when Ogre accepted the parameter.
function exu.SetOverlayParameter(name, parameterName, value) end

--- Sets the color of an overlay element.
--- Can take either four number parameters or a color table.
--- ```lua
--- exu.SetOverlayColor("myPanel", 0.2, 0.8, 0.2, 0.5)
--- ```
--- @param name string
--- @param r number
--- @param g number
--- @param b number
--- @param a number? optional
function exu.SetOverlayColor(name, r, g, b, a) end

--- Sets the color of an overlay element.
--- Can take either four number parameters or a color table.
--- ```lua
--- exu.SetOverlayColor("myPanel", { r = 0.2, g = 0.8, b = 0.2, a = 0.5 })
--- ```
--- @param name string
--- @param color Color
function exu.SetOverlayColor(name, color) end

--- Sets the caption text of an overlay element.
--- This is primarily useful for text areas.
--- ```lua
--- exu.SetOverlayCaption("myText", "Hello")
--- ```
--- @param name string
--- @param text string
function exu.SetOverlayCaption(name, text) end

--- Sets the font name of a text area created through `CreateOverlayElement("TextArea", ...)`.
---
--- `"CRBZoneOverlayFont"` is EXU's default overlay font (the name is kept for Campaign Reimagined
--- compatibility). EXU builds it on first use from `CRBZoneOverlay.fontdef` or `BZONE.ttf`, found first in
--- directories registered with `exu.AddOverlayFontDirectory`, then in an `OverlayFont` folder next to EXU,
--- the game root, `addon`, `mods`, `packaged_mods` or the Steam Workshop content folder; with none of those
--- it is built from the stock `bzfont.dds`. Any other name must already be a loaded Ogre font, for example
--- one defined by a `.fontdef` in a registered directory.
--- ```lua
--- local ok = exu.SetOverlayTextFont("myText", "my_font")
--- if not ok then print("unknown font") end
--- ```
--- @param name string
--- @param fontName string
--- @return boolean success True when the font was bound successfully; false for an unknown font name.
function exu.SetOverlayTextFont(name, fontName) end

--- Registers a directory of overlay font assets for this mission (cleared when the mission's Lua state closes).
--- `directory` is absolute or relative to the game root. The directory, and an `OverlayFont` folder inside it,
--- are searched before EXU's built-in locations for the default font's assets; call it before the first
--- `SetOverlayTextFont`, since the default font is built once per game session. Every other `.fontdef` in the
--- directory is parsed, so the fonts it defines can be bound by name with `exu.SetOverlayTextFont`.
--- ```lua
--- local registered, parsedScripts = exu.AddOverlayFontDirectory("Addon\\my_fonts")
--- if not registered then print("font directory not found") end
--- ```
--- @param directory string
--- @return boolean registered false when the path is not an existing directory or Ogre rejected it
--- @return integer parsedScripts number of font scripts parsed from the directory
function exu.AddOverlayFontDirectory(directory) end

--- Sets the top and bottom text colors of a text area created through `CreateOverlayElement("TextArea", ...)`.
--- Can take either four number parameters or a color table.
--- ```lua
--- exu.SetOverlayTextColor("myText", 1.0, 1.0, 0.0, 1.0)
--- ```
--- @param name string
--- @param r number
--- @param g number
--- @param b number
--- @param a number? optional
function exu.SetOverlayTextColor(name, r, g, b, a) end

--- Sets the top and bottom text colors of a text area created through `CreateOverlayElement("TextArea", ...)`.
--- Can take either four number parameters or a color table.
--- ```lua
--- exu.SetOverlayTextColor("myText", { r = 1.0, g = 1.0, b = 0.0, a = 1.0 })
--- ```
--- @param name string
--- @param color Color
function exu.SetOverlayTextColor(name, color) end

--- Sets the character height of a text area created through `CreateOverlayElement("TextArea", ...)`.
--- ```lua
--- exu.SetOverlayTextCharHeight("myText", 0.03)
--- ```
--- @param name string
--- @param charHeight number
function exu.SetOverlayTextCharHeight(name, charHeight) end

--- GameObject
---
--- These functions act on game objects (handles) to query and modify various attributes.

--- Sets the local player's user ship to the given handle.
--- ```lua
--- exu.SetAsUser(GetHandle("tank1"))
--- ```
--- @param h Handle
function exu.SetAsUser(h) end

--- Gets if the given comm tower is powered or not, throws an error if the handle is not a comm tower.
--- Returns false for a destroyed tower.
--- ```lua
--- local powered = exu.IsCommTowerPowered(GetHandle("comm1"))
--- ```
--- @nodiscard
--- @param h Handle
--- @return boolean
function exu.IsCommTowerPowered(h) end

--- Whether the object's objective marker is on (SetObjectiveOn). Stock Lua
--- can set the flag but not read it, and stock ObjectiveObjects() is broken.
--- The engine's objective list holds 32 objects and is not bounds-checked, so
--- keep the number of marked objects well under that.
--- ```lua
--- local isOn = exu.IsObjectiveOn(GetHandle("tank1"))
--- if isOn == nil then return end
--- ```
--- @param h handle
--- @return boolean? on nil for an invalid handle or an unsupported build
function exu.IsObjectiveOn(h) end

--- Converts a GameObject* (as returned by exu.GetObj) into a handle that can be used in lua.
--- Returns nil when obj is not a live GameObject.
--- ```lua
--- local h = exu.GetHandle(exu.GetObj(GetPlayerHandle()))
--- if not h then return end
--- ```
--- @nodiscard
--- @param obj GameObject*
--- @return Handle?
function exu.GetHandle(obj) end

--- Gets whether the object's Ogre entity is currently visible.
--- ```lua
--- local visible = exu.GetEntityVisible(GetPlayerHandle())
--- if visible == nil then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return boolean | nil
function exu.GetEntityVisible(h) end

--- Gets whether the object's Ogre entity currently casts shadows.
--- ```lua
--- local castShadows = exu.GetEntityCastShadows(GetPlayerHandle())
--- if castShadows == nil then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return boolean | nil
function exu.GetEntityCastShadows(h) end

--- Gets the object's Ogre rendering distance.
--- ```lua
--- local entityRenderingDistance = exu.GetEntityRenderingDistance(GetPlayerHandle())
--- if not entityRenderingDistance then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return number | nil
function exu.GetEntityRenderingDistance(h) end

--- Gets the object's Ogre visibility flags mask.
--- ```lua
--- local entityVisibilityFlags = exu.GetEntityVisibilityFlags(GetPlayerHandle())
--- if not entityVisibilityFlags then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return integer | nil
function exu.GetEntityVisibilityFlags(h) end

--- Gets the object's Ogre query flags mask.
--- ```lua
--- local entityQueryFlags = exu.GetEntityQueryFlags(GetPlayerHandle())
--- if not entityQueryFlags then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return integer | nil
function exu.GetEntityQueryFlags(h) end

--- Gets the object's Ogre render queue group.
--- ```lua
--- local entityRenderQueueGroup = exu.GetEntityRenderQueueGroup(GetPlayerHandle())
--- if not entityRenderQueueGroup then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return integer | nil
function exu.GetEntityRenderQueueGroup(h) end

--- Sets whether the object's Ogre entity is visible.
--- ```lua
--- exu.SetEntityVisible(GetPlayerHandle(), false)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param visible boolean
function exu.SetEntityVisible(h, visible) end

--- Sets whether the object's Ogre entity casts shadows.
--- ```lua
--- exu.SetEntityCastShadows(GetPlayerHandle(), true)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param castShadows boolean
function exu.SetEntityCastShadows(h, castShadows) end

--- Sets the object's Ogre rendering distance.
--- ```lua
--- exu.SetEntityRenderingDistance(GetPlayerHandle(), 500)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param distance number
function exu.SetEntityRenderingDistance(h, distance) end

--- Sets the object's Ogre visibility flags mask.
--- ```lua
--- exu.SetEntityVisibilityFlags(GetPlayerHandle(), 1)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param flags integer
function exu.SetEntityVisibilityFlags(h, flags) end

--- Sets the object's Ogre query flags mask.
--- ```lua
--- exu.SetEntityQueryFlags(GetPlayerHandle(), 1)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param flags integer
function exu.SetEntityQueryFlags(h, flags) end

--- Sets the object's Ogre render queue group.
--- ```lua
--- exu.SetEntityRenderQueueGroup(GetPlayerHandle(), 50)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param group integer
function exu.SetEntityRenderQueueGroup(h, group) end

--- Sets whether a specific sub-entity is visible.
--- ```lua
--- exu.SetSubEntityVisible(GetPlayerHandle(), 0, false)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param subEntityIndex integer
--- @param visible boolean
function exu.SetSubEntityVisible(h, subEntityIndex, visible) end

--- Moves sub-entities into their own Ogre render queue group, ahead of the
--- entity's group (Ogre's Entity::_updateRenderQueue prefers a sub-entity's
--- own group). The selector is a 0-based sub-entity index, or a material name
--- (case-insensitive) that selects every sub-entity drawn with it. With
--- `exu.animation.TargetCockpit(h)` this draws a cockpit glass submesh after
--- the world (the rest of the cockpit stays in group 10, before terrain, and
--- its depth hides the glass behind the frame and dash). The cockpit entity
--- is rebuilt on vehicle and view changes, which drops the setting: re-apply
--- it while the view is up. Returns how many sub-entities changed (0 when the
--- entity is absent or nothing matched) and the first changed index.
--- ```lua
--- local changed, firstIndex = exu.SetSubEntityRenderQueueGroup(exu.animation.TargetCockpit(GetPlayerHandle()), "glass", 80)
--- if changed == 0 then print("no sub-entity matched") end
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param selector integer|string Sub-entity index or material name
--- @param group integer Render queue group 0..255
--- @param priority integer? Optional priority 0..65535 within the group
--- @return integer changed
--- @return integer? firstIndex
function exu.SetSubEntityRenderQueueGroup(h, selector, group, priority) end

--- Returns a sub-entity's render queue group, and whether it is the
--- sub-entity's own (false while it follows its entity's group).
--- Returns nothing when the entity is absent.
--- ```lua
--- local group, isOwnGroup = exu.GetSubEntityRenderQueueGroup(GetPlayerHandle(), 0)
--- if not group then return end
--- ```
--- @param h Handle|ExuAnimationTarget
--- @param subEntityIndex integer
--- @return integer? group
--- @return boolean? isOwnGroup
function exu.GetSubEntityRenderQueueGroup(h, subEntityIndex) end

--- Gets the number of Ogre sub-entities attached to the object's render entity.
--- ```lua
--- local count = exu.GetSubEntityCount(GetPlayerHandle())
--- if not count then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return integer | nil
function exu.GetSubEntityCount(h) end

--- Gets the number of Ogre sub-entities attached to the object's render entity.
--- ```lua
--- local count = exu.GetNumSubEntities(GetPlayerHandle())
--- if not count then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @return integer | nil
function exu.GetNumSubEntities(h) end

--- Gets the material name used by the given sub-entity.
--- ```lua
--- local material = exu.GetSubEntityMaterial(GetPlayerHandle(), 0)
--- if not material then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param subEntityIndex integer
--- @return string | nil
function exu.GetSubEntityMaterial(h, subEntityIndex) end

--- Gets the material name used by the object's render entity.
--- If no sub-entity index is provided, returns the material on sub-entity 0.
--- ```lua
--- local material = exu.GetMaterialName(GetPlayerHandle())
--- if not material then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param subEntityIndex integer? optional
--- @return string | nil
function exu.GetMaterialName(h, subEntityIndex) end

--- Sets the material name used by the object's whole render entity.
--- The resource group defaults to "General".
--- ```lua
--- exu.SetEntityMaterial(GetPlayerHandle(), "my_material")
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param materialName string
--- @param resourceGroup string? optional
function exu.SetEntityMaterial(h, materialName, resourceGroup) end

--- Builds one mission-scoped Ogre StaticGeometry object from a mesh/material
--- template and a bulk list of transforms. Each instance accepts `position`
--- (`{x,y,z}`), `yaw` in radians or `orientation` (`{w,x,y,z}`), and `scale`
--- as either one number or `{x,y,z}`. Options accept `regionDimensions`,
--- `origin`, `renderingDistance`, `castShadows`, and `visible`.
--- Positions and `origin` are ordinary simulation coordinates, the same ones
--- `GetPosition` returns; EXU converts them into Redux's render space.
--- Reusing an EXU-owned name replaces its previous geometry. On success the
--- returned table includes `instanceCount`, estimated `regionCount`, and
--- `buildMilliseconds`, and `totalMilliseconds` (high-resolution native time,
--- including argument conversion, replacement, entity creation and logging;
--- excludes caller Lua table preparation). Mission Lua teardown destroys all tracked geometry.
--- ```lua
--- local info, err = exu.CreateStaticGeometry("rocks1", "my_rock.mesh", nil, { { position = GetPosition(GetPlayerHandle()), yaw = 0.5, scale = 1.0 } })
--- if not info then print(err) end
--- ```
--- @param name string
--- @param mesh string
--- @param material string|nil
--- @param instances table[]
--- @param options? table
--- @return table|nil info
--- @return string? error
function exu.CreateStaticGeometry(name, mesh, material, instances, options) end

--- Enables/disables a bounded local ExplosionClass::Build event queue for this
--- mission. Opt-in; does not change explosions or send network messages. False
--- means this supported-build/entry-hook capability is unavailable. Disabling
--- clears queued events. Lua is never called from the engine hook.
--- ```lua
--- local available = exu.SetExplosionEventsEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean available
function exu.SetExplosionEventsEnabled(enabled) end

--- Drains up to 64 local explosion creations (default 64), returning plain copied
--- data: {odf,x,y,z,radius,sequence}. Coordinates are simulation space; radius is
--- the loaded ExplosionClass damageRadius, including inherited defaults. Zero
--- radius includes cosmetic impacts; callers decide which effects to use.
--- The queue holds 512 events and drops oldest on overflow. dropped is cumulative
--- since enable; pending is the remaining queue size. No pointers escape.
--- ```lua
--- local events, dropped, pending = exu.DrainExplosionEvents(16)
--- for _, e in ipairs(events) do print(e.odf, e.radius) end
--- ```
--- @param limit? integer
--- @return table[] events
--- @return number dropped
--- @return integer pending
function exu.DrainExplosionEvents(limit) end

--- Destroys one EXU-owned StaticGeometry object.
--- ```lua
--- local destroyed = exu.DestroyStaticGeometry("rocks1")
--- ```
--- @param name string
--- @return boolean destroyed
function exu.DestroyStaticGeometry(name) end

--- Destroys all StaticGeometry objects owned by the current mission Lua state.
--- ```lua
--- local trackedCount = exu.DestroyAllStaticGeometry()
--- ```
--- @return integer trackedCount
function exu.DestroyAllStaticGeometry() end

--- Returns build and lifecycle information for an EXU-owned StaticGeometry object.
--- ```lua
--- local info = exu.GetStaticGeometryInfo("rocks1")
--- if not info then return end
--- ```
--- @param name string
--- @return table|nil info
function exu.GetStaticGeometryInfo(name) end

--- Shows or hides all regions in an EXU-owned StaticGeometry object.
--- ```lua
--- local changed = exu.SetStaticGeometryVisible("rocks1", false)
--- ```
--- @param name string
--- @param visible boolean
--- @return boolean changed
function exu.SetStaticGeometryVisible(name, visible) end

--- @class ExuLightColour
--- @field r number
--- @field g number
--- @field b number

--- Creates a free-standing, mission-scoped world point light: an Ogre point
--- light on its own scene node, not attached to any game object. `position` is
--- an ordinary simulation position (the same coordinates `GetPosition`
--- returns); EXU converts it into Redux's render space. `colour` defaults to
--- white and applies to both diffuse and specular; components may exceed 1.
--- `range` (default 50, at most 2000) uses the engine's own falloff
--- (constant 1, linear 0, quadratic 15/range^2, 1/16 brightness at the edge).
--- At most 8 lights may exist at once. Every light competes with the sun,
--- headlights and weapon/explosion lights for each renderable's light slots
--- (8 under stock high-quality materials, sun only under low), so keep ranges
--- modest and overlap low; see Docs/POINT_LIGHTS.md. Lights never cast
--- shadows and are not saved: recreate them from your load path.
--- Returns false when the name is taken, the cap is reached, or the scene
--- manager or render origin is unavailable. Lua teardown destroys every light.
--- ```lua
--- local created = exu.CreatePointLight("beacon1", GetPosition(GetPlayerHandle()), { r = 1.0, g = 0.6, b = 0.2 }, 60)
--- if not created then print("name taken or light cap reached") end
--- ```
--- @param name string 1-64 printable characters
--- @param position Vector|{x:number,y:number,z:number}
--- @param colour? ExuLightColour
--- @param range? number
--- @return boolean created
function exu.CreatePointLight(name, position, colour, range) end

--- Moves an EXU point light to a simulation position.
--- ```lua
--- local changed = exu.SetPointLightPosition("beacon1", GetPosition(GetPlayerHandle()))
--- ```
--- @param name string
--- @param position Vector|{x:number,y:number,z:number}|number vector, or x
--- @param y? number
--- @param z? number
--- @return boolean changed
function exu.SetPointLightPosition(name, position, y, z) end

--- Sets an EXU point light's diffuse colour, and its specular colour
--- (defaults to the diffuse colour). The power scale multiplies both.
--- ```lua
--- local changed = exu.SetPointLightColour("beacon1", { r = 1.0, g = 0.6, b = 0.2 })
--- ```
--- @param name string
--- @param diffuse ExuLightColour
--- @param specular? ExuLightColour
--- @return boolean changed
function exu.SetPointLightColour(name, diffuse, specular) end

--- Sets an EXU point light's range and resets its falloff to the engine curve
--- for that range (see `CreatePointLight`).
--- ```lua
--- local changed = exu.SetPointLightRange("beacon1", 80)
--- ```
--- @param name string
--- @param range number
--- @return boolean changed
function exu.SetPointLightRange(name, range) end

--- Sets an EXU point light's range and raw Ogre attenuation terms:
--- brightness = 1 / (constant + linear*d + quadratic*d^2) inside `range`.
--- Terms must be finite and non-negative, not all zero.
--- ```lua
--- local changed = exu.SetPointLightAttenuation("beacon1", 80, 1.0, 0.0, 0.005)
--- ```
--- @param name string
--- @param range number
--- @param constant number
--- @param linear number
--- @param quadratic number
--- @return boolean changed
function exu.SetPointLightAttenuation(name, range, constant, linear, quadratic) end

--- Scales an EXU point light's brightness (0..100, default 1). Applied to the
--- uploaded colours, because no Redux or Campaign Reimagined shader reads
--- Ogre's light power; 0 makes the light dark but it still takes a slot.
--- ```lua
--- local changed = exu.SetPointLightPowerScale("beacon1", 1.5)
--- ```
--- @param name string
--- @param scale number
--- @return boolean changed
function exu.SetPointLightPowerScale(name, scale) end

--- Shows or hides an EXU point light. A hidden light takes no light slot.
--- ```lua
--- local changed = exu.SetPointLightVisible("beacon1", false)
--- ```
--- @param name string
--- @param visible boolean
--- @return boolean changed
function exu.SetPointLightVisible(name, visible) end

--- Destroys one EXU point light.
--- ```lua
--- local destroyed = exu.DestroyPointLight("beacon1")
--- ```
--- @param name string
--- @return boolean destroyed
function exu.DestroyPointLight(name) end

--- Destroys every EXU point light owned by the current mission Lua state.
--- ```lua
--- local destroyedCount = exu.DestroyAllPointLights()
--- ```
--- @return integer destroyedCount
function exu.DestroyAllPointLights() end

--- Whether an EXU point light with this name currently exists in the scene.
--- ```lua
--- local exists = exu.HasPointLight("beacon1")
--- ```
--- @param name string
--- @return boolean exists
function exu.HasPointLight(name) end

--- Returns `{ name, position, diffuse, specular, powerScale, range, constant,
--- linear, quadratic, customAttenuation, visible }` for an EXU point light, or nil.
--- ```lua
--- local info = exu.GetPointLightInfo("beacon1")
--- if not info then return end
--- ```
--- @param name string
--- @return table|nil info
function exu.GetPointLightInfo(name) end

--- Light-slot measurement aid. Returns `{ count, max, sceneManager }` and, when
--- readable, `frustumLights` (every light Ogre found touching the camera
--- frustum on the last render: sun, headlights, weapon lights and EXU's) and
--- `ownedInFrustum` (how many of those are EXU point lights).
--- ```lua
--- local stats = exu.GetPointLightStats()
--- ```
--- @return table stats
function exu.GetPointLightStats() end

--- Sets the material name used by a specific sub-entity.
--- The resource group defaults to "General".
--- ```lua
--- exu.SetSubEntityMaterial(GetPlayerHandle(), 0, "my_material")
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param subEntityIndex integer
--- @param materialName string
--- @param resourceGroup string? optional
function exu.SetSubEntityMaterial(h, subEntityIndex, materialName, resourceGroup) end

--- Sets the material name used by the object's render entity.
--- If a sub-entity index is provided, changes only that sub-entity. Otherwise changes the whole entity.
--- The resource group defaults to "General".
--- ```lua
--- exu.SetMaterialName(GetPlayerHandle(), "my_material")
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param materialName string
--- @param subEntityIndex integer? optional
--- @param resourceGroup string? optional
function exu.SetMaterialName(h, materialName, subEntityIndex, resourceGroup) end

--- Returns whether a named Ogre material can be resolved from the given resource group.
--- The resource group defaults to "General".
--- ```lua
--- local exists = exu.MaterialExists("my_material")
--- ```
--- @nodiscard
--- @param materialName string
--- @param resourceGroup string? optional
--- @return boolean
function exu.MaterialExists(materialName, resourceGroup) end

--- Clones a named Ogre material at runtime.
--- This is useful when you want per-team or per-state variants without authoring separate material files.
--- The resource group defaults to "General".
--- ```lua
--- local ok = exu.CloneMaterial("my_material", "my_material_copy")
--- if not ok then print("clone failed") end
--- ```
--- @param sourceMaterialName string
--- @param cloneMaterialName string
--- @param resourceGroup string? optional
--- @return boolean
function exu.CloneMaterial(sourceMaterialName, cloneMaterialName, resourceGroup) end

--- Sets the texture used by a material pass texture unit.
--- This is useful for swapping atlas-backed HUD materials like `HUDcombi` at runtime.
--- The technique index defaults to 0, the pass index defaults to 0, the texture unit index defaults to 0,
--- and the resource group defaults to "General".
--- ```lua
--- local ok = exu.SetMaterialTexture("my_material", "my_texture.dds")
--- if not ok then print("SetMaterialTexture failed") end
--- ```
--- @param materialName string
--- @param textureName string
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetMaterialTexture(materialName, textureName, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Sets the current UV scroll offset on a material pass texture unit.
--- The technique index defaults to 0, the pass index defaults to 0, the texture unit index defaults to 0,
--- and the resource group defaults to "General".
--- ```lua
--- local ok = exu.SetMaterialTextureScroll("my_material", 0.1, 0.0)
--- if not ok then print("SetMaterialTextureScroll failed") end
--- ```
--- @param materialName string
--- @param u number
--- @param v number
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetMaterialTextureScroll(materialName, u, v, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Sets the UV scale of a material pass texture unit (Ogre setTextureScale,
--- about the texture centre: 2 makes the texture look twice as big). Same
--- defaults as SetMaterialTextureScroll.
--- ```lua
--- local ok = exu.SetMaterialTextureScale("my_material", 2.0, 2.0)
--- if not ok then print("exu.SetMaterialTextureScale failed") end
--- ```
--- @param materialName string
--- @param uScale number Non-zero
--- @param vScale number Non-zero
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetMaterialTextureScale(materialName, uScale, vScale, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Points a material pass texture unit at the texture window [u0, u0 + du] x
--- [v0, v0 + dv] across the mesh's UV 0..1, like an overlay panel's
--- `uv_coords` (a negative extent mirrors that axis). Sets scale and scroll;
--- rotation is left as it is. techniqueIndex -1 applies it to every technique
--- (for example the DX11 and DX9 techniques at once). Returns the number of
--- texture units changed.
--- ```lua
--- local changed = exu.SetMaterialTextureWindow("my_material", 0.0, 0.0, 0.5, 0.5)
--- if changed == 0 then print("no texture unit matched") end
--- ```
--- @param materialName string
--- @param u0 number
--- @param v0 number
--- @param du number Non-zero
--- @param dv number Non-zero
--- @param techniqueIndex integer? optional, default 0; -1 = every technique
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return integer
function exu.SetMaterialTextureWindow(materialName, u0, v0, du, dv, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Sets the current UV rotation on a material pass texture unit in radians.
--- The technique index defaults to 0, the pass index defaults to 0, the texture unit index defaults to 0,
--- and the resource group defaults to "General".
--- ```lua
--- local ok = exu.SetMaterialTextureRotate("my_material", math.pi / 4)
--- if not ok then print("SetMaterialTextureRotate failed") end
--- ```
--- @param materialName string
--- @param radians number
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetMaterialTextureRotate(materialName, radians, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Sets scrolling UV animation speeds on a material pass texture unit.
--- The technique index defaults to 0, the pass index defaults to 0, the texture unit index defaults to 0,
--- and the resource group defaults to "General".
--- ```lua
--- local ok = exu.SetMaterialTextureScrollAnimation("my_material", 0.1, 0.0)
--- if not ok then print("exu.SetMaterialTextureScrollAnimation failed") end
--- ```
--- @param materialName string
--- @param uSpeed number
--- @param vSpeed number
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetMaterialTextureScrollAnimation(materialName, uSpeed, vSpeed, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Sets rotational UV animation speed on a material pass texture unit in radians per second.
--- The technique index defaults to 0, the pass index defaults to 0, the texture unit index defaults to 0,
--- and the resource group defaults to "General".
--- ```lua
--- local ok = exu.SetMaterialTextureRotateAnimation("my_material", 0.5)
--- if not ok then print("exu.SetMaterialTextureRotateAnimation failed") end
--- ```
--- @param materialName string
--- @param speed number
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param textureUnitIndex integer? optional
--- @param resourceGroup string? optional
--- @return boolean
function exu.SetMaterialTextureRotateAnimation(materialName, speed, techniqueIndex, passIndex, textureUnitIndex, resourceGroup) end

--- Resolves a terrain atlas material name from a TRN file.
--- When no argument is provided, EXU asks stock Lua for the current map's TRN and reads `[Atlases] MaterialName`.
--- ```lua
--- local material = exu.GetTerrainMaterialName()
--- if not material then return end
--- ```
--- @nodiscard
--- @param trnFilename string? optional
--- @return string | nil
function exu.GetTerrainMaterialName(trnFilename) end

--- Applies one or more terrain texture-unit swaps to the active terrain material.
--- This keeps HG2/TRN tile painting intact and only retargets the terrain material's live texture inputs.
--- Supported texture fields are `diffuse`, `detail`, `normal`, `specular`, and `emissive`.
--- EXU also accepts the `*Map` aliases shown in `TerrainTextureSet`.
--- If `material` is omitted, EXU resolves the current map's terrain material from the TRN.
--- ```lua
--- local success, materialName = exu.SetTerrainTextureSet({ diffuse = "my_diffuse.dds", detail = "my_detail.dds" })
--- if not success then print("terrain texture swap failed") end
--- ```
--- @param textureSet TerrainTextureSet
--- @return boolean success
--- @return string | nil materialName
function exu.SetTerrainTextureSet(textureSet) end

--- Gets the ambient, diffuse, specular, and emissive colors from a material pass.
--- The technique index defaults to 0, the pass index defaults to 0, and the resource group defaults to "General".
--- ```lua
--- local colors = exu.GetMaterialPassColors("my_material")
--- if not colors then return end
--- ```
--- @nodiscard
--- @param materialName string
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param resourceGroup string? optional
--- @return table | nil
function exu.GetMaterialPassColors(materialName, techniqueIndex, passIndex, resourceGroup) end

--- Sets one or more colors on a material pass.
--- Supported table keys are `ambient`, `diffuse`, `specular`, and `emissive`.
--- The technique index defaults to 0, the pass index defaults to 0, and the resource group defaults to "General".
--- When targeting every technique (`techniqueIndex == -1`), `includeGlowScheme`
--- optionally includes bloom-mask passes. It defaults to false so ordinary
--- material tints do not make the whole mesh bleed into the glow buffer.
--- ```lua
--- local ok = exu.SetMaterialPassColors("my_material", { diffuse = { r = 1, g = 0.5, b = 0.5, a = 1 } })
--- if not ok then print("SetMaterialPassColors failed") end
--- ```
--- @param materialName string
--- @param colors table
--- @param techniqueIndex integer? optional
--- @param passIndex integer? optional
--- @param resourceGroup string? optional
--- @param includeGlowScheme boolean? optional
--- @return boolean
function exu.SetMaterialPassColors(materialName, colors, techniqueIndex, passIndex, resourceGroup, includeGlowScheme) end

--- Returns whether the entity has a named animation state.
--- ```lua
--- local hasAnim = exu.HasEntityAnimation(GetPlayerHandle(), "my_animation")
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param animationName string
--- @return boolean
function exu.HasEntityAnimation(h, animationName) end

--- Returns the current values for a named entity animation state.
--- ```lua
--- local info = exu.GetEntityAnimationInfo(GetPlayerHandle(), "my_animation")
--- if not info then return end
--- ```
--- @nodiscard
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param animationName string
--- @return table | nil
function exu.GetEntityAnimationInfo(h, animationName) end

--- Enables or disables a named entity animation state.
--- ```lua
--- exu.SetEntityAnimationEnabled(GetPlayerHandle(), "my_animation", true)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param animationName string
--- @param enabled boolean
function exu.SetEntityAnimationEnabled(h, animationName, enabled) end

--- Sets whether a named entity animation loops.
--- ```lua
--- exu.SetEntityAnimationLoop(GetPlayerHandle(), "my_animation", true)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param animationName string
--- @param loop boolean
function exu.SetEntityAnimationLoop(h, animationName, loop) end

--- Sets the blend weight of a named entity animation.
--- ```lua
--- exu.SetEntityAnimationWeight(GetPlayerHandle(), "my_animation", 1.0)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param animationName string
--- @param weight number
function exu.SetEntityAnimationWeight(h, animationName, weight) end

--- Sets the current time position of a named entity animation.
--- ```lua
--- exu.SetEntityAnimationTime(GetPlayerHandle(), "my_animation", 0.0)
--- ```
--- @param h Handle|ExuAnimationTarget A handle (main entity) or exu.animation.Target/TargetCockpit(h)
--- @param animationName string
--- @param timePosition number
function exu.SetEntityAnimationTime(h, animationName, timePosition) end

--- Sets the diffuse color of the headlight of the given object (if it exists).
--- ```lua
--- exu.SetHeadlightDiffuse(GetPlayerHandle(), 1.0, 0.9, 0.7)
--- ```
--- @param h Handle
--- @param r number
--- @param g number
--- @param b number
function exu.SetHeadlightDiffuse(h, r, g, b) end

--- Sets the specular color of the headlight of the given object (if it exists).
--- ```lua
--- exu.SetHeadlightSpecular(GetPlayerHandle(), 1.0, 0.9, 0.7)
--- ```
--- @param h Handle
--- @param r number
--- @param g number
--- @param b number
function exu.SetHeadlightSpecular(h, r, g, b) end

--- Sets the range parameters of the headlight of the given object (if it exists).
--- From the Ogre documentation: "Sets the range of a spotlight, i.e. the angle of the inner and outer cones and the rate of falloff between them."
--- 1.0 falloff is linear, <0 is slower and >0 is faster. Angles measured in radians.
--- ```lua
--- exu.SetHeadlightRange(GetPlayerHandle(), exu.OGRE.HEADLIGHT_RANGE.INNER_ANGLE, exu.OGRE.HEADLIGHT_RANGE.OUTER_ANGLE, exu.OGRE.HEADLIGHT_RANGE.FALLOFF)
--- ```
--- @param h Handle
--- @param innerAngle number
--- @param outerAngle number
--- @param falloff number
function exu.SetHeadlightRange(h, innerAngle, outerAngle, falloff) end

--- Sets whether or not a headlight is visible or hidden.
--- ```lua
--- exu.SetHeadlightVisible(GetPlayerHandle(), true)
--- ```
--- @param h Handle
--- @param visible boolean
function exu.SetHeadlightVisible(h, visible) end

--- Gets the power scale of the object's Ogre light, if it has one.
--- ```lua
--- local lightPowerScale = exu.GetLightPowerScale(GetPlayerHandle())
--- if not lightPowerScale then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return number | nil
function exu.GetLightPowerScale(h) end

--- Sets the power scale of the object's Ogre light.
--- ```lua
--- exu.SetLightPowerScale(GetPlayerHandle(), 1.5)
--- ```
--- @param h Handle
--- @param powerScale number
function exu.SetLightPowerScale(h, powerScale) end

--- Gets the position of the object's Ogre light.
--- ```lua
--- local pos = exu.GetLightPosition(GetPlayerHandle())
--- if not pos then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return Vector | nil
function exu.GetLightPosition(h) end

--- Sets the position of the object's Ogre light.
--- ```lua
--- exu.SetLightPosition(GetPlayerHandle(), 0, 5, 0)
--- ```
--- @param h Handle
--- @param x number
--- @param y number
--- @param z number
function exu.SetLightPosition(h, x, y, z) end

--- Sets the position of the object's Ogre light.
--- ```lua
--- exu.SetLightPosition(GetPlayerHandle(), GetPosition(GetPlayerHandle()))
--- ```
--- @param h Handle
--- @param position Vector
function exu.SetLightPosition(h, position) end

--- Gets the direction of the object's Ogre light.
--- ```lua
--- local dir = exu.GetLightDirection(GetPlayerHandle())
--- if not dir then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return Vector | nil
function exu.GetLightDirection(h) end

--- Sets the direction of the object's Ogre light.
--- ```lua
--- exu.SetLightDirection(GetPlayerHandle(), 0, -1, 0)
--- ```
--- @param h Handle
--- @param x number
--- @param y number
--- @param z number
function exu.SetLightDirection(h, x, y, z) end

--- Sets the direction of the object's Ogre light.
--- ```lua
--- exu.SetLightDirection(GetPlayerHandle(), SetVector(0, -1, 0))
--- ```
--- @param h Handle
--- @param direction Vector
function exu.SetLightDirection(h, direction) end

--- Sets the attenuation parameters of the object's Ogre light.
--- ```lua
--- exu.SetLightAttenuation(GetPlayerHandle(), 100, 1.0, 0.0, 0.005)
--- ```
--- @param h Handle
--- @param range number
--- @param constant number
--- @param linear number
--- @param quadratic number
function exu.SetLightAttenuation(h, range, constant, linear, quadratic) end

--- Gets the mass of the given object (most ships default to 1750 KG afaik).
--- Returns nil when the handle no longer refers to a live object.
--- ```lua
--- local mass = exu.GetMass(GetPlayerHandle())
--- if not mass then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return number?
function exu.GetMass(h) end

--- Sets the mass of the given object. This affects collisions and knockback.
--- Does nothing when the handle no longer refers to a live object.
--- ```lua
--- exu.SetMass(GetPlayerHandle(), 5000)
--- ```
--- @param h Handle
--- @param mass number finite and greater than 0
function exu.SetMass(h, mass) end

--- Converts a handle into a GameObject* that can be used in patching and debugging.
--- Returns nil when the handle no longer refers to a live object (the unit died
--- or its slot was reused).
--- ```lua
--- local obj = exu.GetObj(GetPlayerHandle())
--- if not obj then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return GameObject*?
function exu.GetObj(h) end

--- Gets live construction-menu selection state for a construction rig.
--- Returns nil if the handle is not a live construction rig or the state could not be read.
--- `activeMode` is the raw stock mode value.
--- When `hasBuildSelection` is true, `selectedClass` is the raw GameObjectClass*
--- for the selected build item and `buildItemIndex` is the 1-based
--- `ProducerClass.buildItemN` slot currently selected in the mode list.
--- ```lua
--- local constructionRigSelectionInfo = exu.GetConstructionRigSelectionInfo(GetPlayerHandle())
--- if not constructionRigSelectionInfo then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return table | nil
function exu.GetConstructionRigSelectionInfo(h) end

--- Opts into a BZ2-style, renderer-only construction-rig placement hologram.
--- Follows the stock GetCommand placement matrix/status without issuing Build,
--- changing selection, consuming resources, spawning a GameObject or changing cameras.
--- `meshes` maps lowercase build ODF names (max 8 chars) to actual Ogre .mesh resources.
--- `materialPrefix` names six existing materials: GreenSolid, GreenWire, YellowSolid,
--- YellowWire, RedSolid, RedWire appended to the prefix. Solid/wire alternates every
--- 512 ms. First enable requires configuration; later calls can omit it.
--- A fresh native placement is required each frame; cancel/confirm/death hides it.
--- False hides immediately. Closing the mission restores the optional native hook,
--- removes the frame listener and destroys only the EXU-owned display entity/node.
--- Returns false on unsupported builds, hook conflicts, missing renderer exports
--- or incomplete configuration. Stock placement feedback stays active throughout.
--- ```lua
--- local available = exu.SetConstructionHologramEnabled(true, { abwpow = "abwpow.mesh" }, "my_holo")
--- if not available then print("hologram unavailable") end
--- ```
--- @param enabled boolean
--- @param meshes table<string,string>?
--- @param materialPrefix string?
--- @return boolean available
function exu.SetConstructionHologramEnabled(enabled, meshes, materialPrefix) end

--- Gets the live selected weapon mask from the object's carrier, filtered to mounted weapons.
--- This is different from stock `GetWeaponMask`, which reflects the object's stored weapon-mask field.
--- ```lua
--- local mask = exu.GetSelectedWeaponMask(GetPlayerHandle())
--- if not mask then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return integer | nil
function exu.GetSelectedWeaponMask(h) end

--- Gets low-level weapon-selection state for the object.
--- The returned table may include stored weapon-mask fields, carrier masks, and mode-list fields.
--- Use this when you need to distinguish configured mask state from live carrier selection.
--- ```lua
--- local weaponSelectionInfo = exu.GetWeaponSelectionInfo(GetPlayerHandle())
--- if not weaponSelectionInfo then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return table | nil
function exu.GetWeaponSelectionInfo(h) end

--- Gets the object's live AiProcess* pointer, if any.
--- ```lua
--- local aiProcess = exu.GetAiProcess(GetPlayerHandle())
--- if not aiProcess then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return AiProcess* | nil
function exu.GetAiProcess(h) end

--- Gets the RTTI class name of the object's live AI process, if available.
--- ```lua
--- local aiProcessTypeName = exu.GetAiProcessTypeName(GetPlayerHandle())
--- if not aiProcessTypeName then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return string | nil
function exu.GetAiProcessTypeName(h) end

--- Gets AI process debug info for the given object.
--- Fields may include `process`, `vtable`, `rawTypeName`, `typeName`, `hierarchy`, scanned child objects, and aligned memory field scans.
--- ```lua
--- local aiProcessInfo = exu.GetAiProcessInfo(GetPlayerHandle())
--- if not aiProcessInfo then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @param scanBytes? integer Defaults to 256. Max 512.
--- @return table | nil
function exu.GetAiProcessInfo(h) end

--- Gets a typed snapshot of the object's current AI process state when EXU recognizes the process layout.
--- Currently specializes scavenger process fields and still includes common RTTI metadata.
--- ```lua
--- local aiProcessState = exu.GetAiProcessState(GetPlayerHandle())
--- if not aiProcessState then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @return table | nil
function exu.GetAiProcessState(h) end

--- Gets the first likely task/attack child object discovered inside the AI process.
--- This is heuristic and intended for reverse-engineering and diagnostics.
--- The returned table may include `candidates`, `children`, and aligned `fields`.
--- ```lua
--- local aiTaskInfo = exu.GetAiTaskInfo(GetPlayerHandle())
--- if not aiTaskInfo then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @param scanBytes? integer Defaults to 256. Max 512.
--- @return table | nil
function exu.GetAiTaskInfo(h) end

--- Gets an aligned 32-bit field scan for the most likely AI task/attack object.
--- Each entry may include integer, float, pointer, RTTI, and handle interpretations.
--- ```lua
--- local aiTaskFieldScan = exu.GetAiTaskFieldScan(GetPlayerHandle())
--- if not aiTaskFieldScan then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @param scanBytes? integer Defaults to 256. Max 512.
--- @return table | nil
function exu.GetAiTaskFieldScan(h) end

--- Gets a typed snapshot of the primary live AI task for the object.
--- Fields include task type/RTTI, state values, target handles, steering factors,
--- pitch, goto force/direction, and EXU's current per-unit turbo flag.
--- Returns nil for units whose task is not a UnitTask, e.g. a recycler's RecycleTask
--- (use exu.GetAiRecycleTaskState for those).
--- ```lua
--- local aiTaskState = exu.GetAiTaskState(GetPlayerHandle())
--- if not aiTaskState then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @param scanBytes? integer Defaults to 256. Max 512.
--- @return table | nil
function exu.GetAiTaskState(h) end

--- Gets the object's AI process state and current task state by direct field reads
--- (no scanning). Returns nil when the object has no UnitProcess (dead handle, no AI
--- process, or a multiplayer replica, which carries a NetProcess instead).
--- Fields: `processClass` (RTTI class name), `state`, `nextState`; `stateName` only when
--- the process class is exactly UnitProcess (0-17: NO_STATE, START, DONE, WAIT, GOTO,
--- PATROL, STAGE, ATTACK, FOLLOW, DEFEND, REPAIR, RELOAD, RESCUE, RECYCLE, PACKUP,
--- SUBATTACK, USTATE1, USTATE2); other process classes have their own state enums, so
--- they get the raw int and no name. `taskClass` is the current task's RTTI class name
--- when there is one. `taskState`, `taskStateName`, `stuckState` and `planIndex` are
--- present only when the task is a UnitTask (not e.g. a RecycleTask). `taskState` is the
--- UnitTask enum: 0 NO_STATE, 1 START, 2 GOTO, 3 STUCK, 4 FOLLOW, 5 ATTACK, 6 WAIT,
--- 7 SLIDE, 8 STAND, 9 FLEE, 10 BLAST, 11 LEAD, 12 CATCH, 13 DONE (the stock debug text
--- labels a GOTO task "DONE"; this does not). `planIndex` is the 1-based current plan
--- point, absent when the task has no readable plan or the engine's index is outside
--- the plan. `stuckState` is the raw engine stuck-action value (no name is given).
--- @nodiscard
--- @param h Handle
--- @return { processClass: string, state: integer, stateName: string|nil, nextState: integer, taskClass: string|nil, taskState: integer|nil, taskStateName: string|nil, stuckState: integer|nil, planIndex: integer|nil }|nil
function exu.GetAiState(h) end

--- Gets the pathfinder route the unit's current UnitTask is following.
--- Returns nil when there is no readable plan: dead handle, no UnitProcess (including
--- multiplayer replicas), a task that is not a UnitTask, no plan, or a corrupt plan.
--- `points` are the whole plan in order as `{x, y, z}` (simulation coordinates; `y` is
--- the terrain height at x,z and is absent if the lookup failed), capped at 512;
--- `truncated` is true when the plan was longer, and `count` is the true point count.
--- `index` is the 1-based current plan point (the stock debug view draws from it
--- onward), absent if the engine's index is outside the plan or, when `truncated`,
--- beyond the 512 points returned. A 2-point plan can be
--- the pathfinder's straight-line fallback.
--- @nodiscard
--- @param h Handle
--- @return { points: { x: number, y: number|nil, z: number }[], index: integer|nil, count: integer, truncated: boolean }|nil
function exu.GetAiPath(h) end

--- Gets the terrain pathing-grid cell flags at a world position (native x/z, no flip).
--- Bits: 1 slope, 2 steep (both = cliff), 4 lava-ish, 8 building (bit 8 unverified).
--- Returns nil when there is no pathing grid (no map loaded) or the position is outside
--- the grid.
--- @nodiscard
--- @param x number
--- @param z number
--- @return integer|nil
function exu.GetPathCell(x, z) end

--- Gets the pathing region id at a world position. Regions are connected components
--- built once at map load; 0 means cliff/unconnected. They do NOT reflect buildings
--- placed afterwards. Returns nil when there is no pathing grid or the position is
--- outside it.
--- @nodiscard
--- @param x number
--- @param z number
--- @return integer|nil
function exu.GetPathRegion(x, z) end

--- True when both positions are in the same nonzero pathing region (ground-reachable
--- at map load; buildings are not considered). False when either is a cliff or outside
--- the grid. Returns nil when there is no pathing grid.
---
--- Example: gate an order on reachability. nil means "unknown" (no grid or
--- unsupported build), so only skip the order on an explicit false.
--- ```lua
--- local from, to = GetPosition(h), GetPosition(target)
--- if exu.IsPathReachable(from.x, from.z, to.x, to.z) == false then
---     print("unreachable by ground")
--- else
---     Goto(h, target)
--- end
--- ```
---
--- Example: same-region test with GetPathRegion (0 = cliff, nil = no data).
--- ```lua
--- local ra = exu.GetPathRegion(a.x, a.z)
--- local rb = exu.GetPathRegion(b.x, b.z)
--- local same = ra ~= nil and ra ~= 0 and ra == rb
--- ```
---
--- @nodiscard
--- @param x0 number
--- @param z0 number
--- @param x1 number
--- @param z1 number
--- @return boolean|nil
function exu.IsPathReachable(x0, z0, x1, z1) end

--- Runs the engine pathfinder from (x0, z0) to (x1, z1) and returns the route as
--- `{x, y, z}` points (`y` is terrain height and is absent if the lookup failed;
--- capped at 512 points) plus an `ok` boolean that is false when the engine returned
--- its bad/fallback path. `mode` 0-2 are ground classes (the stock editor uses 0),
--- 3 is flying (a straight 2-point path); `team` is 0-15. The search is synchronous and
--- costs a real pathfind: do not call it every frame for many units. Longer routes are
--- cut to their first 512 points without notice. Returns nil when there is no pathing
--- grid, or the engine returned no plan or an empty/unreadable one. The plan is freed
--- before returning, and the search resets its own settings on every call (a fault
--- inside the native search is caught, but can leak that plan).
---
--- Example: walk the route and mark it with stock BuildObject. Check for nil
--- (no grid, no plan, unsupported build) and for `ok == false` (fallback path).
--- ```lua
--- local from, to = GetPosition(h), GetPosition(target)
--- local points, ok = exu.FindPath(from.x, from.z, to.x, to.z, 0, GetTeamNum(h))
--- if points and ok then
---     for i, p in ipairs(points) do
---         BuildObject("my_marker", 0, SetVector(p.x, p.y or from.y, p.z)) -- your own marker ODF
---     end
--- else
---     print("no usable path")
--- end
--- ```
---
--- @nodiscard
--- @param x0 number
--- @param z0 number
--- @param x1 number
--- @param z1 number
--- @param mode integer|nil Default 0.
--- @param team integer|nil Default 1.
--- @return { x: number, y: number|nil, z: number }[]|nil points
--- @return boolean|nil ok
function exu.FindPath(x0, z0, x1, z1, mode, team) end

--- Gets the AI strategic-grid cell (80 m squares) a team's AI holds at a world position.
--- The grid only exists for AI-controlled teams. Returns nil when the team has no grid,
--- the position is outside it, or the cell is unused. `goal` is the engine's raw goal
--- type; `priority` is the cell priority.
--- @nodiscard
--- @param team integer 0-15
--- @param x number
--- @param z number
--- @return { goal: integer, priority: number, row: integer, col: integer, minX: number, minZ: number, maxX: number, maxZ: number }|nil
function exu.GetAiGridCell(team, x, z) end

--- Updates selected fields on the primary live UnitTask (any stock task except
--- RecycleTask) for the object. Supported fields: `braccel`, `strafe`, `steer`,
--- `omega`, `omegaScale`, `pitch`, `gotoForce`, `gotoDir`, and `turbo`.
--- Numbers and vector components must be finite and within +/-10000; an invalid
--- field raises an error before anything is written. Returns false, writing
--- nothing, when the handle is dead or a task field is given and the object has no UnitTask.
--- ```lua
--- local ok = exu.SetAiTaskState(GetHandle("tank1"), { strafe = 0.5, turbo = true })
--- ```
--- @param h Handle
--- @param state table
--- @return boolean
function exu.SetAiTaskState(h, state) end

--- True when this executable's optional vehicle control hooks qualify and the session is single player.
--- Does not install a hook or claim a vehicle. Currently supports ordinary HoverCraft with stock UnitProcess Execute.
--- ```lua
--- local available = exu.HasVehicleControl()
--- ```
--- @return boolean
function exu.HasVehicleControl() end

--- Claims an AI hovercraft for this mission. Starts neutral with a 0.5 simulation-second command lease.
--- Suppresses stock process/task decisions and firing while commands remain current.
--- Rejects the player, unsupported craft/processes, dead handles and network sessions.
--- Repeated claims of the same identity are harmless but do not renew the lease.
--- ```lua
--- local accepted, reason = exu.ClaimVehicleControl(GetHandle("tank1"))
--- if not accepted then print(reason) end
--- ```
--- @param h Handle
--- @return boolean accepted
--- @return string? reason
function exu.ClaimVehicleControl(h) end

--- @class ExuVehicleControls
--- @field steer? number Finite [-1,1], defaults to zero.
--- @field pitch? number Finite [-1,1], defaults to zero.
--- @field strafe? number Finite [-1,1], defaults to zero.
--- @field throttle? number Finite [-1,1], defaults to zero.
--- @field turbo? boolean Defaults to false.
--- @field jump? boolean Defaults to false.
--- @field fire? boolean Defaults to false.
--- @field special? boolean Defaults to false.
--- @field weaponMask? integer Five hardpoint bits [0,31]; omitted preserves selection.
--- @field timeout? number Simulation seconds [0.01,5], defaults to 0.5.

--- Replaces the claimed vehicle's held inputs; omitted axes/booleans reset to zero/false.
--- Fields: steer, pitch, strafe, throttle (finite [-1,1]); turbo, jump, fire, special (booleans);
--- weaponMask (optional integer [0,31], omitted preserves carrier selection); timeout (0.01..5 simulation seconds, default 0.5).
--- Unknown fields, wrong types and nonfinite values raise before anything changes.
--- Renew from Update; expiry clears held movement and returns the vehicle to stock AI.
--- Fire uses the current native muzzle aim; this API does not solve a new aimPoint or target lock.
--- ```lua
--- local accepted, reason = exu.SetVehicleControls(GetHandle("tank1"), { throttle = 1, steer = -0.5, timeout = 0.5 })
--- if not accepted then print(reason) end
--- ```
--- @param h Handle
--- @param controls ExuVehicleControls
--- @return boolean accepted
--- @return string? reason
function exu.SetVehicleControls(h, controls) end

--- Typed hovercraft telemetry: claimed, hookActive, processType, processState/processStateName,
--- pendingProcessState, savedProcessState, taskType, taskState/taskStateName/pendingTaskState (UnitTask only),
--- selectedWeaponMask, enabledWeaponMask, inputs. Active claims add requested, lastApplied, applications,
--- leaseRemaining, fire and special. State numbers for process and task use different enums.
--- ```lua
--- local state, reason = exu.GetVehicleControlState(GetHandle("tank1"))
--- if not state then print(reason) end
--- ```
--- @param h Handle
--- @return table? state
--- @return string? reason
function exu.GetVehicleControlState(h) end

--- Clears held movement and releases to stock AI on the next native update. Accepts destroyed handles.
--- Existing stock task/order is retained; issue a fresh stock order if resuming after a long scripted sequence.
--- ```lua
--- local released = exu.ReleaseVehicleControl(GetHandle("tank1"))
--- ```
--- @param h Handle
--- @return boolean
function exu.ReleaseVehicleControl(h) end

--- Clears all mission claims/held movement. Also done automatically on EXU mission teardown.
--- Reapply claims after loading a save; native claims are not serialized.
--- ```lua
--- local reset = exu.ResetVehicleControls()
--- ```
--- @return boolean
function exu.ResetVehicleControls() end

--- Gets a typed snapshot of the active recycle subtask when the object's AI task is a recycle task.
--- Useful for scavenger/recycler debugging and scripting around scrap collection flow.
--- ```lua
--- local aiRecycleTaskState = exu.GetAiRecycleTaskState(GetPlayerHandle())
--- if not aiRecycleTaskState then return end
--- ```
--- @nodiscard
--- @param h Handle
--- @param scanBytes? integer Defaults to 256. Max 512.
--- @return table | nil
function exu.GetAiRecycleTaskState(h) end

--- Gets the radar scan period for the given object (if it has a radar).
--- ```lua
--- local radarPeriod = exu.GetRadarPeriod(GetPlayerHandle())
--- ```
--- @nodiscard
--- @param h Handle
--- @return number
function exu.GetRadarPeriod(h) end

--- Sets the radar scan period for the given object.
--- ```lua
--- exu.SetRadarPeriod(GetPlayerHandle(), 2.0)
--- ```
--- @param h Handle
--- @param period number
function exu.SetRadarPeriod(h, period) end

--- Gets the radar range of the given object (if it has a radar).
--- ```lua
--- local radarRange = exu.GetRadarRange(GetPlayerHandle())
--- ```
--- @nodiscard
--- @param h Handle
--- @return number
function exu.GetRadarRange(h) end

--- Sets the radar range of the given object.
--- ```lua
--- exu.SetRadarRange(GetPlayerHandle(), 100)
--- ```
--- @param h Handle
--- @param range number
function exu.SetRadarRange(h, range) end

--- Gets the velocjam value for the given object (in m/s).
--- ```lua
--- local velocJam = exu.GetVelocJam(GetPlayerHandle())
--- ```
--- @nodiscard
--- @param h Handle
--- @return number
function exu.GetVelocJam(h) end

--- Sets the velocjam value (maximum speed in m/s) for the given object.
--- ```lua
--- exu.SetVelocJam(GetHandle("jammer1"), 10) -- maximum speed in m/s
--- ```
--- @param h Handle
--- @param maxSpeed number finite and non-negative
function exu.SetVelocJam(h, maxSpeed) end

--- Triggers the unit's selected weapons this tick, the same call the engine
--- makes when a pilot or the AI fires (Carrier::TriggerSelected). With a mask,
--- the weapon selection is first changed to those slots (bit i = slot i, 1..31;
--- slots without a weapon are dropped). Disabled slots (see
--- exu.SetWeaponEnabled) never fire.
---
--- The trigger is a one-tick latch the weapon consumes on its next update:
--- call this every frame for sustained fire. The mask changes the selection
--- persistently (Select/Deselect run, the HUD follows for the player), and the
--- unit's AI may reselect weapons on its own afterwards.
---
--- Multiplayer: weapon fire is simulated by the machine that owns the unit;
--- call this there. On other machines it only affects the local copy.
--- ```lua
--- exu.FireWeapons(h)      -- fire whatever is selected
--- exu.FireWeapons(h, 3)   -- select slots 0 and 1, then fire them
--- ```
--- @param h Handle
--- @param mask? integer slot bitmask 1..31
--- @return boolean|nil fired true if any weapon was triggered, false if nothing could fire (bad mask, nothing selected or enabled), nil for an invalid handle or a unit without weapons
function exu.FireWeapons(h, mask) end

--- Triggers the unit's special slot (Carrier::TriggerSpecial), if it has one
--- and it is enabled. Special items (RadarDamper, ImageRefract,
--- TerrainExpose, SpecialItem) toggle on trigger and ignore repeated triggers
--- while held, so one call switches the item; check exu.IsSpecialActive.
--- UNVERIFIED: the engine may ignore this toggle on the local player's own
--- vehicle (SpecialItem::Trigger is gated for the user object).
---
--- Multiplayer: call on the machine that owns the unit.
--- ```lua
--- exu.FireSpecialWeapon(h)
--- ```
--- @param h Handle
--- @return boolean|nil fired true if the special slot was triggered, nil for an invalid handle or a unit without weapons
function exu.FireSpecialWeapon(h) end

--- Releases the trigger on every mounted weapon (Carrier::UnTriggerAll, the
--- call the engine makes when a pilot leaves a craft). Cancels a fire latched
--- earlier in the same tick and switches special items off. It does not stop
--- the AI or the player pulling the trigger again on a later tick.
---
--- Multiplayer: call on the machine that owns the unit.
--- ```lua
--- exu.CeaseFire(h)
--- ```
--- @param h Handle
--- @return boolean|nil done true once the weapons were untriggered, nil for an invalid handle or a unit without weapons
function exu.CeaseFire(h) end

--- Gets whether the weapon in a slot is enabled (the carrier's enabled mask).
--- A disabled weapon never fires, whoever pulls the trigger.
--- ```lua
--- local on = exu.GetWeaponEnabled(GetPlayerHandle(), 0)
--- ```
--- @nodiscard
--- @param h Handle
--- @param slot integer weapon slot 0..4
--- @return boolean|nil enabled nil for an empty slot, an invalid slot, an invalid handle or a unit without weapons
function exu.GetWeaponEnabled(h, slot) end

--- Enables or disables the weapon in a slot. A disabled weapon never fires,
--- for the player or the AI. Refused (false) for an empty slot and for
--- producers (factory, armory, recycler, constructor) and turret tanks /
--- howitzers, whose enabled mask the engine rewrites every tick from their
--- deployed state. GiveWeapon re-enables the slot it fills.
---
--- Multiplayer: the mask is local to each machine; call on the machine that
--- owns the unit (and on every machine if the HUD should agree).
--- ```lua
--- exu.SetWeaponEnabled(h, 1, false) -- slot 1 stops firing
--- ```
--- @param h Handle
--- @param slot integer weapon slot 0..4
--- @param enabled boolean
--- @return boolean applied true if the mask was written
function exu.SetWeaponEnabled(h, slot, enabled) end

--- Gets whether the special item in a slot is switched on (RadarDamper,
--- ImageRefract, TerrainExpose or another SpecialItem). False for a weapon
--- that is not a special item.
--- ```lua
--- if exu.IsSpecialActive(h, 4) then print("cloaked") end
--- ```
--- @nodiscard
--- @param h Handle
--- @param slot integer weapon slot 0..4
--- @return boolean|nil active nil for an empty slot, an invalid slot, an invalid handle or a unit without weapons
function exu.IsSpecialActive(h, slot) end

--- Gets the hardpoint name of a weapon slot (the ODF weaponHardN, e.g. "GC1")
--- and the ODF of the weapon mounted there.
--- ```lua
--- local hardpoint, odf = exu.GetHardpoint(GetPlayerHandle(), 0)
--- ```
--- @nodiscard
--- @param h Handle
--- @param slot integer weapon slot 0..4
--- @return string|nil hardpoint nil when the slot has no hardpoint
--- @return string|nil odf nil when the slot has no weapon
function exu.GetHardpoint(h, slot) end

--- Graphics Options

--- Gets if the game is currently in fullscreen.
--- ```lua
--- local fullscreen = exu.GetFullscreen()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetFullscreen() end

--- Gets the actual resolution of the game, returns two values for X and Y pixel count.
--- ```lua
--- local width, height = exu.GetGameResolution()
--- ```
--- @nodiscard
--- @return integer, integer
function exu.GetGameResolution() end

--- Gets the current UI scaling factor (1, 2, 3, etc).
--- ```lua
--- local uiScaling = exu.GetUIScaling()
--- ```
--- @nodiscard
--- @return integer
function exu.GetUIScaling() end

--- IO
---
--- These functions handle input/output that the stock game doesn't provide.

--- Gets whether or not a key is held. Always false while the game window is not in the foreground.
--- A full list of keys can be found here.
--- ```lua
--- local wHeld = exu.GetGameKey("W")
--- ```
--- @nodiscard
--- @param key string
--- @return boolean
function exu.GetGameKey(key) end

--- Suppress selected native LOCAL player controls under a named mission owner.
--- Mask bits come from exu.CONTROL; combine distinct bits with addition or bit.bor.
--- A mask of 0 releases this owner. Owners combine with OR; releasing one cannot
--- unlock another. Limited to 16 owners with names of 1..63 bytes without NUL.
--- In a network session it affects only the local player on each peer that calls
--- it (untested in live multiplayer so far). Returns false for an unsupported/unqualified runtime or
--- exhausted owner capacity. Invalid owner/mask arguments raise a Lua error.
--- Opt-in native filtering starts on the next UserProcess simulation, follows
--- pilot/craft changes, preserves UI keys, and clears at Lua-state teardown.
--- This filters user input, not scripted FireAt/EjectPilot or physical momentum.
--- ```lua
--- local accepted = exu.SetPlayerControlMask("mymod", exu.CONTROL.FIRE + exu.CONTROL.JUMP)
--- if not accepted then print("control mask unavailable") end
--- ```
---@param owner string
---@param mask integer
---@return boolean accepted
function exu.SetPlayerControlMask(owner, mask) end

--- Idempotently release only the named owner's control mask.
--- ```lua
--- local cleared = exu.ClearPlayerControlMask("mymod")
--- ```
---@param owner string
---@return boolean
function exu.ClearPlayerControlMask(owner) end

--- Inspect support and actual simulation applications separately from requests.
--- Fields: available, hookActive, requestedMask, lastAppliedMask, ownerCount,
--- applications, injecting, injections, injectedFires, injectRemaining, reason.
--- lastAppliedMask
--- records the last successful application, not proof that the current frame's
--- UI/focus permits gameplay.
--- ```lua
--- local playerControlState = exu.GetPlayerControlState()
--- ```
---@return table
function exu.GetPlayerControlState() end

---@class PlayerInputs
---@field steer? number  -1..1
---@field pitch? number  -1..1
---@field strafe? number -1..1
---@field throttle? number -1..1
---@field turbo? boolean
---@field jump? boolean
---@field fire? boolean  selected weapon; hovercraft and subclasses (e.g. wingman)
---@field timeout? number seconds 0.01..5, default 0.5

--- Drive the LOCAL player's current vehicle as if by input, for a short lease.
--- Renew it every Update (or within timeout); omitted fields are neutral. The
--- lease overrides player input and any control mask, and its end zeroes the
--- driven controls once. In a network session it drives only the local player
--- on each peer that calls it (untested in multiplayer). Returns false plus a reason
--- (unsupported-build, qualification-failed,
--- fire-qualification-failed, clock-unavailable, invalid-command). Bad fields
--- raise a Lua error. Verified on a wingman-class tank; pilot-on-foot is untested.
--- ```lua
--- local accepted, reason = exu.SetPlayerInputs({ throttle = 1, steer = 0.25, timeout = 0.5 })
--- if not accepted then print(reason) end
--- ```
---@param inputs PlayerInputs
---@return boolean accepted
---@return string? reason
function exu.SetPlayerInputs(inputs) end

--- End the scripted input lease on the next simulation update. Idempotent.
--- ```lua
--- local cleared = exu.ClearPlayerInputs()
--- ```
---@return boolean
function exu.ClearPlayerInputs() end

--- Native player input suppression flags. Zero permits all controls.
exu.CONTROL = {
    THROTTLE=1, STRAFE=2, LOOK=4, FIRE=8, JUMP=16, DEPLOY=32,
    EJECT=64, ABANDON=128, TURBO=256, WEAPONS=512, NAV=1024,
    JETPACK=2048, MOVEMENT=2323, ALL=4095,
}

--- Returns whether any stock game or shell UI is currently covering gameplay.
--- This includes the Escape/pause hierarchy, save/load/options pages, and
--- mission-success or mission-failure screens. Use it to suppress custom HUD.
--- ```lua
--- local isGameUiOpen = exu.IsGameUiOpen()
--- ```
--- @nodiscard
--- @return boolean
function exu.IsGameUiOpen() end

--- Returns whether the in-game pause or Escape menu is currently open.
--- This is intended for suppressing custom HUD/UI during paused gameplay.
--- ```lua
--- local isPauseMenuOpen = exu.IsPauseMenuOpen()
--- ```
--- @nodiscard
--- @return boolean
function exu.IsPauseMenuOpen() end

--- Returns a debug snapshot of the native game-side pause/UI state probe.
--- Useful for reverse-engineering stock UI ownership without relying on Ogre or Lua timing.
--- Fields include:
--- `ok`, `gameUiOpen`, `pauseOpen`, `singleplayerPauseOpen`, `multiplayerPauseOpen`,
--- `cursorVisible`, `currentScreenMatchesPauseRoot`, `singleplayerPauseRoot`,
--- `multiplayerPauseRoot`, `uiCurrentScreen`, `escapeUiWrapperActive`, `uiWrapperActive`,
--- `uiCurrentScreenType`, `uiCurrentScreenTypeName`, `multiplayerPauseFlag`.
--- ```lua
--- local pauseMenuDebugState = exu.GetPauseMenuDebugState()
--- ```
--- @nodiscard
--- @return table
function exu.GetPauseMenuDebugState() end

--- Returns whether a stock in-mission text editor has keyboard focus.
--- Includes multiplayer chat and the ally/unally team-number prompt.
--- Combine with IsGameUiOpen() when suppressing mission key commands.
--- Returns false if the native probe is unavailable; use the debug state's `ok` to detect that case.
--- ```lua
--- local isTextEntryActive = exu.IsTextEntryActive()
--- ```
--- @nodiscard
--- @return boolean
function exu.IsTextEntryActive() end

--- Returns whether the multiplayer ally or unally team-number prompt has keyboard focus.
--- Returns false if the native probe is unavailable.
--- ```lua
--- local isAllyPromptOpen = exu.IsAllyPromptOpen()
--- ```
--- @nodiscard
--- @return boolean
function exu.IsAllyPromptOpen() end

--- Controls whether the Enter key opens multiplayer chat (stock binding CHAT_SENDTOALL).
--- `false` clears Enter's entry in the parsed gamekey.map table, but only if it is still the stock chat action;
--- `true` restores it, but only if Enter is currently unbound. If the player rebound Enter to another action it is left alone.
--- Backtick and Ctrl+C still open chat. The engine re-parses gamekey.map when the ESC menu closes and re-binds Enter,
--- so call this every frame while Enter must stay free.
--- Returns true when Enter's entry matches the request afterwards; false if the build is unsupported or Enter holds another action.
--- ```lua
--- local applied = exu.SetChatOnEnter(true)
--- ```
--- @param enabled boolean
--- @return boolean
function exu.SetChatOnEnter(enabled) end

--- Returns a snapshot of the stock in-mission text editor focus state.
--- Fields: `ok`, `textEntryActive`, `chatOpen`, `allyPromptOpen`, `focusedNode`,
--- `focusedEntry`, `chatNode`, `allyNode`, `entryFlags`. Pointers are diagnostic integers.
--- An unavailable build or failed/corrupt native read returns `ok=false` with all other fields false/zero.
--- Shell/lobby cUI_TextEntry controls are covered separately by IsGameUiOpen().
--- ```lua
--- local textEntryDebugState = exu.GetTextEntryDebugState()
--- ```
--- @nodiscard
--- @return table
function exu.GetTextEntryDebugState() end

--- Multiplayer
---
--- These functions work exclusively in multiplayer to help with various things.

--- Functions the same as BuildObject except the resulting object is always asynchronous regardless of hosting status.
--- ```lua
--- local h = exu.BuildAsyncObject("avtank", 1, GetPosition(GetPlayerHandle())) -- same arguments as BuildObject
--- ```
--- @return Handle
function exu.BuildAsyncObject(...) end

--- Functions the same as BuildObject except the resulting object is always synchronized regardless of hosting status.
--- ```lua
--- local h = exu.BuildSyncObject("avtank", 1, GetPosition(GetPlayerHandle())) -- same arguments as BuildObject
--- ```
--- @return Handle
function exu.BuildSyncObject(...) end

--- Returns the custom kill message set for the given team if it exists
--- ```lua
--- local message = exu.GetCustomKillMessage(1)
--- if not message then return end
--- ```
--- @nodiscard
--- @param team integer
--- @return string | nil
function exu.GetCustomKillMessage(team) end

--- Sets a custom kill message for the given team.
--- Can be used to rename generic AI teams (Team 5 etc.)
--- [Custom name] killed by [Custom name 2]
--- ```lua
--- exu.SetCustomKillMessage(1, "was scrapped") -- must be under 32 characters
--- ```
--- @param team integer
--- @param message string MUST BE <32 CHARACTERS
function exu.SetCustomKillMessage(team, message) end

--- Gets the local player's life count.
--- ```lua
--- local lives = exu.GetLives()
--- ```
--- @nodiscard
--- @return number
function exu.GetLives() end

--- Sets the local player's life count. This must be called during or after Start(), otherwise lives are not initialized in the map yet.
--- ```lua
--- exu.SetLives(3)
--- ```
--- @param lives number
function exu.SetLives(lives) end

--- Gets the net ID of the local player (the parameter that would appear in CreatePlayer()).
--- ```lua
--- local myNetID = exu.GetMyNetID()
--- ```
--- @nodiscard
--- @return number
function exu.GetMyNetID() end

--- Whether stock multiplayer death/ejection/destruction HUD notices are enabled.
--- Does not affect scoring, human chat or mission DisplayMessage calls.
--- ```lua
--- local showKillMessages = exu.GetShowKillMessages()
--- ```
--- @return boolean
function exu.GetShowKillMessages() end

--- Mission-scoped local presentation switch, reset when the Lua state closes.
--- Returns false if the released executable's formatter cannot be qualified.
--- ```lua
--- local applied = exu.SetShowKillMessages(false)
--- ```
--- @param state boolean
--- @return boolean applied
function exu.SetShowKillMessages(state) end

--- Read-only snapshot of the native scoreboard data for a network player id.
--- Fields: id, team, name, deaths, kills, lives. Missing score slots use the
--- native scoreboard default of zero.
--- Remote players also have pingMs (native average, clamped 9999) and lossPercent
--- (stock ten-packet history indicator). Local ping/loss are absent, not fake zero.
--- Remote pingSamples (0-10) is the native measurement-history count; zero means
--- no measurements yet, so consumers should not present default zeros as a healthy link.
--- Returns nil outside a network mission, for absent players or unsupported sites.
--- Does not show/refresh the scoreboard or retain native pointers. Query on the
--- game thread, at a modest UI refresh rate (e.g. once per second).
--- ```lua
--- local stats = exu.GetPlayerNetworkStats(exu.GetMyNetID())
--- if not stats then return end
--- ```
--- @param id integer Net ID used by CreatePlayer/Receive, not the team number
--- @return table|nil
function exu.GetPlayerNetworkStats(id) end

--- Local-client BZR UDP observations from optional OpenShim telemetry.
--- Fields: txBytes/rxBytes, txPackets/rxPackets (since observed lobby reset),
--- txBytesPerSecond/rxBytesPerSecond, txPacketsPerSecond/rxPacketsPerSecond,
--- windowMs (2000), sampledAtMs, route (relay/direct/mixed/unknown/idle),
--- routeAgeMs/receiveAgeMs when observed, coverageLimited.
--- Tx counts Winsock-accepted submissions, including pending sends; Rx counts
--- completed observed receives. Payload bytes include BZR headers, not IP/UDP
--- overhead. Totals aggregate this client's peers; no per-player attribution.
--- Passive route evidence expires from the rate window; it is not a route setting.
--- Returns nil outside network play or when the optional OpenShim API is absent.
--- ```lua
--- local networkTrafficStats = exu.GetNetworkTrafficStats()
--- if not networkTrafficStats then return end
--- ```
--- @return table|nil
function exu.GetNetworkTrafficStats() end

--- Gets if the multiplayer scoreboard is visible.
--- ```lua
--- local showScoreboard = exu.GetShowScoreboard()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetShowScoreboard() end

--- Sets the state of the multiplayer scoreboard. (Default toggle key M).
--- ```lua
--- exu.SetShowScoreboard(true)
--- ```
--- @param state boolean
function exu.SetShowScoreboard(state) end

--- Prevents the starting recycler from spawning in strategy and MPI missions. CALL LOOSE IN THE SCRIPT PRE-START!
--- ```lua
--- exu.DisableStartingRecycler()
--- ```
function exu.DisableStartingRecycler() end

--- Network games only. Takes over a remote AI object on this machine and gives
--- it a working AI here, the way the engine does when a pilot boards an empty
--- craft: claims ownership with SetLocal (the previous owner stops running its
--- AI) and attaches a fresh AI process from the object's ODF. Pass `team` to
--- move it to that team first; its AI class depends on the team. Call it on one
--- machine only, never on the player's own craft. Pending orders do not carry
--- over; give new ones afterwards.
--- ```lua
--- local ok, reason = exu.AdoptObject(GetHandle("tank1"), 1)
--- if not ok then print(reason) end
--- ```
--- @param h handle
--- @param team? integer 0-15
--- @return boolean ok
--- @return string? reason when not ok
function exu.AdoptObject(h, team) end

--- Gets the object's network id: the id the engine uses to name this object in
--- network packets. A local object gets `(owner's net id * 65536) + counter`
--- when it is networked, and remote copies on other machines carry the same
--- id, so the number names one object on every machine and can be sent with
--- Send() and resolved with exu.GetHandleFromNetworkID. Returns nil for an
--- invalid handle and for objects that are not networked (single player, or
--- map objects nobody has taken ownership of, which keep id 0).
--- ```lua
--- local id = exu.GetNetworkID(GetHandle("tank1"))
--- if id then Send(0, "T", id) end
--- ```
--- @nodiscard
--- @param h handle
--- @return integer? id
function exu.GetNetworkID(h) end

--- Finds the object with a network id from exu.GetNetworkID, on this machine:
--- the local object with that id, else the local copy of a remote one. Returns
--- nil when no object here has that id (not networked yet, already dead, not
--- yet created here, or single player).
--- ```lua
--- local h = exu.GetHandleFromNetworkID(id)
--- if h then SetTeamNum(h, 2) end
--- ```
--- @nodiscard
--- @param id integer
--- @return handle?
function exu.GetHandleFromNetworkID(id) end

--- Gets the network id of the player whose machine owns (simulates) this
--- object. Compare it with exu.GetMyNetID() or the `id` fields of
--- exu.AllPlayers(). Ownership moves when a player boards a craft or a script
--- calls SetLocal / exu.AdoptObject. Returns nil for an invalid handle and for
--- objects nobody owns on the network (all objects in single player).
--- ```lua
--- if exu.GetOwnerID(h) == exu.GetMyNetID() then print("mine") end
--- ```
--- @nodiscard
--- @param h handle
--- @return integer? ownerId
function exu.GetOwnerID(h) end

--- Lists the players in the network session as this machine sees them, one
--- table each: `id` (network id, as used by Send, exu.GetMyNetID and
--- exu.GetOwnerID), `team` (0-15; missing if the engine holds no valid team
--- yet), `name`, and `isLocal`. There is no handle: GetPlayerHandle(team) does
--- not resolve remote players in BZR, so look up the craft yourself. Returns an
--- empty table outside a network game.
--- ```lua
--- for _, p in ipairs(exu.AllPlayers()) do print(p.id, p.team, p.name, p.isLocal) end
--- ```
--- @nodiscard
--- @return { id: integer, team: integer?, name: string, isLocal: boolean }[]
function exu.AllPlayers() end

--- Returns true in a deathmatch-class network game (the engine's MultDM mission),
--- false in strategy games and single player. The engine sets
--- it when a stock DM mission starts and clears it when a stock ST mission
--- starts; other mission types keep the last value (not verified for Lua MP
--- missions).
--- ```lua
--- if exu.IsDeathmatch() then print("DM") end
--- ```
--- @nodiscard
--- @return boolean
function exu.IsDeathmatch() end

--- Gets the session's launch settings, the same values the engine logs at
--- "Launching Network Game": `timeLimit` (minutes, 0 = none), `killLimit`
--- (0 = none), `lives` (lives each player starts with; the live counter is
--- exu.GetLives), and booleans `sniper`, `barracks`, `commSat`, `splinter`
--- and `syncJoin`. Returns nil outside a network game. Read-only.
--- ```lua
--- local o = exu.GetNetGameOptions()
--- if o then print(o.timeLimit, o.killLimit, o.lives) end
--- ```
--- @nodiscard
--- @return { timeLimit: integer, killLimit: integer, lives: integer, sniper: boolean, barracks: boolean, commSat: boolean, splinter: boolean, syncJoin: boolean }?
function exu.GetNetGameOptions() end

--- Gets this machine's network start location: where the stock DM and ST
--- missions build and respawn the local player. Returns nil when there is no
--- network session (single player).
--- ```lua
--- local pos = exu.GetStartLocation()
--- ```
--- @nodiscard
--- @return vector?
function exu.GetStartLocation() end

--- Sets this machine's network start location, used by the stock DM respawn
--- (when no weighted spawn point applies) and the ST respawn. Local only: each
--- machine keeps its own, so call it on the machine whose player should move.
--- When the mission ends, EXU puts back the value the engine had before
--- the first call.
--- Returns false when there is no network session or a coordinate is not finite.
--- ```lua
--- exu.SetStartLocation(GetPosition("spawn_red"))
--- ```
--- @param pos vector|number x, or call as (x, y, z)
--- @param y? number
--- @param z? number
--- @return boolean ok
function exu.SetStartLocation(pos, y, z) end

--- Sets the OpenShim target-reticle popup mode.
--- 1 = Default, 3 = Explicit Only
--- 2 remains reserved for the experimental neutral-only path and downgrades to Default in normal builds.
--- ```lua
--- local applied = exu.SetTargetReticlePopupMode(2) -- mode 1-3
--- ```
--- @param mode integer
--- @return boolean
function exu.SetTargetReticlePopupMode(mode) end

--- Callback. Define this function in your mission script; EXU calls it after any
--- object's target changes (the engine's GameObject::SetTarget), the Redux
--- counterpart of BZCC's SetPostTargetChangedCallback. It fires only when the
--- stored target handle actually changed, never for a call that sets the same
--- target again, and the return value is ignored.
---
--- It runs synchronously inside the engine, in the middle of AI and order
--- processing, so keep it short. A target change made from inside the handler
--- (for example SetTarget on any object) does not call it again. Errors are
--- reported like other Lua errors and do not reach the engine.
---
--- Multiplayer remarks: runs on each client, for the objects whose target that
--- client's simulation sets. Expect remote objects to be missing or to differ;
--- do not treat it as a synchronized event.
---
--- objectHandle: the object whose target changed.
--- oldTargetHandle: its previous target, nil when it had none (engine handle 0).
--- newTargetHandle: its new target, nil when the target was cleared (engine handle 0).
---
--- Do not remove or destroy the objects passed in (RemoveObject, Damage to
--- death) from inside the handler: the engine keeps using them right after
--- the call returns. Defer that work to Update.
---
--- Declared as a field rather than a function stub because EXU does not export
--- it; the mission assigns it:
---     exu.TargetChanged = function(objectHandle, oldTargetHandle, newTargetHandle) ... end
--- @type (fun(objectHandle: Handle, oldTargetHandle: Handle?, newTargetHandle: Handle?))?
exu.TargetChanged = nil

--- Veto callback. Define it in your mission script; EXU calls it when a pilot
--- (the player or an AI pilot) touches an empty vehicle and every native
--- get-in rule has already passed, before anything is applied. The Redux
--- counterpart of BZCC's SetPreGetInCallback, without BZCC's world argument.
---
--- Return false to block the entry: the pilot bounces off as if the craft were
--- occupied. Only an explicit boolean false blocks; nil, any other value, no
--- handler, or a Lua error lets the engine continue natively.
---
--- Native vetoes (the engine's own checks, such as an occupied or unsuitable
--- craft) happen before this call, so it never sees attempts the engine already refused. It
--- runs synchronously inside the engine's collision code: keep it short. An
--- entry attempt triggered from inside the handler is allowed without calling
--- it again.
---
--- Do not remove or destroy the objects passed in (RemoveObject, Damage to
--- death) from inside the handler: the engine keeps using them right after
--- the call returns. Defer that work to Update.
---
--- Multiplayer remarks: runs only on the machine that owns the pilot (the
--- engine skips remote pilots before this point), so a veto never desyncs.
---
--- pilotHandle: the pilot trying to get in.
--- vehicleHandle: the vehicle it touched.
---
--- Declared as a field because EXU does not export it; the mission assigns it:
---     exu.PreGetIn = function(pilotHandle, vehicleHandle) return false end
--- @type (fun(pilotHandle: Handle, vehicleHandle: Handle): boolean?)?
exu.PreGetIn = nil

--- Veto callback. Define it in your mission script; EXU calls it when a vehicle
--- (player or AI) touches a powerup and every native pickup rule has already
--- passed, before the powerup is collected. The Redux counterpart of BZCC's
--- SetPrePickupPowerupCallback, without BZCC's world argument.
---
--- Return false to block the pickup; the powerup stays where it is. Only an
--- explicit boolean false blocks; nil, any other value, no handler, or a Lua
--- error lets the engine continue natively.
---
--- Native vetoes (the engine's own checks, such as the vehicle class or a
--- remote vehicle) happen before this call. It runs synchronously inside the engine's
--- collision code, and the engine may ask again on later contacts. A pickup
--- triggered from inside the handler is allowed without calling it again.
---
--- Do not remove or destroy the objects passed in (RemoveObject, Damage to
--- death) from inside the handler: the engine keeps using them right after
--- the call returns. Defer that work to Update.
---
--- Multiplayer remarks: runs only on the machine that owns the vehicle (the
--- engine skips remote vehicles before this point), so a veto never desyncs.
---
--- vehicleHandle: the vehicle touching the powerup.
--- powerupHandle: the powerup.
---
--- Declared as a field because EXU does not export it; the mission assigns it:
---     exu.PrePickupPowerup = function(vehicleHandle, powerupHandle) return false end
--- @type (fun(vehicleHandle: Handle, powerupHandle: Handle): boolean?)?
exu.PrePickupPowerup = nil

--- Ordnance
---
--- These functions modify the behavior of ordnance.

--- Builds an ordnance of the given odf (omit the .odf extension like with BuildObject) at the given transform matrix.
--- You must also provide an owner handle since the game appears to require this.
--- The owner of an ordnance will not be hit by it if it is created inside its hitbox, otherwise it can be hit.
--- Returns a handle to the ordnance object that can be used in exu.GetOrdnanceAttribute().
--- Ordnances are short lived. exu.GetOrdnanceAttribute returns nil once the object is gone,
--- but a freed round's memory can be reused by a newer round, so do not hold on to the value
--- past its lifespan: check that current time minus its init time is below the ODF lifespan,
--- or use the exu.BulletHit callback. The odf name is matched case-insensitively, without
--- the .odf extension, against the ordnance classes loaded so far.
---
--- Multiplayer remarks: You should not Send() another player a local ordnance handle,
--- they are likely client side and I haven't rigorously tested it in MP.
--- ```lua
--- local ord = exu.BuildOrdnance("gcannon", GetTransform(GetPlayerHandle()), GetPlayerHandle())
--- ```
--- @param odf string
--- @param transform Matrix
--- @param owner Handle
--- @return Ordnance*
function exu.BuildOrdnance(odf, transform, owner) end

--- Queries an ordnance handle for certain values. See the valid attribute codes in the ORDNANCE enum.
--- Returns nil when the value is no longer a live ordnance object.
--- ```lua
--- local velocity = exu.GetOrdnanceAttribute(ord, exu.ORDNANCE.VELOCITY)
--- if not velocity then return end -- the ordnance is gone
--- ```
--- @nodiscard
--- @param ordnanceHandle Ordnance*
--- @param attribute OrdnanceAttributes
--- @return any
function exu.GetOrdnanceAttribute(ordnanceHandle, attribute) end

--- Gets the global ballistic coefficient (how much mortars drop, default 4.9f).
--- ```lua
--- local coeffBallistic = exu.GetCoeffBallistic()
--- ```
--- @nodiscard
--- @return number
function exu.GetCoeffBallistic() end

--- Sets the global ballistic coefficient.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call,
--- or mortar trajectories differ between peers. EXU restores the pre-mission value when the mission's
--- Lua state closes. Raises an error for a non-finite value.
--- ```lua
--- local applied = exu.SetCoeffBallistic(4.9)
--- ```
--- @param coeff number
--- @return boolean applied false on an unqualified game build
function exu.SetCoeffBallistic(coeff) end

--- OS
---
--- These functions can get various information from the operating system (windows only).

--- High-resolution monotonic clock for profiling Lua (the BZR sandbox has no `os.clock`).
--- Seconds since the first clock read in this game process, from QueryPerformanceCounter.
--- Wall time, not mission time: it keeps running while paused and across mission loads,
--- and is local to this machine, so never use it for simulation or multiplayer-synced logic.
--- ```lua
--- local seconds = exu.GetClock()
--- ```
--- @nodiscard
--- @return number seconds
function exu.GetClock() end

--- Same clock as `exu.GetClock`, in milliseconds.
--- ```lua
--- local milliseconds = exu.GetClockMs()
--- ```
--- @nodiscard
--- @return number milliseconds
function exu.GetClockMs() end

--- Gets the screen resolution for the local user.
--- First value in the multi-return is the width in pixels, second is the height.
--- ```lua
--- local width, height = exu.GetScreenResolution()
--- ```
--- @nodiscard
--- @return number, number
function exu.GetScreenResolution() end

--- Displays an application modal windows message box with the given message, useful for debugging.
--- ```lua
--- exu.MessageBox("Hello")
--- ```
--- @param message string
function exu.MessageBox(message) end

--- Triggers the game's native save serializer.
--- Pass either a save slot number (1-10, mapped to `Save\\game<slot>.sav`) or a save path string.
--- A path string must name a file inside the game's `Save` directory. Relative paths are taken from the
--- game directory, so `"Save\\auto.sav"` works; an absolute path under `<game>\\Save` also works.
--- Any other path (outside `Save`, `..` above it, network or device paths) returns false and a reason.
--- The save is binary when the game was started with `-binarysave`, as the game's own saves are.
--- The optional `saveType` argument defaults to 0 and maps to the native second parameter.
--- You may also pass a description override as the second argument, or as the third argument after `saveType`.
--- Description overrides only rewrite the `saveGameDesc` field in text saves after the native save succeeds;
--- they are skipped for binary saves.
--- This should be called during active gameplay, not from shell menus or loading screens.
--- ```lua
--- local success, pathOrError = exu.SaveGame(1)
--- if not success then print(pathOrError) end
--- ```
--- @param slotOrPath integer | string
--- @param saveType integer | string?
--- @param description string?
--- @return boolean success
--- @return string pathOrError
function exu.SaveGame(slotOrPath, saveType, description) end

--- Patches
--- 
--- These functions control various patches to modify game behavior.

--- Wrapper for AddScrap that does not trigger the exu.AddScrap callback.
--- ```lua
--- local newScrap = exu.AddScrapSilent(1, 50)
--- ```
--- @param team any
--- @param amount any
--- @return number newScrapCount
function exu.AddScrapSilent(team, amount) end

--- Gets the configured engine flame color override for a team.
--- This currently stores EXU-side configuration for the native engine flame patch path.
--- ```lua
--- local color = exu.GetTeamEngineFlameColor(1)
--- ```
--- @nodiscard
--- @param team integer
--- @return '"default"' | '"blue"' | '"red"' | '"green"'
function exu.GetTeamEngineFlameColor(team) end

--- Sets the engine flame color override for a team.
--- Valid colors are "default", "blue", "red", and "green".
--- This currently stores EXU-side configuration for the native engine flame patch path.
--- ```lua
--- exu.SetTeamEngineFlameColor(1, "blue")
--- ```
--- @param team integer
--- @param color '"default"' | '"blue"' | '"red"' | '"green"'
function exu.SetTeamEngineFlameColor(team, color) end

--- Clears the engine flame color override for a team.
--- Equivalent to setting the color to "default".
--- ```lua
--- exu.ClearTeamEngineFlameColor(1)
--- ```
--- @param team integer
function exu.ClearTeamEngineFlameColor(team) end

--- Gets whether the red full-screen flash plays when the local player's craft or pilot takes damage (default true).
--- Reports what the engine will actually do: false only while the suppression patch is installed.
--- ```lua
--- local enabled = exu.GetDamageFlashEnabled()
--- ```
--- @nodiscard
--- @return boolean enabled
function exu.GetDamageFlashEnabled() end

--- Enables or disables the red full-screen flash when the local player's craft or pilot takes damage.
--- Only the stock damage flash is affected; mission ColorFade() calls and other flashes still play.
--- Mission scoped: resets to enabled when the Lua state closes, so set it again after loading a save.
--- ```lua
--- exu.SetDamageFlashEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetDamageFlashEnabled(enabled) end

--- Gets whether the orange full-screen flash plays while the local player's craft overheats (default true).
--- ```lua
--- local enabled = exu.GetHeatFlashEnabled()
--- ```
--- @nodiscard
--- @return boolean enabled
function exu.GetHeatFlashEnabled() end

--- Enables or disables the orange full-screen flash while the local player's craft overheats.
--- Mission scoped: resets to enabled when the Lua state closes, so set it again after loading a save.
--- ```lua
--- exu.SetHeatFlashEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetHeatFlashEnabled(enabled) end

--- @alias StockStatusHudPart "hull" | "ammo" | "weapons" | "scrap" | "pilots"

--- Gets whether a part of the stock player status display is drawn (default true).
--- Reports what the engine will actually do: false only while every draw of the part is suppressed.
--- ```lua
--- local visible = exu.GetStockStatusHudVisible("hull")
--- ```
--- @nodiscard
--- @param part StockStatusHudPart
--- @return boolean visible
function exu.GetStockStatusHudVisible(part) end

--- Shows or hides a part of the stock player status display (bottom-right hull/ammo bars and weapon list):
--- `"hull"` is the hull label and bar; `"ammo"` the ammo label, bar, shots-remaining count and ammo-cost marker;
--- `"weapons"` the weapon rows (plates, hardpoint icons and names).
--- `"scrap"` is the stock scrap sprite, title and current/capacity text; the pilot readout continues normally.
--- `"pilots"` is the stock pilot sprite, title and current/capacity text; scrap continues normally.
--- Pilot suppression skips its checked complete draw block and needs no OpenShim.
--- Only drawing is suppressed; low-hull and out-of-ammo warnings still play. Use your own HUD, for example
--- `Workshop/exu_ring_gauge.lua`. Needs no OpenShim. Returns false when hiding failed (unsupported build, or a
--- draw call another module already patched), in which case the part stays visible.
--- With a compatible OpenShim provider, suppression uses an explicit external-HUD
--- lease: OpenShim yields its draw/tint/fade handling for the claimed part instead
--- of both DLLs replacing the same CALL bytes. Saved OpenShim settings are unchanged.
--- Mission scoped: resets to visible when the Lua state closes, so set it again after loading a save.
--- ```lua
--- local applied = exu.SetStockStatusHudVisible("hull", false)
--- ```
--- @param part StockStatusHudPart
--- @param visible boolean
--- @return boolean applied
function exu.SetStockStatusHudVisible(part, visible) end

--- Gets whether or not the global turbo mode patch is enabled (default false).
--- ```lua
--- local globalTurbo = exu.GetGlobalTurbo()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetGlobalTurbo() end

--- Sets the state of the global turbo mode patch.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call.
--- Returns false when enabling fails because the turbo hooks are not installed (unsupported build or site already patched).
--- ```lua
--- local ok = exu.SetGlobalTurbo(true)
--- if not ok then print("exu.SetGlobalTurbo failed") end
--- ```
--- @param state boolean
--- @return boolean applied
function exu.SetGlobalTurbo(state) end

--- Gets whether a unit has turbo enabled (off by default).
--- ```lua
--- local unitTurbo = exu.GetUnitTurbo(GetPlayerHandle())
--- ```
--- @nodiscard
--- @param h Handle
--- @return boolean
function exu.GetUnitTurbo(h) end

--- Sets turbo for an individual unit that overrides the global setting.
--- `false` is an override too: that unit keeps stock turbo behaviour even while global turbo is on.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call.
--- Returns false for a dead handle, or when enabling fails because the turbo hooks are not installed.
--- ```lua
--- local ok = exu.SetUnitTurbo(GetPlayerHandle(), true)
--- if not ok then print("exu.SetUnitTurbo failed") end
--- ```
--- @param h Handle
--- @param state boolean
--- @return boolean applied
function exu.SetUnitTurbo(h, state) end

--- Gets the state of the ordnance velocity inheritance patch (see below).
--- ```lua
--- local ordnanceVelocInheritance = exu.GetOrdnanceVelocInheritance()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetOrdnanceVelocInheritance() end

--- Makes projectiles inherit the velocity of the shooter, and patches the TLI for both the player and AI to account for this, currently does NOT work in multiplayer, and the TLI on mortars is not accurate.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call.
--- ```lua
--- local ok = exu.SetOrdnanceVelocInheritance(true)
--- if not ok then print("exu.SetOrdnanceVelocInheritance failed") end
--- ```
--- @param state boolean
--- @return boolean applied
function exu.SetOrdnanceVelocInheritance(state) end

--- Returns whether or not the hovercraft shot convergence patch is enabled.
--- ```lua
--- local shotConvergence = exu.GetShotConvergence()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetShotConvergence() end

--- Sets the hovercraft shot convergence patch true or false (default false).
--- ```lua
--- exu.SetShotConvergence(true)
--- ```
--- @param enabled boolean
function exu.SetShotConvergence(enabled) end

--- Returns whether or not the local player's smart-reticle shot convergence patch is enabled.
--- This adjusts the local user's hovercraft weapon-aim transform toward the current reticle world position,
--- without requiring an explicit target lock.
--- ```lua
--- local playerReticleShotConvergence = exu.GetPlayerReticleShotConvergence()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetPlayerReticleShotConvergence() end

--- Sets the local player's smart-reticle shot convergence patch true or false (default false).
--- ```lua
--- exu.SetPlayerReticleShotConvergence(true)
--- ```
--- @param enabled boolean
function exu.SetPlayerReticleShotConvergence(enabled) end

--- Gets the global unit VO throttle window in milliseconds.
--- Unit barks attempted inside this window are dropped before enqueue.
--- ```lua
--- local unitVoThrottle = exu.GetUnitVoThrottle()
--- ```
--- @nodiscard
--- @return integer
function exu.GetUnitVoThrottle() end

--- Sets the global unit VO throttle window in milliseconds.
--- `0` disables throttling.
--- ```lua
--- exu.SetUnitVoThrottle(1500)
--- ```
--- @param milliseconds integer
function exu.SetUnitVoThrottle(milliseconds) end

--- Returns the maximum number of queued unit barks EXU allows before it compacts the queue.
--- `0` disables queue depth compaction.
--- ```lua
--- local unitVoQueueDepthLimit = exu.GetUnitVoQueueDepthLimit()
--- ```
--- @nodiscard
--- @return integer
function exu.GetUnitVoQueueDepthLimit() end

--- Sets the maximum number of queued unit barks EXU allows before it compacts the queue.
--- `0` disables queue depth compaction.
--- ```lua
--- exu.SetUnitVoQueueDepthLimit(4)
--- ```
--- @param depth integer
function exu.SetUnitVoQueueDepthLimit(depth) end

--- Returns the queue age threshold in milliseconds used to flush stale unit bark backlogs.
--- `0` disables stale-queue compaction.
--- ```lua
--- local unitVoQueueStaleMs = exu.GetUnitVoQueueStaleMs()
--- ```
--- @nodiscard
--- @return integer
function exu.GetUnitVoQueueStaleMs() end

--- Sets the queue age threshold in milliseconds used to flush stale unit bark backlogs.
--- `0` disables stale-queue compaction.
--- ```lua
--- exu.SetUnitVoQueueStaleMs(3000)
--- ```
--- @param milliseconds integer
function exu.SetUnitVoQueueStaleMs(milliseconds) end

--- Returns whether EXU is currently suppressing likely unit VO bark enqueue requests.
--- This does not affect unrelated mission audio such as `AudioMessage("misn*.wav")`.
--- ```lua
--- local unitVoMuted = exu.GetUnitVoMuted()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetUnitVoMuted() end

--- Sets whether EXU suppresses likely unit VO bark enqueue requests before they enter the stock queue.
--- This only affects filenames that pass EXU's stock unit-bark heuristic.
--- ```lua
--- exu.SetUnitVoMuted(true)
--- ```
--- @param muted boolean
function exu.SetUnitVoMuted(muted) end

--- Returns the alternate bark list for the given unit VO filename.
--- This only affects filenames that pass EXU's stock unit-bark heuristic.
--- ```lua
--- local alternates = exu.GetUnitVoAlternates("my_bark.wav")
--- if not alternates then return end
--- ```
--- @nodiscard
--- @param filename string
--- @return string[] | nil
function exu.GetUnitVoAlternates(filename) end

--- Replaces the bark rotation for the given unit VO filename.
--- Pass `nil` as the second argument to clear the mapping.
--- ```lua
--- exu.SetUnitVoAlternates("my_bark.wav", { "my_bark1.wav", "my_bark2.wav" }) -- pass nil to clear
--- ```
--- @param filename string
--- @param alternates string[] | nil
function exu.SetUnitVoAlternates(filename, alternates) end

--- Play Option
---
--- These functions query and modify certain options in the stock options menu.
--- Reminder, just because you can change these settings via script doesn't mean you should,
--- these have accessibility implications and forcing TLI off for example can make the game frustrating to play.

--- Gets the local player's difficulty setting.
--- ```lua
--- local difficulty = exu.GetDifficulty()
--- ```
--- @nodiscard
--- @return number
function exu.GetDifficulty() end

--- Sets the local player's difficulty setting. See the difficulty enum for options.
--- Note that it won't change what it says in the menu, but it will in fact change in-game.
--- It also won't work in multiplayer where the difficulty is locked to very hard.
--- Values outside 0-4 raise an error. The previous value is restored when the mission ends.
--- ```lua
--- exu.SetDifficulty(exu.DIFFICULTY.HARD)
--- ```
--- @param difficulty number
function exu.SetDifficulty(difficulty) end

--- Gets the local player's automatic leveling setting.
--- ```lua
--- local autoLevel = exu.GetAutoLevel()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetAutoLevel() end

--- Sets the local player's automatic leveling setting.
--- ```lua
--- exu.SetAutoLevel(true)
--- ```
--- @param newAL boolean
function exu.SetAutoLevel(newAL) end

--- Gets the local player's target lead indicator setting.
--- ```lua
--- local tli = exu.GetTLI()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetTLI() end

--- Sets the local player's target lead indicator setting.
--- ```lua
--- exu.SetTLI(true)
--- ```
--- @param newTLI boolean
function exu.SetTLI(newTLI) end

--- Gets the local player's reverse mouse setting.
--- ```lua
--- local reverseMouse = exu.GetReverseMouse()
--- ```
--- @nodiscard
--- @return boolean
function exu.GetReverseMouse() end

--- Sets the local player's reverse mouse setting.
--- ```lua
--- exu.SetReverseMouse(true)
--- ```
--- @param newMouse boolean
function exu.SetReverseMouse(newMouse) end

--- Radar
---
--- These functions can query and modify the in game radar.

--- Gets the state of the radar. See the radar enum for options. 0 = minimap 1 = radar
--- ```lua
--- local radarState = exu.GetRadarState()
--- ```
--- @nodiscard
--- @return number
function exu.GetRadarState() end

--- Sets the current radar state.
--- ```lua
--- exu.SetRadarState(exu.RADAR.RADAR)
--- ```
--- @param state number
function exu.SetRadarState(state) end

--- Gets the radar-specific HUD size scale. 1.0 is the stock full-size radar.
--- Values below 1 shrink the radar, values above 1 enlarge it.
--- ```lua
--- local radarSizeScale = exu.GetRadarSizeScale()
--- ```
--- @nodiscard
--- @return number
function exu.GetRadarSizeScale() end

--- Sets the radar-specific HUD size scale and refreshes the radar layout immediately.
--- Note that changing the game's HUD scaling option may overwrite this value.
--- ```lua
--- exu.SetRadarSizeScale(1.5)
--- ```
--- @param scale number
function exu.SetRadarSizeScale(scale) end

--- Rebuilds the live map bounds and minimap extents from the current edge_path.
--- This uses the game's native refresh path and can be called after native edge_path edits.
--- ```lua
--- exu.RefreshEdgePathBounds()
--- ```
function exu.RefreshEdgePathBounds() end

--- Replaces the existing edge_path point coordinates in place and refreshes the live map bounds.
--- The number of points must match the map's existing edge_path exactly.
--- Each entry may be a vector-like userdata/table with x/z fields, or a two-number array {x, z}.
--- ```lua
--- exu.SetEdgePathCoords({ { 0, 0 }, { 0, 1000 }, { 1000, 1000 }, { 1000, 0 } }) -- point count must match the map's edge_path
--- ```
--- @param points table
function exu.SetEdgePathCoords(points) end

--- Reticle
---
--- These functions can query various information about the smart reticle.

--- Gets the transform matrix of the reticle.
--- ```lua
--- local m = exu.GetReticleMatrix()
--- ```
--- @nodiscard
--- @return Matrix
function exu.GetReticleMatrix() end

--- Gets the handle of the object under the local user's reticle, as well as the satellite cursor.
--- (The object that appears next to "SPACE No Action (object)" in the command interface).
--- The object must be within smart reticle range in order to be detected.
--- ```lua
--- local target = exu.GetReticleObject()
--- ```
--- @nodiscard
--- @return Handle
function exu.GetReticleObject() end

--- Gets the location on terrain of the player's reticle.
--- Must be within smart reticle range, otherwise it will return the last known value.
--- Additionally, while looking at an object or into the sky it will return the last known value.
--- ```lua
--- local pos = exu.GetReticlePos()
--- ```
--- @nodiscard
--- @return Vector
function exu.GetReticlePos() end

--- Captures the current smart-reticle hit without returning a stale terrain position.
--- Returns nil for no hit, out of range, an invalid object, or an unsupported build.
--- Object hits return "object", handle, nil; terrain hits return "terrain", nil, position.
--- This reads the current cockpit smart-reticle result; it does not extend its range.
--- ```lua
--- local kind, object, position = exu.GetReticleHit()
--- if not kind then return end
--- ```
--- @return string? kind
--- @return Handle? object
--- @return Vector? position
function exu.GetReticleHit() end

--- Gets the current range of the smart cursor (default 200m).
--- ```lua
--- local reticleRange = exu.GetReticleRange()
--- ```
--- @nodiscard
--- @return number
function exu.GetReticleRange() end

--- Sets the current range of the smart cursor.
--- ```lua
--- exu.SetReticleRange(100)
--- ```
--- @param range number
function exu.SetReticleRange(range) end

--- Satellite
---
--- These functions can query various information about the satellite view.

--- Gets the current position of the player's camera in satellite view.
--- The Y coordinate becomes static when you pan the camera, otherwise it follows the player's Y coordinate.
--- ```lua
--- local pos = exu.GetSatCameraPos()
--- ```
--- @nodiscard
--- @return Vector
function exu.GetSatCameraPos() end

--- Gets the last position the player clicked in satellite view.
--- Persists after satellite is closed so be sure to handle this case.
--- ```lua
--- local pos = exu.GetSatClickPos()
--- ```
--- @nodiscard
--- @return Vector
function exu.GetSatClickPos() end

--- Gets the current position of the cursor in satellite view.
--- If satellite is disabled it will return the last known position of the cursor.
--- ```lua
--- local pos = exu.GetSatCursorPos()
--- ```
--- @nodiscard
--- @return Vector
function exu.GetSatCursorPos() end

--- Gets the maximum zoom level of satellite view.
--- Default = `8`
--- ```lua
--- local satMaxZoom = exu.GetSatMaxZoom()
--- ```
--- @nodiscard
--- @return number
function exu.GetSatMaxZoom() end

--- Sets the maximum zoom level of satellite view.
--- Default = `8`
--- ```lua
--- exu.SetSatMaxZoom(8)
--- ```
--- @param zoom number
function exu.SetSatMaxZoom(zoom) end

--- Gets the minimum zoom level of satellite view.
--- Default = `2`
--- ```lua
--- local satMinZoom = exu.GetSatMinZoom()
--- ```
--- @nodiscard
--- @return number
function exu.GetSatMinZoom() end

--- Sets the minimum zoom level of satellite view.
--- Default = `2`
--- ```lua
--- exu.SetSatMinZoom(2)
--- ```
--- @param zoom number
function exu.SetSatMinZoom(zoom) end

--- Gets the current pan speed of satellite view.
--- Default = `1250`
--- ```lua
--- local satPanSpeed = exu.GetSatPanSpeed()
--- ```
--- @nodiscard
--- @return number
function exu.GetSatPanSpeed() end

--- Sets the current pan speed of satellite view.
--- Default = `1250`
--- ```lua
--- exu.SetSatPanSpeed(1250)
--- ```
--- @param speed number
function exu.SetSatPanSpeed(speed) end

--- Gets the satellite state of the local player. See the satellite enum for options.
--- ```lua
--- local isSatellite = exu.GetSatState() == exu.SATELLITE.ENABLED
--- ```
--- @nodiscard
--- @return SatelliteState
function exu.GetSatState() end

--- Gets the current zoom level of satellite view.
--- Default = `4`
--- ```lua
--- local satZoom = exu.GetSatZoom()
--- ```
--- @nodiscard
--- @return number
function exu.GetSatZoom() end

--- Sets the current zoom level of satellite view.
--- Make sure to stay within the boundaries defined by min and max zoom, otherwise it'll cause issues.
--- ```lua
--- exu.SetSatZoom(4)
--- ```
--- @param zoom number
function exu.SetSatZoom(zoom) end

--- Steam
---
--- These functions can get information from steam.

--- Gets the Steam 64 ID of the local player.
--- ```lua
--- local steam64 = exu.GetSteam64()
--- ```
--- @nodiscard
--- @return string
function exu.GetSteam64() end

--- Sound Options
---
--- These functions can query information about the stock sound bar settings.

--- Gets the value of the music slider from 0-10.
--- ```lua
--- local musicVolume = exu.GetMusicVolume()
--- ```
--- @nodiscard
--- @return number
function exu.GetMusicVolume() end

--- Stock Extensions
---
--- These functions are either existing in game but unbound in stock lua, or are in stock lua but were removed.

--- Interprets the given string as lua code and executes it.
--- ```lua
--- exu.DoString([[print('hello from DoString')]])
--- ```
--- @param code string
function exu.DoString(code) end

--- Returns the inverse of the given matrix.
--- ```lua
--- local inverse = exu.MatrixInverse(GetTransform(GetPlayerHandle()))
--- ```
--- @nodiscard
--- @param mat Matrix
--- @return Matrix
function exu.MatrixInverse(mat) end

--- Transforms the given screen coordinates in pixels to a position in the world.
--- Returns a unit vector that originates at your camera and points into the world in the direction that matches the position of the given coordinates on screen.
--- Note that if you want something to appear at a consistent place on the screen regardless of resolution, you should use a ratio of the current resolution like (currentScreenWidth * 0.5) rather than a hardcoded pixel value.
--- ```lua
--- local worldPos = exu.ScreenToWorld(960, 540)
--- ```
--- @nodiscard
--- @param screenX integer
--- @param screenY integer
--- @return Vector
function exu.ScreenToWorld(screenX, screenY) end

--- Unrotates the given vector by the given perspective matrix.
--- ```lua
--- local localDir = exu.VectorUnrotate(SetVector(0, 0, 1), GetTransform(GetPlayerHandle()))
--- ```
--- @nodiscard
--- @param v Vector
--- @param perspectiveMat Matrix
--- @return Vector
function exu.VectorUnrotate(v, perspectiveMat) end

--- Runtime exports that predated their LuaLS declarations.
--- These conservative declarations keep editor/runtime API parity exact.
--- ```lua
--- local cleared = exu.ClearAiUnitTuning(GetHandle("tank1"))
--- ```
--- @param h Handle
--- @return boolean cleared false when OpenShim lacks the export or the handle is dead
function exu.ClearAiUnitTuning(h) end

--- ```lua
--- local cleared = exu.ClearAllAiUnitTuning()
--- ```
--- @return boolean cleared false when OpenShim lacks the export
function exu.ClearAllAiUnitTuning() end

--- Placeholder: clears nothing. Debug line and box drawing is not implemented yet;
--- the function exists so scripts that call it keep working.
--- ```lua
--- exu.ClearVisuals()
--- ```
function exu.ClearVisuals() end

--- Placeholder: checks its arguments and draws nothing. Debug line and box drawing
--- is not implemented yet; the function exists so scripts that call it keep working.
--- ```lua
--- exu.DrawBox(SetVector(-5, 0, -5), SetVector(5, 5, 5), { r = 1, g = 0, b = 0, a = 1 }) -- placeholder, draws nothing yet
--- ```
--- @param min Vector
--- @param max Vector
--- @param color Color
function exu.DrawBox(min, max, color) end

--- Placeholder: checks its arguments and draws nothing. Debug line and box drawing
--- is not implemented yet; the function exists so scripts that call it keep working.
--- ```lua
--- exu.DrawLine(GetPosition(GetPlayerHandle()), GetPosition(GetHandle("tank1")), { r = 1, g = 0, b = 0, a = 1 })
--- ```
--- @param from Vector
--- @param to Vector
--- @param color Color
function exu.DrawLine(from, to, color) end

--- ```lua
--- local enabled = exu.GetAiTargetScoringEnabled()
--- ```
--- @return boolean enabled
function exu.GetAiTargetScoringEnabled() end

--- ```lua
--- local enabled = exu.GetAiTargetSelectEnabled()
--- ```
--- @return boolean enabled
function exu.GetAiTargetSelectEnabled() end

--- ```lua
--- local tuning = exu.GetAiUnitTuning(GetHandle("tank1"))
--- if tuning then print(tuning.engageRange) end
--- ```
--- @param h Handle
--- @return table|nil tuning only the fields that are set: engageRange, weaponRangeMin, retargetPeriod, kiteDesiredRange, kiteEnterRange, kiteExitRange, kitePreserveLos, kiteStrafe, kiteSwitchPeriod; nil when none is set
function exu.GetAiUnitTuning(h) end

--- ```lua
--- local distance = exu.GetCullDistance()
--- ```
--- @return number
function exu.GetCullDistance() end

--- ```lua
--- local enabled = exu.GetCullingEnabled()
--- ```
--- @return boolean
function exu.GetCullingEnabled() end

--- ```lua
--- local x, y, width, height = exu.GetHudSpriteRect("my_sprite")
--- if not x then return end -- unknown sprite or no OpenShim
--- ```
--- @param spriteName string
--- @return integer? x
--- @return integer? y
--- @return integer? width
--- @return integer? height
function exu.GetHudSpriteRect(spriteName) end

--- ```lua
--- local enabled = exu.GetInfiniteAmmo()
--- ```
--- @return boolean
function exu.GetInfiniteAmmo() end

--- ```lua
--- local enabled = exu.GetInfiniteScrap()
--- ```
--- @return boolean
function exu.GetInfiniteScrap() end

--- Returns the engine's selected track (-1 if none), or nil if unavailable.
--- Stop retains the selection; this is not a playing/audibility test.
--- ```lua
--- local track = exu.GetMusicTrack()
--- if not track then return end
--- ```
--- @return integer | nil
function exu.GetMusicTrack() end

--- ```lua
--- local mode = exu.GetOrdnanceVelocMode()
--- ```
--- @return integer mode 0 or 1
function exu.GetOrdnanceVelocMode() end

--- ```lua
--- local enabled = exu.GetViewportOverlaysEnabled()
--- ```
--- @return boolean
function exu.GetViewportOverlaysEnabled() end

--- ```lua
--- local mask = exu.GetWeaponMask()
--- ```
--- @return integer
function exu.GetWeaponMask() end

--- ```lua
--- local wireframe = exu.GetWireframe()
--- ```
--- @return boolean
function exu.GetWireframe() end

--- Pauses the native soundtrack without releasing its stream; freezes an EXU fade.
--- ```lua
--- local ok = exu.PauseMusic()
--- if not ok then print("exu.PauseMusic failed") end
--- ```
--- @return boolean accepted
function exu.PauseMusic() end

--- ```lua
--- local reset = exu.ResetMissionHookOverrides()
--- ```
--- @return boolean reset false when OpenShim lacks the export
function exu.ResetMissionHookOverrides() end

-------------------------------------------------------------------------------
-- Small calls (idea credit: GoombaBZ's bzx; reimplemented from the decompiles)
-------------------------------------------------------------------------------

--- Clears the engine's chat/message list (the engine's own clear, run when a
--- network game ends). Local to this machine.
--- ```lua
--- exu.ClearMessages()
--- ```
--- @return boolean ok
function exu.ClearMessages() end

--- The sound played when a pilot ejects from a destroyed craft (stock "bail.wav").
--- ```lua
--- local name = exu.GetEjectSound()
--- ```
--- @nodiscard
--- @return string | nil name nil when the engine string cannot be read
function exu.GetEjectSound() end

--- Replaces the pilot eject sound for every craft; nil restores "bail.wav".
--- The stock sound returns when the Lua state closes. Local to this machine.
--- ```lua
--- exu.SetEjectSound("mybail.wav")
--- exu.SetEjectSound(nil)
--- ```
--- @param name string? 1-63 chars of A-Z a-z 0-9 . _ - (a bare file name)
--- @return boolean ok
function exu.SetEjectSound(name) end

--- The sound played when the player hops out of a craft (stock "jump.wav").
--- ```lua
--- local name = exu.GetHopOutSound()
--- ```
--- @nodiscard
--- @return string | nil name
function exu.GetHopOutSound() end

--- Replaces the hop-out sound; nil restores "jump.wav". Only the hop-out
--- sound changes: other "jump.wav" uses keep the stock name. Restored when the
--- Lua state closes. Local to this machine.
--- ```lua
--- exu.SetHopOutSound("myhop.wav")
--- ```
--- @param name string? 1-63 chars of A-Z a-z 0-9 . _ -
--- @return boolean ok
function exu.SetHopOutSound(name) end

--- A craft's pilot eject ratio (0..1, ODF personEjectRatio). The stock death
--- roll ejects the pilot when ratio * 2 - 1 < random.
--- ```lua
--- local ratio = exu.GetPersonEjectRatio(h)
--- ```
--- @nodiscard
--- @param h Handle
--- @return number | nil ratio nil when the handle is stale or not a craft
function exu.GetPersonEjectRatio(h) end

--- Sets a craft's pilot eject ratio. Per object; in multiplayer the death roll
--- runs on the owning machine, so set it there.
--- ```lua
--- exu.SetPersonEjectRatio(h, 0) -- never eject
--- ```
--- @param h Handle
--- @param ratio number 0..1
--- @return boolean ok false for a non-craft, a dead handle or a bad ratio
function exu.SetPersonEjectRatio(h, ratio) end

--- Spawns a stock scrap field (the ScrapField object's own call): count
--- scrap pieces scattered around the position. The engine caps the count to
--- the free scrap slots. The scrap is local to the calling machine; in
--- multiplayer call it on every machine.
--- ```lua
--- exu.MakeScrapField(GetPosition(h), 8)
--- ```
--- @param position Vector
--- @param count integer 1..1024
--- @return boolean ok
function exu.MakeScrapField(position, count) end

--- True when the satellite view is available to the player (a powered comm
--- tower, the free-satellite option, or arcade).
--- ```lua
--- if exu.HasSatellite() then exu.ActivateSatellite() end
--- ```
--- @nodiscard
--- @return boolean
function exu.HasSatellite() end

--- Switches the player into the satellite view, like pressing the satellite
--- key. Does nothing when the satellite is unavailable. Use exu.GetSatState
--- to read the current state.
--- ```lua
--- local active = exu.ActivateSatellite()
--- ```
--- @return boolean active satellite view is on after the call
function exu.ActivateSatellite() end

--- Leaves the satellite view for the previous view.
--- ```lua
--- exu.DeactivateSatellite()
--- ```
--- @return boolean inactive satellite view is off after the call
function exu.DeactivateSatellite() end

--- Plays a craft's unit voice message through the stock path: only for units
--- on the player's team, with the stock repeat throttle and mutes (and the EXU
--- unit-voice policy). Local to this machine.
--- Names: select_wait, select_go, select_follow, select_attack, select_pickup,
--- select_dropoff, select_deploy, select_other, go, go_object, follow,
--- follow_me, attack, repair, reload, rescue, recycle, other, deployed, packed,
--- killed, died, blocked; or a number 0..26.
--- ```lua
--- exu.UnitMessage(h, "attack")
--- ```
--- @param h Handle
--- @param msg string | integer
--- @return boolean called false for a non-craft or an unknown message; true does not mean a clip played
function exu.UnitMessage(h, msg) end

--- Queues a craft's unit voice clip directly, skipping the team check,
--- throttle and mutes of exu.UnitMessage. Priority follows the engine queue:
--- 0 append, 1 flush the queue first, 2 replace the playing clip, 5 drop when
--- busy, 6 drop unless the queue is idle or low priority. Local to this machine.
--- ```lua
--- exu.UnitMessageDirect(h, "killed", 1)
--- ```
--- @param h Handle
--- @param msg string | integer
--- @param priority integer? 0..6, default 0
--- @return boolean queued
function exu.UnitMessageDirect(h, msg, priority) end

--- Stops the playing unit voice clip and clears the queue.
--- ```lua
--- exu.StopUnitMessage()
--- ```
--- @return boolean ok
function exu.StopUnitMessage() end

--- True while a unit voice clip is playing or queued.
--- ```lua
--- if not exu.IsUnitMessagePlaying() then exu.UnitMessage(h, "go") end
--- ```
--- @nodiscard
--- @return boolean
function exu.IsUnitMessagePlaying() end

--- Typed persistent settings, stored in the exu.storage namespace
--- "exu.settings" as { [section] = { [label] = value } }. The section defaults
--- to "General". Each call reads the file, so cache values you read often.
--- Settings live on each machine; they are not synced in multiplayer.
--- Returns the default (or nil) and false when the key is missing or holds
--- another type.
--- ```lua
--- local hard, found = exu.GetSettingBool("MyMod", "HardMode", false)
--- local hard2 = exu.GetSettingBool("HardMode", false) -- section "General"
--- ```
--- @nodiscard
--- @param section string? 1-64 printable ASCII chars
--- @param label string 1-64 printable ASCII chars
--- @param default boolean?
--- @return boolean | nil value
--- @return boolean found
function exu.GetSettingBool(section, label, default) end

--- Integer setting; see exu.GetSettingBool.
--- ```lua
--- local waves = exu.GetSettingInt("MyMod", "Waves", 3)
--- ```
--- @nodiscard
--- @param section string?
--- @param label string
--- @param default integer?
--- @return integer | nil value
--- @return boolean found
function exu.GetSettingInt(section, label, default) end

--- Number setting; see exu.GetSettingBool.
--- ```lua
--- local scale = exu.GetSettingFloat("MyMod", "Scale", 1.0)
--- ```
--- @nodiscard
--- @param section string?
--- @param label string
--- @param default number?
--- @return number | nil value
--- @return boolean found
function exu.GetSettingFloat(section, label, default) end

--- String setting; see exu.GetSettingBool. One argument is the label; two
--- arguments are always (section, label), so pass the section to give a default.
--- ```lua
--- local name = exu.GetSettingString("MyMod", "Callsign", "Red 1")
--- ```
--- @nodiscard
--- @param section string?
--- @param label string
--- @param default string?
--- @return string | nil value
--- @return boolean found
function exu.GetSettingString(section, label, default) end

--- Saves a boolean setting ([section] defaults to "General").
--- ```lua
--- local ok, err = exu.SaveSettingBool("MyMod", "HardMode", true)
--- ```
--- @param section string?
--- @param label string
--- @param value boolean
--- @return boolean ok
--- @return string | nil err
function exu.SaveSettingBool(section, label, value) end

--- Saves an integer setting.
--- ```lua
--- exu.SaveSettingInt("MyMod", "Waves", 5)
--- ```
--- @param section string?
--- @param label string
--- @param value integer
--- @return boolean ok
--- @return string | nil err
function exu.SaveSettingInt(section, label, value) end

--- Saves a finite number setting.
--- ```lua
--- exu.SaveSettingFloat("MyMod", "Scale", 1.5)
--- ```
--- @param section string?
--- @param label string
--- @param value number
--- @return boolean ok
--- @return string | nil err
function exu.SaveSettingFloat(section, label, value) end

--- Saves a string setting.
--- ```lua
--- exu.SaveSettingString("MyMod", "Callsign", "Red 1")
--- ```
--- @param section string?
--- @param label string
--- @param value string
--- @return boolean ok
--- @return string | nil err
function exu.SaveSettingString(section, label, value) end

--- Removes one setting; an emptied section is removed too.
--- ```lua
--- exu.DeleteSetting("MyMod", "HardMode")
--- ```
--- @param section string?
--- @param label string
--- @return boolean ok
--- @return string | nil err
function exu.DeleteSetting(section, label) end

--- ```lua
--- local ok = exu.ResetOverlaySupport("mission-end")
--- if not ok then print("overlay reset failed") end
--- ```
--- @param reason string? Diagnostic label for the log; defaults to "lua-reset"
--- @return boolean success
function exu.ResetOverlaySupport(reason) end

--- ```lua
--- local restored = exu.RestoreAllHudSprites()
--- ```
--- @return boolean restored false when OpenShim lacks the export
function exu.RestoreAllHudSprites() end

--- ```lua
--- local restored = exu.RestoreHudSprite("my_sprite")
--- ```
--- @param spriteName string
--- @return boolean restored false when OpenShim lacks the export
function exu.RestoreHudSprite(spriteName) end

--- Resumes a paused soundtrack at the retained position. Does not start stopped music.
--- ```lua
--- local ok = exu.ResumeMusic()
--- if not ok then print("exu.ResumeMusic failed") end
--- ```
--- @return boolean accepted
function exu.ResumeMusic() end

--- ```lua
--- local accepted = exu.SetAiOdfGameplayTuningEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean accepted false when OpenShim lacks the export
function exu.SetAiOdfGameplayTuningEnabled(enabled) end

--- ```lua
--- local accepted = exu.SetAiTargetScoringEnabled(true)
--- if not accepted then print("target scoring hook unavailable") end
--- ```
--- @param enabled boolean
--- @return boolean accepted false when the native hook could not be installed
function exu.SetAiTargetScoringEnabled(enabled) end

--- ```lua
--- local accepted = exu.SetAiTargetSelectEnabled(true)
--- if not accepted then print("target select hook unavailable") end
--- ```
--- @param enabled boolean
--- @return boolean accepted false when the native hook could not be installed
function exu.SetAiTargetSelectEnabled(enabled) end

--- ```lua
--- local accepted = exu.SetAiUnitTuning(GetHandle("tank1"), { engageRange = 120, retargetPeriod = 0.5 })
--- if not accepted then print("AI unit tuning unavailable") end
--- ```
--- @param h Handle
--- @param tuning table? Optional numeric fields engageRange, weaponRangeMin, retargetPeriod, kiteDesiredRange, kiteEnterRange, kiteExitRange, kiteStrafe, kiteSwitchPeriod and boolean kitePreserveLos; omitted keys are unset, an empty table or nil clears
--- @return boolean accepted false when OpenShim lacks the export or the handle is dead
function exu.SetAiUnitTuning(h, tuning) end

--- True when EXU qualifies and installs its native Craft damage hook (no OpenShim needed).
--- Currently qualified on Windows/GOG Redux 2.2.301; unsupported builds return false.
--- ```lua
--- local available = exu.HasNativeDamageResistance()
--- ```
--- @return boolean available
function exu.HasNativeDamageResistance() end

--- Sets mission-scoped incoming Craft damage scaling before health/death.
--- 0 is immunity, 1 restores stock damage. Healing and direct SetCurHealth edits
--- are unchanged. Applies after stock difficulty adjustment; never changes the
--- shared blast damage record. Reapply after loading a mission save.
--- ```lua
--- local accepted = exu.SetUnitDamageMultiplier(GetHandle("tank1"), 0.5)
--- ```
--- @param h Handle live craft handle
--- @param multiplier number finite number in [0, 1]
--- @return boolean accepted false for stale handles or unavailable native support
function exu.SetUnitDamageMultiplier(h, multiplier) end

--- Clears an override, including after destruction. Does not dereference h.
--- ```lua
--- local ok = exu.ClearUnitDamageMultiplier(GetPlayerHandle())
--- if not ok then print("exu.ClearUnitDamageMultiplier failed") end
--- ```
--- @param h Handle
--- @return boolean accepted
function exu.ClearUnitDamageMultiplier(h) end

--- Clears all mission damage overrides. EXU also does this at VM teardown/init.
--- ```lua
--- local ok = exu.ResetUnitDamageMultipliers()
--- if not ok then print("exu.ResetUnitDamageMultipliers failed") end
--- ```
--- @return boolean accepted
function exu.ResetUnitDamageMultipliers() end

--- ```lua
--- local accepted = exu.SetAttackRevealEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean accepted false when OpenShim lacks the export
function exu.SetAttackRevealEnabled(enabled) end

--- Deprecated: does nothing. Bomber range is now part of OpenShim's
--- AiOdfGameplayTuning; use exu.SetAiOdfGameplayTuningEnabled.
--- ```lua
--- local accepted = exu.SetBomberAiRangeEnabled(true) -- deprecated
--- ```
--- @deprecated
--- @param enabled boolean
--- @return boolean accepted
function exu.SetBomberAiRangeEnabled(enabled) end

--- Turns the OpenShim Legacy Tank AI (1.4/TRO strafe-and-retreat attack AI) on or off for this mission. Single player only; reverts at mission end. Returns false if OpenShim is missing.
--- ```lua
--- local ok = exu.SetLegacyTankAiEnabled(true)
--- if not ok then print("exu.SetLegacyTankAiEnabled failed") end
--- ```
--- @param enabled boolean
--- @return boolean accepted
function exu.SetLegacyTankAiEnabled(enabled) end

--- Selects which teams use the OpenShim Legacy Tank AI (1.4/TRO strafe-and-retreat attack AI). Single player only; reverts at mission end. Returns false if OpenShim is missing or too old.
--- Modes: 0 = off, 1 = all teams, 2 = AI teams only (not the player's team).
--- ```lua
--- local ok = exu.SetLegacyTankAiMode(1)
--- if not ok then print("exu.SetLegacyTankAiMode failed") end
--- ```
--- @param mode integer 0, 1 or 2
--- @return boolean accepted
function exu.SetLegacyTankAiMode(mode) end

--- Makes Follow orders use Formation rules (fire at the closest enemy, no chasing). Returns false if OpenShim or the export is missing, or OpenShim refuses (e.g. multiplayer).
--- ```lua
--- local ok = exu.SetFollowEngageEnabled(true)
--- if not ok then print("exu.SetFollowEngageEnabled failed") end
--- ```
--- @param enabled boolean
--- @return boolean accepted
function exu.SetFollowEngageEnabled(enabled) end

--- Makes Defend keep up with a moving target. Returns false if OpenShim or the export is missing, or OpenShim refuses (e.g. multiplayer).
--- ```lua
--- local ok = exu.SetDefendRefreshEnabled(true)
--- if not ok then print("exu.SetDefendRefreshEnabled failed") end
--- ```
--- @param enabled boolean
--- @return boolean accepted
function exu.SetDefendRefreshEnabled(enabled) end

--- Marks a unit as held. Issue stock Defend(h, 0) (Defend with no target) first. A held unit stands still, fires at the closest enemy and never chases. Returns false if OpenShim or the export is missing, or OpenShim refuses (e.g. multiplayer).
--- ```lua
--- local accepted = exu.SetAiHold(GetHandle("tank1"), true)
--- ```
--- @param h Handle
--- @param hold boolean
--- @return boolean accepted
function exu.SetAiHold(h, hold) end

--- Sets the camera distance beyond which units are hidden while culling is enabled.
--- ```lua
--- exu.SetCullDistance(800)
--- ```
--- @param distance number finite, non-negative
function exu.SetCullDistance(distance) end

--- Enables or disables distance culling of units. Disabling it (or raising the distance)
--- shows again every unit culling hid; units hidden by the game itself are not touched.
--- ```lua
--- exu.SetCullingEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetCullingEnabled(enabled) end

--- ```lua
--- local accepted = exu.SetHowitzerVolleyEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean accepted false when OpenShim lacks the export
function exu.SetHowitzerVolleyEnabled(enabled) end

--- ```lua
--- local accepted = exu.SetHudSpriteRect("my_sprite", 10, 10, 64, 64)
--- if not accepted then print("sprite rect not applied") end
--- ```
--- @param spriteName string
--- @param x integer
--- @param y integer
--- @param width integer
--- @param height integer
--- @return boolean accepted false when OpenShim lacks the export or refuses
function exu.SetHudSpriteRect(spriteName, x, y, width, height) end

--- ```lua
--- local accepted = exu.SetHudSpriteVisible("my_sprite", false)
--- ```
--- @param spriteName string
--- @param visible boolean
--- @return boolean accepted false when OpenShim lacks the export or refuses
function exu.SetHudSpriteVisible(spriteName, visible) end

--- Enables or disables infinite ammo for the local player.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call.
--- ```lua
--- local ok = exu.SetInfiniteAmmo(true)
--- if not ok then print("exu.SetInfiniteAmmo failed") end
--- ```
--- @param enabled boolean
--- @return boolean applied
function exu.SetInfiniteAmmo(enabled) end

--- Enables or disables infinite scrap.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call.
--- ```lua
--- local ok = exu.SetInfiniteScrap(true)
--- if not ok then print("exu.SetInfiniteScrap failed") end
--- ```
--- @param enabled boolean
--- @return boolean applied
function exu.SetInfiniteScrap(enabled) end

--- ```lua
--- local effective = exu.SetJumpSnipeCrouch(false)
--- ```
--- @param enabled boolean
--- @return boolean effective the state in effect; false when OpenShim suppressed it or lacks the export
function exu.SetJumpSnipeCrouch(enabled) end

--- Returns whether an ODF is treated as a plant: the Lua override from `exu.SetPlant` if
--- there is one, otherwise `[PlantClass] isPlant = 1` in the ODF. Plants keep normal
--- collision but lose the building bump sound, chips and building hit explosion, and
--- die with a plant explosion instead of the building death sequence.
--- ```lua
--- local isPlant = exu.IsPlant("avtank")
--- ```
--- @param odf string ODF name, with or without ".odf" (case-insensitive)
--- @return boolean isPlant
function exu.IsPlant(odf) end

--- Overrides the plant verdict of an ODF for this mission. `true` makes it a plant
--- (the ODF's `[PlantClass]` keys hitExplosion, explosionName and dropScrap still
--- apply, with their defaults when absent), `false` forces it to not be a plant, and
--- `nil` removes the override. Overrides beat the ODF `isPlant` flag.
--- In multiplayer this is local to each machine: every peer's mission script must make the same call.
--- ```lua
--- exu.SetPlant("avtank", true) -- pass nil to restore the ODF's own setting
--- ```
--- @param odf string ODF name, with or without ".odf" (case-insensitive)
--- @param enabled boolean|nil
function exu.SetPlant(odf, enabled) end

--- Turns Betty's "ammo depleted" voice for the player's own object on or off. Pass `false`
--- when a script holds the engine ammo at 0 as a dead trigger (reloads, bolt cycling) and
--- would otherwise make Betty speak every time; use `exu.PlayUserAmmoWarning` to play the
--- line when truly dry. The detour is installed on the first `false`; own-team units are
--- unaffected. Mission-scoped: Betty is back on at mission end.
--- ```lua
--- local ok = exu.SetUserAmmoWarning(false)
--- ```
--- @param enabled boolean false silences the user's line, true restores stock behaviour
--- @return boolean ok false when unavailable (unsupported build)
function exu.SetUserAmmoWarning(enabled) end

--- Returns whether Betty's ammo-depleted voice for the player's own object is on (stock).
--- ```lua
--- local enabled = exu.GetUserAmmoWarning()
--- ```
--- @return boolean enabled
function exu.GetUserAmmoWarning() end

--- Queues the stock Betty ammo-depleted line for the player's own object now, regardless
--- of `exu.SetUserAmmoWarning`. Does nothing when there is no user object or the engine
--- function is unavailable.
--- ```lua
--- local ok = exu.PlayUserAmmoWarning()
--- if not ok then print("exu.PlayUserAmmoWarning failed") end
--- ```
--- @return boolean played
function exu.PlayUserAmmoWarning() end

--- Selects and plays a single looping NN.ogg soundtrack through the native manager.
--- Works with EXU alone on the qualified Redux build. Cancels EXU fades.
--- Missing content leaves the current track and fade unchanged.
--- ```lua
--- local accepted = exu.SetMusicTrack(3)
--- ```
--- @param track integer 0..255
--- @return boolean accepted
function exu.SetMusicTrack(track) end

--- Alias of SetMusicTrack.
--- ```lua
--- local accepted = exu.PlayMusic(3)
--- ```
--- @param track integer 0..255
--- @return boolean accepted
function exu.PlayMusic(track) end

--- ```lua
--- exu.SetOrdnanceVelocMode(1)
--- ```
--- @param mode integer 0 or 1
function exu.SetOrdnanceVelocMode(mode) end

--- ```lua
--- local accepted = exu.SetTurretAimPitchEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean accepted false when OpenShim lacks the export
function exu.SetTurretAimPitchEnabled(enabled) end

--- Lets producer rally points work in this network-game mission (co-op).
--- Cleared on every mission reset; the player's setting still decides.
--- ```lua
--- local accepted = exu.AllowRallyPoints(true)
--- ```
--- @param allowed boolean
--- @return boolean accepted false when OpenShim is missing or refuses
function exu.AllowRallyPoints(allowed) end

--- ```lua
--- local accepted = exu.SetUnderAttackAlertMode(2)
--- ```
--- @param mode integer 1 to 3
--- @return boolean accepted false when OpenShim lacks the export
function exu.SetUnderAttackAlertMode(mode) end

--- ```lua
--- exu.SetViewportOverlaysEnabled(false)
--- ```
--- @param enabled boolean
function exu.SetViewportOverlaysEnabled(enabled) end

--- ```lua
--- local accepted = exu.SetWeaponMaskCarrierBiasEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean accepted false when OpenShim lacks the export
function exu.SetWeaponMaskCarrierBiasEnabled(enabled) end

--- ```lua
--- exu.SetWireframe(true)
--- ```
--- @param enabled boolean
function exu.SetWireframe(enabled) end

--- Stops/releases the native soundtrack stream and cancels EXU fades.
--- Repeated calls are harmless; the selected track remains available for read-back.
--- ```lua
--- local ok = exu.StopMusic()
--- if not ok then print("exu.StopMusic failed") end
--- ```
--- @return boolean accepted
function exu.StopMusic() end

--- Reads native engine state; nil means standalone soundtrack qualification failed.
--- playing/paused reflect engine flags, not proof that audio is audible.
--- gain is EXU's temporary 0..1 multiplier; userVolume is the saved 0..10 setting.
--- ```lua
--- local state = exu.GetMusicState()
--- if state then print(state.track, state.playing) end
--- ```
--- @return {track: integer, playing: boolean, paused: boolean, gain: number, fading: boolean, userVolume: integer} | nil
function exu.GetMusicState() end

--- Fades a temporary soundtrack gain without changing saved options or pause state.
--- Call UpdateMusic(dt) once per mission Update to advance nonzero durations.
--- ```lua
--- local accepted = exu.FadeMusic(0.5, 2.0)
--- ```
--- @param gain number 0..1 (0 = silence)
--- @param seconds? number finite, non-negative; default 1
--- @return boolean accepted
function exu.FadeMusic(gain, seconds) end

--- Fades out, selects a looping track, then fades back to the player's volume.
--- This is a sequential transition; Redux exposes one soundtrack stream.
--- Call UpdateMusic(dt) once per mission Update. New requests replace old fades.
--- ```lua
--- local accepted = exu.ChangeMusicTrack(3, 1.0, 1.0)
--- ```
--- @param track integer 0..255
--- @param fadeOut? number finite, non-negative seconds; default 1
--- @param fadeIn? number finite, non-negative seconds; default 1
--- @return boolean accepted
function exu.ChangeMusicTrack(track, fadeOut, fadeIn) end

--- Advances the pending fade on the mission thread and reapplies any temporary gain.
--- An external track/stream change cancels the gain override. Audio is local per peer.
--- ```lua
--- local updated = exu.UpdateMusic(GetTimeStep())
--- ```
--- @param dt number finite, non-negative simulation seconds
--- @return boolean updated
function exu.UpdateMusic(dt) end

--- Cancels a transition and restores the player's volume without changing playback.
--- Also performed on Lua-state teardown.
--- ```lua
--- local ok = exu.ResetMusic()
--- if not ok then print("exu.ResetMusic failed") end
--- ```
--- @return boolean restored
function exu.ResetMusic() end


--- Native player HUD meters (optional OpenShim adapter)
--- Physical viewport pixels, top-left origin. IDs are exactly "hull" and "ammo".
--- No released adapter is qualified yet: availability is currently false.
--- See Docs/NATIVE_HUD_LAYOUT_API.md for qualification and ownership rules.

--- Returns whether OpenShim has a qualified adapter for this meter.
--- False also covers older/missing OpenShim and bootstrap-only winmm.dll.
--- ```lua
--- local isNativeHudLayoutAvailable = exu.IsNativeHudLayoutAvailable("hull")
--- ```
--- @param meter "hull"|"ammo"
--- @return boolean
function exu.IsNativeHudLayoutAvailable(meter) end

--- Gets the effective full bar rectangle, independent of the remaining fill.
--- Returns nil if the adapter or current frame's stock meter is unavailable.
--- ```lua
--- local x, y, width, height = exu.GetNativeHudMeterRect("hull")
--- if not x then return end
--- ```
--- @param meter "hull"|"ammo"
--- @return integer? x
--- @return integer? y
--- @return integer? width
--- @return integer? height
function exu.GetNativeHudMeterRect(meter) end

--- Gets the current stock full bar rectangle before a mission override.
--- Stock geometry is observed again every frame; it follows resolution/UI scale.
--- ```lua
--- local x, y, width, height = exu.GetNativeHudMeterDefaultRect("hull")
--- if not x then return end
--- ```
--- @param meter "hull"|"ammo"
--- @return integer? x
--- @return integer? y
--- @return integer? width
--- @return integer? height
function exu.GetNativeHudMeterDefaultRect(meter) end

--- Requests physical-pixel placement/size at the next native meter draw.
--- Dimensions must be integers in 1..16384; coordinates and far edges must
--- be within -65535..65535. Fractional/non-finite values are rejected.
--- The intended adapter transforms live bar/clip, label and ammo marker/readout.
--- Accepted requests are mission scoped; this checkpoint returns unavailable.
--- ```lua
--- local accepted, reason = exu.SetNativeHudMeterRect("hull", 20, 20, 160, 16)
--- if not accepted then print(reason) end
--- ```
--- @param meter "hull"|"ammo"
--- @param x integer
--- @param y integer
--- @param width integer
--- @param height integer
--- @return boolean accepted
--- @return string? reason invalid_meter|invalid_rect|unavailable|native_state_unavailable
function exu.SetNativeHudMeterRect(meter, x, y, width, height) end

--- Sets meter-group visibility without disabling simulation or warning audio.
--- Requires a boolean. Does not hide unrelated radar, weapons or HUD sprites.
--- ```lua
--- local accepted, reason = exu.SetNativeHudMeterVisible("ammo", false)
--- if not accepted then print(reason) end
--- ```
--- @param meter "hull"|"ammo"
--- @param visible boolean
--- @return boolean accepted
--- @return string? reason
function exu.SetNativeHudMeterVisible(meter, visible) end

--- Removes this meter's placement and visibility override, using current stock layout.
--- ```lua
--- local accepted, reason = exu.RestoreNativeHudMeter("hull")
--- if not accepted then print(reason) end
--- ```
--- @param meter "hull"|"ammo"
--- @return boolean accepted
--- @return string? reason
function exu.RestoreNativeHudMeter(meter) end

--- Restores only meter slots successfully changed through EXU this mission.
--- A no-op with no owned slots succeeds even when the provider is unavailable.
--- Also runs automatically on EXU mission/Lua teardown, including pinned DLLs.
--- ```lua
--- local accepted, reason = exu.RestoreAllNativeHudMeters()
--- if not accepted then print(reason) end
--- ```
--- @return boolean accepted
--- @return string? reason
function exu.RestoreAllNativeHudMeters() end

--- Sets the OpenShim laser-sight mode: 0 = default (follows openshim.ini
--- LaserSight, off in network games), 1 = also allowed in network games.
--- OpenShim resets it to 0 whenever the game leaves a mission, so set it each
--- mission. Needs an OpenShim build with the laser sight; nil without it.
--- Raises an error for anything but 0 or 1.
--- ```lua
--- local inEffect = exu.SetLaserSightMode(1)
--- if not inEffect then return end -- OpenShim lacks the laser sight
--- ```
--- @param mode integer 0 or 1
--- @return integer? modeInEffect
function exu.SetLaserSightMode(mode) end

--- Reads the OpenShim laser-sight mode (see exu.SetLaserSightMode).
--- ```lua
--- local mode = exu.GetLaserSightMode()
--- if not mode then return end
--- ```
--- @return integer? mode nil when OpenShim lacks the laser sight
function exu.GetLaserSightMode() end

--- Path-grid footprint from collision faces (Docs/PATH_BLOCK.md). An ODF
--- opts in under [GameObjectClass] with pathBlock = "faces" (block only the
--- cells whose centre is inside the LOD0 collision faces at pathBlockHeight
--- or pathBlockHeight2, default 1.5 / 3.0 m above the origin) or "none" (block
--- nothing). Applied automatically when EXU loads and for every later spawn
--- and removal.
exu.pathing = {}

--- Recomputes the grid cells of every flagged object (idempotent).
--- ```lua
--- local count = exu.pathing.Refresh()
--- if not count then return end -- no path grid
--- ```
--- @return integer|nil count flagged objects re-applied; nil without a grid
function exu.pathing.Refresh() end

--- false puts flagged objects back on the stock bbox (re-applied at once).
--- ```lua
--- local count = exu.pathing.SetEnabled(false)
--- ```
--- @param enabled boolean
--- @return integer|nil count as Refresh
function exu.pathing.SetEnabled(enabled) end

--- ```lua
--- local enabled = exu.pathing.IsEnabled()
--- ```
--- @return boolean
function exu.pathing.IsEnabled() end

--- The effective mode of an object and its footprint.
--- ```lua
--- local mode, details = exu.pathing.GetMode(GetHandle("tower1"))
--- if mode then print(mode, details.cellsBlocked) end
--- ```
--- @nodiscard
--- @param h Handle
--- @return "box"|"faces"|"none"|nil mode
--- @return table|nil details { odf, configured, effective, cellsInBox, cellsBlocked, triangles, inGrid }
function exu.pathing.GetMode(h) end

--- ASCII picture of the path grid around a point or object.
--- ```lua
--- local picture = exu.pathing.DumpGrid(GetPosition(GetPlayerHandle()), 40)
--- if picture then print(picture) end
--- ```
--- @nodiscard
--- @param pos Vector|Handle
--- @param radius number? metres, default 40
--- @return string|nil
function exu.pathing.DumpGrid(pos, radius) end

--- ```lua
--- local caps = exu.pathing.GetCapabilities()
--- ```
--- @nodiscard
--- @return table { pathBlock, available, hookedSites, hookSites, gridReady, cellSize, enabled }
function exu.pathing.GetCapabilities() end

--- Creates a real-reference-owned General-group clone; prefix exu_surface_, cap 1024.
--- Restore only your own assignments before ReleaseSurfaceMaterial.
--- ```lua
--- local ok = exu.CloneSurfaceMaterial("my_material", "exu_surface_my_clone")
--- if not ok then print("clone failed") end
--- ```
--- @param source string
--- @param name string
--- @return boolean
function exu.CloneSurfaceMaterial(source, name) end

--- Removes an owned clone from the manager; scene references remain valid until detached.
--- ```lua
--- local released = exu.ReleaseSurfaceMaterial("exu_surface_my_clone")
--- ```
--- @param name string
--- @return boolean
function exu.ReleaseSurfaceMaterial(name) end

--- Assigns an owned surface clone. Does not retain the entity pointer.
--- ```lua
--- local ok = exu.SetSurfaceMaterial(GetPlayerHandle(), 0, "exu_surface_my_clone")
--- if not ok then print("SetSurfaceMaterial failed") end
--- ```
--- @param target Handle|ExuAnimationTarget
--- @param index integer zero-based
--- @param name string
--- @return boolean
function exu.SetSurfaceMaterial(target, index, name) end

--- Writes an ordinary finite float4 to matching fragment passes; skips absent/wrong types.
--- Negative technique/pass selects all supported passes. See Docs/SURFACE_MATERIAL_API.md.
--- ```lua
--- local changedPasses = exu.SetMaterialShaderVector("exu_surface_my_clone", "my_tint", 1, 0.5, 0.5, 1)
--- ```
--- @param material string
--- @param parameter string
--- @param x number
--- @param y number
--- @param z number
--- @param w number
--- @param technique integer? default -1
--- @param pass integer? default -1
--- @param group string? default General
--- @return integer changedPasses
function exu.SetMaterialShaderVector(material, parameter, x, y, z, w, technique, pass, group) end

--- Replaces an alias/unit texture across an owned clone's techniques, including Glow.
--- ```lua
--- local changedUnits = exu.SetSurfaceTexture("exu_surface_my_clone", "diffuse", "my_texture.dds")
--- ```
--- @param material string
--- @param alias string
--- @param texture string
--- @return integer changedUnits
function exu.SetSurfaceTexture(material, alias, texture) end

--- Sets the texture of every unit whose texture_alias or unit name matches alias (case-insensitive)
--- across all techniques of any named material. Native exu_surface_chunk_* names are refused.
--- ```lua
--- local changedUnits = exu.SetMaterialTextureAlias("my_material", "diffuse", "my_texture.dds")
--- ```
--- @param material string
--- @param alias string
--- @param texture string
--- @param group string? default General
--- @return integer changedUnits
function exu.SetMaterialTextureAlias(material, alias, texture, group) end

--- Swaps the fragment program (resetParams) of every pass with one, in techniques whose scheme equals
--- schemeName, on an owned exu_surface_* clone. Other materials and exu_surface_chunk_* are refused.
--- ```lua
--- local changedPasses = exu.SetSurfaceFragmentProgram("exu_surface_my_clone", "Glow", "my_fragment_program")
--- ```
--- @param material string
--- @param schemeName string
--- @param programName string
--- @return integer changedPasses
function exu.SetSurfaceFragmentProgram(material, schemeName, programName) end

--- Converts simulation world coordinates through the actual derived parent-node transform.
--- Rigid projection only; does not attach to an animated bone.
--- ```lua
--- local localPoint = exu.EntityLocalPoint(GetPlayerHandle(), GetPosition(GetHandle("tank1")))
--- if not localPoint then return end
--- ```
--- @param target Handle|ExuAnimationTarget
--- @param point Vector
--- @return Vector|nil
function exu.EntityLocalPoint(target, point) end

--- Intersects a simulation-space segment with current native exterior collision polygons.
--- Uses current per-part transforms and a bounded triangle BVH cache; no shots or damage.
--- Does not infer Ogre-only visual deformation or transparent texture holes.
--- Returns nil for a miss, dead/invalid handle, unavailable geometry, or unsupported build.
--- ```lua
--- local point, normal, distance = exu.RaycastObjectSurface(GetHandle("tank1"), GetPosition(GetPlayerHandle()), GetPosition(GetHandle("tank1")))
--- if not point then return end -- miss
--- ```
--- @param handle Handle
--- @param from Vector
--- @param to Vector
--- @return Vector? point
--- @return Vector? normal
--- @return number? distance
function exu.RaycastObjectSurface(handle, from, to) end

--- Current corrected native firing mount, or nil without a qualified/bound adapter.
--- ```lua
--- local muzzle = exu.GetWeaponMuzzlePosition(GetPlayerHandle(), 0)
--- if not muzzle then return end
--- ```
--- @param handle Handle
--- @param slot integer 0..4
--- @return Vector|nil
function exu.GetWeaponMuzzlePosition(handle, slot) end

--- World transform of a craft's weapon hardpoint node, for SetTransform.
--- Simulation pose from the native node tree, current at the call: valid off-screen,
--- in multiplayer and with OpenShim presentation off. This is the hardpoint itself,
--- before the weapon's muzzle offset and barrel aim, so it can sit a little behind
--- GetWeaponMuzzlePosition. nil for a dead/stale handle, a non-craft, an empty or
--- out-of-range slot, or an OpenShim without the export.
--- A node whose composed frame is scaled, mirrored or non-orthonormal (ValidPose tolerance 0.01) returns nil, the same as a missing node.
--- An out-of-range slot returns nil (unlike GetWeaponMuzzlePosition, which raises an argument error).
--- ```lua
--- local hardpoint = exu.GetHardpointTransform(GetPlayerHandle(), 0)
--- if not hardpoint then return end
--- ```
--- @param handle Handle
--- @param slot integer 0..4
--- @return Matrix|nil
function exu.GetHardpointTransform(handle, slot) end

--- World transform of a named model part (native node, the SDF/VDF part name,
--- at most 8 characters, case-insensitive) on an object, for SetTransform.
--- Simulation pose: the engine animates these nodes and the renderer copies them,
--- so it moves with walker legs/arms and is valid when the object is not rendered.
--- Not an Ogre skeleton bone: skinned-mesh-only bones are not found.
--- nil for a dead/stale handle, no part by that name, a name shared by two parts,
--- or an OpenShim without the export.
--- ```lua
--- local part = exu.GetBoneTransform(GetPlayerHandle(), "turret")
--- if not part then return end
--- ```
--- @param handle Handle
--- @param boneName string
--- @return Matrix|nil
function exu.GetBoneTransform(handle, boneName) end

--- Actual engine power-source state, class turret only; nil for unsupported layouts/classes.
--- ```lua
--- local powered = exu.IsGunTowerPowered(GetHandle("gtower1"))
--- if powered == nil then return end
--- ```
--- @param handle Handle
--- @return boolean|nil
function exu.IsGunTowerPowered(handle) end

--- Actual engine power state for any powered building: gun tower (turret), comm tower, shield tower.
--- nil for other classes, dead handles or unsupported builds. A shield tower only pushes while this is true.
--- ```lua
--- local powered = exu.IsPowered(GetHandle("gtower1"))
--- if powered == nil then return end
--- ```
--- @param handle Handle
--- @return boolean|nil
function exu.IsPowered(handle) end

--- Optional shared scorched debris finish, creation-time only; disabled on Lua-state teardown.
--- ```lua
--- exu.SetReactiveChunksEnabled(true)
--- ```
--- @param enabled boolean
function exu.SetReactiveChunksEnabled(enabled) end

--- Projects a simulation/Lua position, object origin or matrix translation.
--- Pixel coordinates use the current render viewport (including its left/top),
--- top-left origin, X right and Y down. Off-screen/behind points are not clamped.
--- Depth is signed camera-forward distance in world units; positive is forward.
--- Returns three nils when no live camera, invalid handle, nonfinite input or eye-plane projection.
--- Query after the camera has rendered a frame; this reads the current Ogre camera snapshot.
--- ```lua
--- local x, y, depth = exu.WorldToScreen(GetHandle("tank1"))
--- if not x then return end
--- ```
--- @param target Handle|Vector|Matrix|table
--- @return number? x
--- @return number? y
--- @return number? depth
function exu.WorldToScreen(target) end

--- True when the point (or handle origin/matrix translation) is in Ogre's camera frustum.
--- Includes near/far planes; does not test occlusion, terrain or line of sight.
--- Nil means an unavailable camera/build or invalid input/handle.
--- ```lua
--- local visible = exu.IsPointInView(GetHandle("tank1"))
--- if visible == nil then return end
--- ```
--- @param target Handle|Vector|Matrix|table
--- @return boolean? visible
function exu.IsPointInView(target) end

--- Enables local-authority production notifications. Returns false if targets fail qualification.
--- No Lua callbacks execute inside engine hooks. Disabling clears pending events.
--- Events are mission-local observations; remote replicas do not duplicate local builds.
--- ```lua
--- local available = exu.SetBuildEventsEnabled(true)
--- ```
--- @param enabled boolean
--- @return boolean available
function exu.SetBuildEventsEnabled(enabled) end

--- @class ExuBuildEvent
--- @field type 'queued'|'cancelled'|'completed'
--- @field producer Handle Producer handle, possibly dead by the time the queue is drained.
--- @field built Handle? Completed object handle; nil for armory launches.
--- @field team integer Team at event time.
--- @field odf string ODF basename without extension (up to eight characters).
--- @field producerType 'producer'|'constructor'|'armory'
--- @field sequence number Monotonic within the current enabled session.

--- Destructively drains at most 64 accepted-start/cancel/completion events (default 64).
--- 'queued' means resources were committed by native StartBuild, not a menu hover.
--- Capacity 512, oldest overwritten on overflow; counters reset on disable/mission close.
--- ```lua
--- local events, dropped, pending = exu.DrainBuildEvents(16)
--- for _, e in ipairs(events) do print(e.type, e.odf, e.team) end
--- ```
--- @param limit integer? Clamped to 1..64.
--- @return ExuBuildEvent[] events
--- @return number dropped Cumulative overwritten events.
--- @return integer pending
function exu.DrainBuildEvents(limit) end

--- Cancels the active build of a Producer (Recycler, Factory, Armory, ConstructionRig) and refunds it,
--- through the engine's own CancelBuild, so exu.DrainBuildEvents reports it as 'cancelled'.
--- False for a non-producer, an idle producer, or a remote replica in a net game (call it on the owning machine).
--- ```lua
--- if exu.CancelBuild(GetRecyclerHandle()) then print("refunded") end
--- ```
--- @param h Handle
--- @return boolean cancelled
function exu.CancelBuild(h) end

--- Completes the active build now, as if its timer had run out (engine FinishBuild, so it emits a
--- 'completed' build event). Only when the engine would itself finish it: deployed and busy.
--- An undeployed Recycler/Factory, or a ConstructionRig still driving to its site, returns nil.
--- Returns the new object, or nil (also nil for an Armory, whose launch has no handle).
--- Refuses remote replicas in a net game; call it on the owning machine.
--- ```lua
--- local built = exu.FinishBuild(GetFactoryHandle())
--- ```
--- @param h Handle
--- @return Handle? built
function exu.FinishBuild(h) end

--- Seconds left on a Producer's active build (0 once it is due), or nil when idle or not a producer.
--- The timer only counts down while the producer is deployed.
--- ```lua
--- local left = exu.GetBuildTimeLeft(GetRecyclerHandle())
--- ```
--- @nodiscard
--- @param h Handle
--- @return number? seconds
function exu.GetBuildTimeLeft(h) end

--- Sets the seconds left on a Producer's active build (0..1e6; 0 finishes it on the next deployed update).
--- False when idle, not a producer, or out of range. Local effect only: in a net game call it on the owner.
--- ```lua
--- exu.SetBuildTimeLeft(GetFactoryHandle(), 2.5)
--- ```
--- @param h Handle
--- @param seconds number
--- @return boolean ok
function exu.SetBuildTimeLeft(h, seconds) end

--- ODF basename (up to eight characters) a Producer is building, or nil when idle or not a producer.
--- ```lua
--- print(exu.GetBuildingOdf(GetRecyclerHandle()))
--- ```
--- @nodiscard
--- @param h Handle
--- @return string? odf
function exu.GetBuildingOdf(h) end

--- Scrap a Scavenger currently holds, or nil for a non-scavenger.
--- ```lua
--- local held = exu.GetCurScrapHold(GetHandle("scav1"))
--- ```
--- @nodiscard
--- @param h Handle
--- @return integer? scrap
function exu.GetCurScrapHold(h) end

--- Sets the scrap a Scavenger holds, clamped to [0, GetMaxScrapHold] and truncated to an integer.
--- False for a non-scavenger or NaN. Local effect only: in a net game call it on the owner.
--- ```lua
--- exu.SetCurScrapHold(GetHandle("scav1"), 10)
--- ```
--- @param h Handle
--- @param amount number
--- @return boolean ok
function exu.SetCurScrapHold(h, amount) end

--- A Scavenger's scrap capacity, or nil for a non-scavenger.
--- ```lua
--- local cap = exu.GetMaxScrapHold(GetHandle("scav1"))
--- ```
--- @nodiscard
--- @param h Handle
--- @return integer? capacity
function exu.GetMaxScrapHold(h) end

--- Sets a Scavenger's scrap capacity (1..1e6, truncated); the held amount is clamped to it.
--- Not saved: a loaded game restores the ODF capacity, so set it again after load.
--- Local effect only: in a net game call it on the owner.
--- ```lua
--- exu.SetMaxScrapHold(GetHandle("scav1"), 40)
--- ```
--- @param h Handle
--- @param capacity number
--- @return boolean ok
function exu.SetMaxScrapHold(h, capacity) end

--- ODF basename the unit's current Build command builds (Build, BuildAt, or SetCommand with
--- the build command and a string param), or nil for any other command. Pairs with GetCurrentCommand/GetCurrentWho.
--- ```lua
--- print(GetCurrentCommand(rig), exu.GetCurrentParam(rig))
--- ```
--- @nodiscard
--- @param h Handle
--- @return string? odf
function exu.GetCurrentParam(h) end

--- The 'when' value of the unit's current command (as given to SetCommand), or nil.
--- Commands that do not set it (stock Build, for example) leave an arbitrary value here.
--- ```lua
--- local when = exu.GetCurrentWhen(GetHandle("tank1"))
--- ```
--- @nodiscard
--- @param h Handle
--- @return number? when
function exu.GetCurrentWhen(h) end

--- The path of the unit's current command: its name (nil for an anonymous path, such as Goto or BuildAt
--- to a position) and its last point, with y from the terrain height. Nil when the command has no path or the height cannot be read.
--- ```lua
--- local name, dest = exu.GetCurrentWhere(GetHandle("tank1"))
--- ```
--- @nodiscard
--- @param h Handle
--- @return string? path
--- @return Vector? destination
function exu.GetCurrentWhere(h) end

--- Runs BZR's native rig placement validation, with its 10-unit X/Z clearance.
--- Adjusts height to the engine floor, checks terrain/collisions and allied power range.
--- Does not issue Build, spawn an object, check affordability/prerequisites, or reserve space.
--- ODF/model resources may load lazily. Positions/footprints outside the map are rejected.
--- Nil means unavailable, invalid ODF/input, or unsupported class. Only structures/turrets supported.
--- ```lua
--- local valid, adjustedPosition, reason = exu.IsTerrainBuildable(1, "abwpow", GetPosition(GetPlayerHandle()), SetVector(0, 0, 1))
--- if not valid then print(reason) end
--- ```
--- @param team integer 0..15
--- @param odf string ODF basename (up to 8 characters), optional .odf suffix.
--- @param proposedPosition Vector|table
--- @param facingDirection Vector|table Nonzero horizontal direction; Y ignored.
--- @return boolean? valid
--- @return Vector? adjustedPosition
--- @return string reason 'buildable', 'blocked', 'outside-map', 'power-required', 'invalid-odf', 'unsupported-class', 'unavailable'.
function exu.IsTerrainBuildable(team, odf, proposedPosition, facingDirection) end

--- Reads the live network player's Steam identity as a decimal string, preserving all 64 bits.
--- Uses a network ID from AddPlayer/Receive/GetMyNetID, not a team or object handle.
--- Nil for absent players, GOG identities, singleplayer, or unavailable native qualification.
--- ```lua
--- local steam64 = exu.GetPlayerSteam64(1)
--- if not steam64 then return end
--- ```
--- @param networkId integer 0..65535
--- @return string? steam64
function exu.GetPlayerSteam64(networkId) end

--- Registers an EXU command for explicit dispatch. BZR has no BZCC VarSys console.
--- Names are case-sensitive ASCII letters/digits/dots/underscores/hyphens, 1..96 bytes.
--- Names share a namespace with variables. Duplicate registration is refused.
--- Registry/callbacks belong to the current Lua state and are released with it.
--- ```lua
--- local ok, err = exu.RegisterConsoleCommand("mymod.hello", function(arguments) print("hello " .. arguments) end)
--- if not ok then print(err) end
--- ```
--- @param name string
--- @param callback fun(arguments:string)
--- @return boolean ok
--- @return string? error
function exu.RegisterConsoleCommand(name, callback) end

--- @class ExuConsoleVariableOptions
--- @field min number? Inclusive bound for numeric variables.
--- @field max number? Inclusive bound for numeric variables.
--- @field onChanged fun(value:any, previous:any)? Called after committing an actual change.

--- Registers a typed EXU variable. Default must satisfy its optional finite numeric bounds.
--- ```lua
--- local ok, err = exu.RegisterConsoleVariable("mymod.speed", 1, { min = 0, max = 10 })
--- if not ok then print(err) end
--- ```
--- @param name string
--- @param default number|string|boolean
--- @param options ExuConsoleVariableOptions?
--- @return boolean ok
--- @return string? error
function exu.RegisterConsoleVariable(name, default, options) end

--- Removes an EXU command or variable, returning whether it existed.
--- ```lua
--- local removed = exu.UnregisterConsoleItem("mymod.hello")
--- ```
--- @param name string
--- @return boolean removed
function exu.UnregisterConsoleItem(name) end

--- Explicitly invokes a registered command with one uninterpreted argument string.
--- Does not evaluate Lua, shell commands or stock game commands. Callback errors return false,error.
--- ```lua
--- local ok, err = exu.ExecuteConsoleCommand("mymod.hello", "world")
--- if not ok then print(err) end
--- ```
--- @param name string
--- @param arguments string? Defaults to the empty string.
--- @return boolean ok
--- @return string? error
function exu.ExecuteConsoleCommand(name, arguments) end

--- ```lua
--- local value = exu.GetConsoleVariable("mymod.speed")
--- if value == nil then return end
--- ```
--- @param name string
--- @return number|string|boolean|nil value Nil when no such variable exists.
function exu.GetConsoleVariable(name) end

--- Requires the registered type and bounds; bad values raise Lua argument errors.
--- Value is committed before notification; callback failure returns false,error and preserves the new value.
--- ```lua
--- local ok, err = exu.SetConsoleVariable("mymod.speed", 2)
--- if not ok then print(err) end
--- ```
--- @param name string
--- @param value number|string|boolean
--- @return boolean ok
--- @return string? error
function exu.SetConsoleVariable(name, value) end

--- Returns an independent name-to-kind snapshot of the current state's EXU registry.
--- ```lua
--- for name, kind in pairs(exu.ListConsoleItems()) do print(name, kind) end
--- ```
--- @return table<string, 'command'|'variable'> items
function exu.ListConsoleItems() end

--- Mission-scoped radar presentation: tilted projection, original backdrop hidden.
--- Native contacts, grid, visibility, scan and controls are retained.
--- Returns false on unsupported builds or a conflicting patch; restores on Lua close.
--- ```lua
--- local ok = exu.SetRadarPresentation(true, 30, 8)
--- if not ok then print("radar presentation unavailable") end
--- ```
---@param enabled boolean
---@param tiltDegrees? number 15..75; default 30
---@param perspectiveDistance? number 3..20; default 8
---@return boolean
function exu.SetRadarPresentation(enabled, tiltDegrees, perspectiveDistance) end

--- Current native cockpit radar layout in screen pixels; nil on unsupported builds.
--- ```lua
--- local layout = exu.GetRadarLayout()
--- if layout then print(layout.x, layout.y, layout.radius) end
--- ```
---@return table|nil layout {x,y,radius,visible}
function exu.GetRadarLayout() end

--- Adapts BZ2 terrain-cluster wireframe, pulse brightness, scan ring and white
--- height stems to the active native radar. Requires SetRadarPresentation first.
--- Disabling presentation also disables this drawing. Native contacts stay native.
--- ```lua
--- local ok = exu.SetRadarTerrain(true, 32, 0x0078FF)
--- if not ok then print("radar terrain unavailable") end
--- ```
---@param enabled boolean
---@param gridSpacing? number World metres, 8..256; BZ2 default 32.
---@param color? number 24-bit RGB; BZ2 default 0x0078FF.
---@return boolean enabled Whether the complete requested hook group activated.
function exu.SetRadarTerrain(enabled, gridSpacing, color) end

--- Draws a world-aligned textured radar mesh under the terrain wireframe, through
--- the native HUD batch, and hides the stock heading compass while it draws.
--- Requires SetRadarTerrain first; disabling or re-enabling terrain clears the mesh.
--- If the mesh cannot draw, the native compass comes back.
--- ```lua
--- local ok = exu.SetRadarMesh(true, "my_radar_material", { -1, 0, -1, 0, 0,  1, 0, -1, 1, 0,  0, 0, 1, 0.5, 1 }) -- one triangle: x, y, z, u, v per vertex
--- if not ok then print("radar mesh unavailable") end
--- ```
---@param enabled boolean
---@param material? string Ogre material; required when enabled. Must not depend on scene lighting.
---@param vertices? number[] Triangle list as flat x,y,z,u,v per vertex. Positions are in radar ranges (1 = the scanner edge), each within -1.7..1.7; +X east, +Y up, +Z north. At most 4096 triangles.
---@return boolean enabled Whether the mesh and the compass hook activated.
function exu.SetRadarMesh(enabled, material, vertices) end

--- Whether the last radar frame drew the SetRadarMesh mesh, with the native compass
--- hidden. False before the first drawn frame, on a frame the mesh could not draw,
--- and after a fault clears the mesh, which SetRadarMesh cannot report.
--- ```lua
--- local drawing = exu.IsRadarMeshDrawing()
--- ```
---@return boolean drawing
function exu.IsRadarMeshDrawing() end

return exu
