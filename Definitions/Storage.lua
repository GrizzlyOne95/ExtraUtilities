--- @meta exu
--- Persistent storage API definitions for Extra Utilities.
--- This file augments Definitions/ExtraUtils.lua; it is editor metadata only.

--- Values accepted by exu.storage are nil, boolean, finite number, string, and
--- recursively nested tables whose keys are strings or finite numbers.
--- Userdata/handles, functions, threads, lightuserdata, and cyclic tables are
--- intentionally rejected because they cannot be safely reconstructed later.

--- @class ExuStorageLoadMeta
--- @field exists boolean False only when neither a primary nor backup store exists.
--- @field source "primary"|"backup"|"new"|"error"
--- @field recovered boolean True when Load recovered from the one-generation backup.
--- @field formatVersion integer
--- @field schemaVersion integer Application-controlled schema version passed to Save.
--- @field sizeBytes number
--- @field primaryError string? Primary-file error when backup recovery was required.

--- @class ExuStorageInfo
--- @field primaryExists boolean
--- @field backupExists boolean
--- @field temporaryExists boolean
--- @field primaryBytes number
--- @field backupBytes number
--- @field formatVersion integer

--- @class ExuStorageCapabilities
--- @field formatVersion integer
--- @field maxFileBytes number
--- @field maxStringBytes number
--- @field maxDepth integer
--- @field tables boolean
--- @field atomicReplace boolean
--- @field backupRecovery boolean
--- @field checksum boolean
--- @field nativeSaveParsing boolean False in the portable v1 storage layer.

--- @class ExuStorageApi
local storage = {}

--- Atomically persists a supported Lua value in a sandboxed namespace.
--- A successful replacement retains the prior generation as a backup.
--- @param namespace string 1-64 characters; starts alphanumeric and contains only A-Z, a-z, 0-9, '.', '_', '-'.
--- @param value any
--- @param schemaVersion integer? Application schema version; defaults to 1.
--- @return boolean success
--- @return string? error
function storage.Save(namespace, value, schemaVersion) end

--- Loads a namespace, automatically falling back to the backup when the primary
--- fails format/checksum validation. A brand-new namespace returns an empty table
--- with meta.exists=false. Corrupt primary+backup data returns nil and error metadata.
--- @param namespace string
--- @return any|nil value
--- @return ExuStorageLoadMeta meta
function storage.Load(namespace) end

--- Returns true when the primary or backup generation exists.
--- @param namespace string
--- @return boolean
function storage.Exists(namespace) end

--- Deletes the primary, backup, and temporary files for a namespace.
--- @param namespace string
--- @return boolean success
--- @return string? error
function storage.Delete(namespace) end

--- Returns storage-file metadata without exposing or accepting arbitrary paths.
--- @param namespace string
--- @return ExuStorageInfo|nil info
--- @return string? error
function storage.GetInfo(namespace) end

--- Returns implementation and safety-limit capabilities.
--- @return ExuStorageCapabilities
function storage.GetCapabilities() end

--- @class exu
--- @field storage ExuStorageApi
