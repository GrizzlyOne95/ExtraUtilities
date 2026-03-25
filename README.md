# Extra Utilities

Extra Utilities is the only public script extender for Battlezone 98 Redux 2.2.301. Support open modding!

Usage:
Build the DLL or download the latest release from the releases tab. Then simply require() exu.dll from lua and refer to the function documentation on the wiki page.

Compatibility:
- Windows: Yes
- Mac: No
- Linux: No
- Steam: Yes
- GOG: No
- Battlezone 1.5: of course not lol

Forked from VTRider's initial project. Massive kudos to him for figuring out how to implement EXU in the first place!

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
