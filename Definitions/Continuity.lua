--- @meta exu
--- Mission-to-mission continuity API definitions for Extra Utilities.
--- Snapshots are ordinary Lua tables and can be persisted with exu.storage.

--- @class ExuContinuityMatrix
--- @field right_x number
--- @field right_y number
--- @field right_z number
--- @field up_x number
--- @field up_y number
--- @field up_z number
--- @field front_x number
--- @field front_y number
--- @field front_z number
--- @field posit_x number
--- @field posit_y number
--- @field posit_z number

--- @class ExuContinuityObject
--- @field odf string
--- @field team integer
--- @field transform ExuContinuityMatrix
--- @field health number? Health ratio captured from BZR GetHealth.
--- @field ammo number? Ammo ratio captured from BZR GetAmmo.
--- @field classLabel string?
--- @field classSig string?
--- @field isPlayer boolean Player identity derived from documented GetPlayerHandle calls.
--- @field isPerson boolean?

--- @class ExuContinuityCaptureOptions
--- @field team integer? Capture only objects on this team.
--- @field includePlayers boolean? Defaults to false.
--- @field maxObjects integer? Defaults to 4096; hard cap 10000.

--- @class ExuContinuityCaptureReport
--- @field scanned integer
--- @field captured integer
--- @field skipped integer
--- @field truncated boolean

--- @class ExuContinuitySnapshot
--- @field formatVersion integer
--- @field source string
--- @field enumerator string? Runtime object enumerator used by CaptureWorld; currently AllObjects.
--- @field durationSeconds number? Mission time reported by BZR GetTime at capture.
--- @field team integer? Team filter used for the capture.
--- @field objectCount integer
--- @field objects ExuContinuityObject[]
--- @field captureReport ExuContinuityCaptureReport

--- @class ExuContinuityOffset
--- @field x number
--- @field y number
--- @field z number

--- @class ExuContinuityRestoreOptions
--- @field offset ExuContinuityOffset? Translation added to every saved object position.
--- @field odfMap table<string,string>? Optional old-ODF to new-ODF substitutions.
--- @field teamOverride integer? Build every restored object on this team instead of its saved team.
--- @field restoreHealth boolean? Defaults to true.
--- @field restoreAmmo boolean? Defaults to true.
--- @field skipPlayers boolean? Defaults to true to avoid duplicating a live player object.
--- @field maxObjects integer? Defaults to 4096; hard cap 10000.

--- @class ExuContinuityRestoreReport
--- @field scanned integer
--- @field restored integer
--- @field skipped integer
--- @field truncated boolean
--- @field error string?

--- @class ExuContinuityCapabilities
--- @field snapshotFormatVersion integer
--- @field captureObject boolean
--- @field captureWorld boolean
--- @field restoreObjects boolean
--- @field translationOffset boolean
--- @field odfRemap boolean
--- @field healthRatio boolean
--- @field ammoRatio boolean
--- @field multiplayerHostGuard boolean
--- @field nativeSaveParsing boolean

--- @class ExuContinuityApi
local continuity = {}

--- Captures one live GameObject into a pointer-free, persistable descriptor using
--- documented Battlezone 98 Redux Lua calls.
--- @param h Handle
--- @return ExuContinuityObject|nil object
--- @return string? error
function continuity.CaptureObject(h) end

--- Captures an explicit table of handles. This is the safest option for mods that
--- already track which base units/structures should carry into the sequel.
--- @param handles Handle[]|table<any,Handle>
--- @param options ExuContinuityCaptureOptions?
--- @return ExuContinuitySnapshot snapshot
function continuity.CaptureObjects(handles, options) end

--- Enumerates the map with BZR's documented AllObjects() iterator and captures
--- reconstruction-safe state. Use the team filter for a player/base carry-over
--- rather than blindly restoring every map object.
--- @param options ExuContinuityCaptureOptions?
--- @return ExuContinuitySnapshot|nil snapshot
--- @return string? error
function continuity.CaptureWorld(options) end

--- Rebuilds objects from a continuity snapshot with BZR BuildObject(odf, team,
--- matrix). In multiplayer, restoration fails closed unless IsNetGame/IsHosting
--- verify that the current machine has host authority.
--- @param snapshot ExuContinuitySnapshot
--- @param options ExuContinuityRestoreOptions?
--- @return Handle[]|nil handles
--- @return ExuContinuityRestoreReport report
function continuity.Restore(snapshot, options) end

--- @return ExuContinuityCapabilities
function continuity.GetCapabilities() end

--- @class exu
--- @field continuity ExuContinuityApi