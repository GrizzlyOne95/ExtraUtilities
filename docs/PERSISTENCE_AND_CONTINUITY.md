# EXU Persistent Storage and Mission Continuity

EXU now provides two complementary APIs:

- `exu.storage`: sandboxed, typed persistence for careers, statistics, settings, unlocks, mission results, and other mod-owned state.
- `exu.continuity`: portable world snapshots that can carry selected Battlezone objects from one mission into another.

The systems are deliberately separate. `exu.storage` knows nothing about Battlezone object pointers; `exu.continuity` produces ordinary Lua tables that can be saved by `exu.storage` or manipulated by the mission before saving.

## Persistent profiles and statistics

```lua
local profile, meta = exu.storage.Load("campaign_reimagined")
if not profile then
    error("Could not load career data: " .. tostring(meta.primaryError))
end

profile.schema = profile.schema or 1
profile.stats = profile.stats or {
    missions = 0,
    kills = 0,
    deaths = 0,
    playSeconds = 0,
}
profile.unlocks = profile.unlocks or {}

-- Update these from normal mission callbacks/events.
profile.stats.kills = profile.stats.kills + 1
profile.unlocks.advancedTank = true

local ok, err = exu.storage.Save("campaign_reimagined", profile, 1)
if not ok then
    print("Career save failed: " .. tostring(err))
end
```

The store accepts nil, booleans, finite numbers, strings, and nested tables. Table keys may be strings or finite numbers. It rejects handles/userdata, functions, threads, lightuserdata, cyclic tables, non-finite numbers, excessive nesting, and oversized files rather than serializing values that cannot be safely reconstructed in a later process.

### Duration

Mission time can be accumulated explicitly:

```lua
local missionStart = GetTime()

local function CommitMissionDuration(profile)
    profile.stats = profile.stats or {}
    profile.stats.playSeconds = (profile.stats.playSeconds or 0)
        + math.max(0, GetTime() - missionStart)
end
```

A continuity snapshot also records `durationSeconds = GetTime()` at capture time when the stock function is available.

## Storage safety

Namespaces are file names, not paths. They must start with an alphanumeric character and may contain only letters, digits, `.`, `_`, and `-`. Mods cannot use this API to write arbitrary paths.

Storage lives beneath:

```text
%LOCALAPPDATA%\Battlezone 98 Redux\ExtraUtilities\Storage
```

Each namespace uses:

```text
campaign_reimagined.exudata
campaign_reimagined.exudata.bak
campaign_reimagined.exudata.tmp
```

The on-disk format contains:

- an EXU format magic/version;
- an application-controlled schema version;
- an explicit payload length;
- a CRC32 payload checksum;
- a non-executable typed value stream.

Writes go to `.tmp`, are flushed, preserve the current primary as `.bak`, and then atomically replace the primary. `Load` validates the format/length/checksum and automatically tries the backup when the primary is damaged.

A missing namespace returns an empty table plus metadata with `exists=false`. A corrupt primary and corrupt/missing backup returns `nil` rather than silently replacing progression with an empty career.

## Carrying a base into a sequel mission

### End of mission A

Capture the player's team when the campaign reaches a committed ending state:

```lua
local snapshot, err = exu.continuity.CaptureWorld({
    team = 1,
    includePlayers = false,
    maxObjects = 4096,
})

if not snapshot then
    error(err)
end

local profile = exu.storage.Load("my_campaign")
profile.mission01 = profile.mission01 or {}
profile.mission01.endingWorld = snapshot
profile.mission01.completed = true

local ok, saveErr = exu.storage.Save("my_campaign", profile, 2)
assert(ok, saveErr)
```

`CaptureWorld` obtains the live map object table through `GetAllGameObjectHandles()` (with a table-returning `AllObjects` alias accepted when present). It then describes objects through stock Lua getters rather than reading BZR object-list memory.

Each captured object currently contains reconstruction-safe state:

