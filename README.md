# Extra Utilities

Extra Utilities is the only public script extender for Battlezone 98 Redux 2.2.301. Support open modding!

## Compatibility

| Platform | Supported |
|---|---|
| Windows | ✅ Yes |
| Steam | ✅ Yes |
| Mac | ❌ No |
| Linux | ❌ No |
| GOG | ❌ No |
| Battlezone 1.5 | ❌ No |

Forked from VTRider's initial project. Massive kudos to him for figuring out how to implement EXU in the first place!

---

## Using EXU in a Lua mission

### The short version

Download `exu.dll` from the [latest release](../../releases/latest) and place it in your mission's
folder (or anywhere on BZR's DLL search path). Then load it from your mission script:

```lua
local exu = require("exu")
```

That's it. After this line, all EXU functions are available under the `exu` table.

### Minimal working example

```lua
-- HelloEXU.lua — minimal mission demonstrating EXU setup

local exu = require("exu")

-- Print the loaded EXU version to the game console
print("EXU version: " .. exu.VERSION)

function Start()
    -- Example: disable global turbo (patch-driven behaviour change)
    exu.SetGlobalTurbo(false)

    -- Example: read the current camera view enum
    local view = exu.GetCameraView()
    print("Camera view on start: " .. tostring(view))
end

function Update(dtime)
    -- Example: read satellite cursor position if sat is active
    if exu.GetSatState() == exu.SATELLITE.ENABLED then
        local pos = exu.GetSatCursorPos()
        -- pos.x, pos.y, pos.z are the world-space coordinates
    end
end
```

### What `Definitions/ExtraUtils.lua` is for

`Definitions/ExtraUtils.lua` is a **type-annotation-only** file for editor tooling (e.g. the Lua
Language Server / VS Code extension). It contains `@class`, `@field`, and `@param`/`@return`
annotations that give your editor autocomplete and type-checking for EXU's API. It is **not**
required at runtime — it raises an error if `require()`d directly. Copy it into your project's
definitions path and point your editor at it.

### Vendoring policy

**Anti-Vendoring Stance:** Mod authors should depend on the EXU Steam Workshop item rather than
shipping a copy of `exu.dll` with their mod. Vendoring a fixed DLL version prevents your users
from automatically receiving bugfixes, and creates compatibility problems when multiple mods
load different copies (the first one to `require` wins, potentially breaking others that
needed a newer version). The Workshop item is updated alongside releases; pinning to it
is the only way to ensure compatibility across the ecosystem.

---

## Building from source

### Prerequisites

- Visual Studio 2022 (any edition) with the **Desktop development with C++** workload
- The **v143 toolset** (installed by default with VS2022)

### First-time setup

Run the included setup script once after cloning:

```powershell
.\setup-dev.ps1
```

This performs a sparse checkout of the Ogre 1.10.0 headers into
`third_party\ogre-1.10.0-bzr\_work`. No other dependencies need manual setup —
all libraries (Lua 5.1, OgreMain, OgreOverlay) are already checked in under `lib\`.

After the script completes, open `ExtraUtilities.sln` and build the **Release|x86**
solution configuration. It maps to the project-level `Release|Win32` target and
writes the output DLL to `Release\exu.dll`.

From a Visual Studio developer prompt, the same build is:

```powershell
msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86
```

### Using EXU as a C++ dependency (DLL consumers)

If you are writing a native DLL mission (e.g. `Battlezone98Redux_Shim`) and want to call
into EXU from C++:

1. Add `include/` from this repo to your project's include paths.
2. Link against `Release/exu.lib` (the import library produced by the EXU build).
3. Include `<ExtraUtils.h>`. Do **not** define `EXTRAUTILITIES_EXPORTS` in your project.

```cpp
#include <cstring>
#include <Windows.h>
#include <ExtraUtils.h>

void OnMissionInit()
{
    // Check for a compatible EXU version at runtime
    if (strcmp(EXU_GetVersion(), EXU_VERSION_EXPECTED) != 0) {
        OutputDebugStringA("exu.dll version mismatch — update your EXU installation");
        return;
    }

    // Get the Lua state EXU registered during luaopen_exu
    lua_State* L = EXU_GetLuaState();
    if (!L) return; // EXU not yet initialized from Lua
    // ... use the Lua C API
}
```

---

## Maintenance: what to do when BZR updates

BZR ships infrequently, but when it does, the hardcoded addresses in `src/bzr.h` need to
be re-verified. This section documents the process.

### 1. Check whether addresses are still valid

Open `exu.json` at the repo root. Every address EXU relies on is documented there with
a description and the BZR version it was verified against. Load the new EXE + PDB into
your reverse-engineering tool (Ghidra, IDA, x64dbg, or the Battlezone98Redux_Shim RE
toolchain at `../Battlezone98Redux_Shim`) and verify each address in the JSON.

**Pattern Scans:** Look for the functions and data symbols by their PDB names first — the
PDB is the fastest way to confirm addresses haven't moved. If the PDB is unavailable, use
the `pattern` field in `exu.json` to locate each function via pattern scan. Pattern scans
are more robust than hardcoded addresses as they often survive binary relocation and minor
code changes.

### 2. Update bzr.h

For any address that has changed, update the corresponding constant in `src/bzr.h` and
update the `address` field and `version` comment in `exu.json` to reflect the new BZR version.

### 3. Update the target version comment

At the top of `src/bzr.h`:
```cpp
/*
* Structs and memory offsets for BZR 2.2.301   ← bump this
*/
```

Also update `"version"` in `exu.json`.

### 4. Build and test

Build the Release configuration and drop `exu.dll` into a BZR installation. Run
`examples/RequireTutorial.lua` as a mission script — if it loads and prints the EXU
version without crashing, the basic address set is intact.

### 5. Tag and release

Once the build is verified, bump `version` in `src/About.h` (e.g. `"1.1.1"`) and push a
`v1.1.1` tag. The CI workflow will build and publish a GitHub Release automatically.

---

## High-Level Summary

EXU is the Steam-side native script extender layer for addon code. Its current
work falls into four main buckets:

- Native gameplay/state access: command replacement hooks, selected-weapon mask
  inspection, AI process/task inspection and limited task writes, pause-menu
  state probes, native save triggering, multiplayer object-build helpers, life
  and scoreboard helpers, and custom kill-message support.
- Rendering and UI controls: fog, gravity, ambient and sun parameters,
  time-of-day and shadow controls, viewport retro-lighting material-scheme
  switching, Ogre overlay creation and manipulation, and radar state/size and
  edge-path refresh helpers.
- Patch-driven behavior changes: engine flame color overrides, turbo toggles,
  ordnance velocity inheritance, hovercraft shot convergence, smart-reticle
  convergence for the local player, and unit-VO throttling, muting, and bark
  rotation control.
- Stability and integration hardening: standalone EXU builds, stricter patch and
  scanner validation, safer Ogre/material/environment calls, and better logging
  when viewport or render-system state is missing instead of crashing Lua-side
  callers.

Recent additions:
- Viewport retro-lighting toggles via `exu.GetRetroLightingMode()` and `exu.SetRetroLightingMode(enabled)`, which switch the active viewport between the stock modern material schemes and the `og-*` retro variants.
- Local smart-reticle hovercraft shot convergence via `exu.GetPlayerReticleShotConvergence()` and `exu.SetPlayerReticleShotConvergence(enabled)`.
- Hardened environment/material-scheme access so bad viewport state is logged instead of crashing the caller when Ogre data is unavailable.
- Terrain material convenience helpers via `exu.GetTerrainMaterialName([trnFilename])` and `exu.SetTerrainTextureSet(textureSet)` for live planet-style terrain re-theming without reloading HG2/TRN.
- OpenShim music bridge helpers: `exu.SetMusicTrack(index)`, `exu.GetMusicTrack()`, `exu.StopMusic()`, `exu.PauseMusic()`, and `exu.ResumeMusic()`. These resolve optional OpenShim `winmm.dll` exports at runtime and fail closed when OpenShim or a specific native music control is unavailable.
- Co-op mission sync helper example in `examples/openshim_coop_sync.lua`, wrapping stock Lua `Send`/`Receive` for host-authoritative objective text, markers, mission state, and win/loss synchronization.

## Terrain Planet Swap

EXU now has a terrain-specific material helper for live terrain re-theming. The
intended workflow is:

- Keep the current map's `HG2` and `TRN` static.
- Resolve the terrain atlas material from the map's TRN `[Atlases] MaterialName`.
- Swap one or more live terrain texture units on that material.
- Pair that with the existing EXU environment calls such as `SetFog`,
  `SetAmbientLight`, `SetSunDiffuse`, `SetSunSpecular`, `SetSunDirection`,
  `SetTimeOfDay`, and `SetSunShadowFarDistance`.

`exu.SetTerrainTextureSet` accepts a table with terrain texture keys:
`diffuse`, `detail`, `normal`, `specular`, and `emissive`. Each field is
optional, so you can replace only the slots you need.

```lua
local exu = require("exu")

local ok, materialName = exu.SetTerrainTextureSet({
    diffuse = "trn_ice_diffuse.dds",
    detail = "trn_ice_detail.dds",
    normal = "trn_ice_normal.dds",
    specular = "trn_ice_specular.dds",
    emissive = "black.dds",
})

if ok then
    exu.SetFog(0.66, 0.76, 0.88, 120, 1200)
    exu.SetAmbientLight(0.55, 0.60, 0.70, 1.0)
    exu.SetSunDiffuse(0.80, 0.87, 1.00)
    exu.SetSunSpecular(0.60, 0.70, 0.90)
    exu.SetSunDirection(-0.30, -0.95, 0.08)
    exu.SetTimeOfDay(930, true)
    exu.SetSunShadowFarDistance(850)
end
```

Notes:

- By default EXU resolves the current map's terrain material automatically from
  `GetMapTRNFilename()` and the TRN's `[Atlases] MaterialName` entry.
- You can override the target material by passing `material = "my_material"` in
  the texture-set table.
- This preserves the map's existing terrain tile painting. It does not hot
  reload the map's `TRN` or `HG2`.
- A complete usage example lives in `examples/TerrainPlanetSwap.lua`.

## Credits

- `VTrider` for the original EXU implementation and a large share of the native script-extender codebase.
- `GrizzlyOne95` for ongoing maintenance, loader and integration work, and newer rendering and stability additions.
- `Janne` for the original Lua DLL project that paved the way for EXU.
- `DivisionByZero` for the DLL loader work that later integrations build on.
- `Business Lawyer` for bug hunting and technical collaboration.