```lua
{
    odf = "avfact.odf",
    team = 1,
    transform = {
        right_x = ..., right_y = ..., right_z = ...,
        up_x = ...,    up_y = ...,    up_z = ...,
        front_x = ..., front_y = ..., front_z = ...,
        posit_x = ..., posit_y = ..., posit_z = ...,
    },
    health = 0.73,
    ammo = 0.50,
    classLabel = "factory",
    classSig = "...",
    isPlayer = false,
    isPerson = false,
}
```

`health` and `ammo` are ratios. Restore applies the ratio to the newly built object's current maximum, so a deliberate ODF substitution does not blindly copy the previous ODF's absolute hit-point/ammo values.

### Start of mission B

If mission B shares compatible world coordinates, restore directly:

```lua
local profile, meta = exu.storage.Load("my_campaign")
local oldWorld = profile.mission01 and profile.mission01.endingWorld

if oldWorld then
    local handles, report = exu.continuity.Restore(oldWorld)
    print("Restored objects:", report.restored, "skipped:", report.skipped)
end
```

If the sequel map uses a different base area, translate the entire saved layout:

```lua
local handles, report = exu.continuity.Restore(oldWorld, {
    offset = { x = 1800, y = 0, z = -650 },
})
```

This preserves the relative base layout and orientations while moving it to another part of the new map.

ODFs can also be remapped during a sequel/tech transition:

```lua
local handles, report = exu.continuity.Restore(oldWorld, {
    offset = { x = 1800, y = 0, z = -650 },
    odfMap = {
        avfact = "cvfact",
        avrecy = "cvrecy",
    },
})
```

Use the exact ODF string stored by the snapshot as the mapping key.

## Explicit carry lists

A full team snapshot is convenient, but many campaigns should decide exactly what survives. `CaptureObjects` accepts a table of handles so normal Lua can implement the campaign policy:

```lua
local carry = {}

for _, h in pairs(GetAllGameObjectHandles()) do
    if GetTeamNum(h) == 1 and not IsPlayer(h) then
        local classLabel = GetClassLabel(h)

        -- Example policy: structures and utility units carry forward,
        -- disposable combat units do not.
        if classLabel == "recycler"
            or classLabel == "factory"
            or classLabel == "armory"
            or classLabel == "turrettank"
            or classLabel == "constructionrig" then
            carry[#carry + 1] = h
        end
    end
end

local snapshot = exu.continuity.CaptureObjects(carry)
```

This is also the recommended way to encode story consequences: the campaign decides what constitutes persistent infrastructure instead of EXU guessing from undocumented engine internals.

## Multiplayer

Capture is read-only. Restore creates GameObjects and therefore has network authority implications. In a network game, `exu.continuity.Restore` refuses to proceed unless the runtime can verify that the caller is the host (`ImServer` / `IsHosting`).

The mission should restore once from host-owned mission logic, not independently on every client.

## Deliberately not persisted in snapshot format 1

Format 1 does not claim to reproduce:

- AI command/task stacks;
- target handles;
- group/formation internals;
- pointer identity or sequence handles;
- current weapon firing state;
- particle/audio state;
- renderer pointers;
- physics solver internals;
- arbitrary script userdata;
- the local player object by default.

Those are either transient, pointer-based, multiplayer-sensitive, or not yet backed by a sufficiently stable public/runtime API.

A sequel mission can restore gameplay intent after reconstruction—for example, assign restored units to groups, issue Defend/Goto/Patrol commands, or relabel key structures.

## Native `.sav` pipeline: future extension

EXU already has a `SaveGame` bridge to Battlezone's native serializer. Native-save parsing is intentionally **not** part of continuity format 1.

The continuity schema is source-neutral on purpose. A future qualified `.sav` reader can parse recoverable object records into the same `ExuContinuitySnapshot` structure, after which existing filtering, persistence, offset/remap, and restore logic can be reused. That avoids making career data permanently dependent on one reverse-engineered save-file layout.
