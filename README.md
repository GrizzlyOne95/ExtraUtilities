# Extra Utilities

Extra Utilities (EXU) is a native Lua extension for Battlezone 98 Redux. It adds engine, renderer, UI, object, multiplayer, and gameplay controls that are not exposed by the stock mission API.

EXU currently targets the 32-bit Windows build of Battlezone 98 Redux 2.2.301. It does not support macOS, Linux, or Battlezone 1.5. Native addresses and hooks are build-specific and must be revalidated when the game updates.

## Features

- **Camera and display** — camera modes, matrices, origins, field of view, zoom limits, clip distances, aspect ratio, projection mode, polygon mode, game resolution, UI scaling, and fullscreen state.
- **Environment and lighting** — fog, gravity, ambient and sun lighting, time of day, shadow distance, skybox/skydome/skyplane controls, visibility masks, retro-lighting schemes, and viewport shadow or overlay toggles.
- **Particles and debug rendering** — create and control Ogre particle systems, draw lines and boxes, toggle wireframe, show bounding boxes, and clear temporary visuals.
- **Materials, terrain, and entities** — inspect and replace entity or sub-entity materials, clone materials, modify textures and pass colors, control visibility and render queues, manipulate lights and animations, and re-theme the live terrain material without reloading the map.
- **Overlays and HUD layout** — create Ogre overlays and text elements, position and style them, move or recolor stock scrap and pilot readouts, manipulate HUD sprite rectangles, and inspect the command-menu bounds.
- **Radar, reticle, and satellite** — radar mode and scale, edge-path layout, reticle position/range/object data, satellite positions, pan speed, zoom, and state.
- **Game objects and AI** — object pointers and handles, mass, radar and jamming values, weapon selection masks, construction-rig selection, AI process/task inspection, selected task-state writes, and Lua replacement of the selected-unit Hunt command.
- **Gameplay hooks** — global and per-unit turbo, shot convergence, ordnance velocity inheritance, engine-flame colors, silent scrap changes, infinite ammo/scrap controls, unit-VO behavior, AI targeting and tuning, turret pitch, attack reveal, and mission-scoped hook resets.
- **Ordnance and physics** — build ordnance, inspect ordnance attributes, adjust the ballistic coefficient, and use matrix/vector helpers including screen-to-world conversion.
- **Multiplayer** — synchronized or asynchronous object creation, lives, scoreboard visibility, network player ID, custom kill messages, and starting-recycler control.
- **Input, preferences, and system utilities** — game-key state, pause-menu detection, play and sound settings, native save requests, screen resolution, Steam ID, and diagnostic message boxes.
- **OpenShim integration** — optional runtime bridges for shared turbo, HUD, convergence, reticle-range, and music ownership. EXU retains standalone fallbacks where supported and fails closed when an optional bridge is unavailable.
- **Native consumers** — a small exported C API for version checks, access to the registered Lua state, and selected integration callbacks.

Detailed Lua API descriptions and editor annotations are kept in [`Definitions/ExtraUtils.lua`](Definitions/ExtraUtils.lua). That file is for editor tooling only and must not be loaded at runtime; the runtime export table in `src/luaexport.cpp` remains the definitive list of registered functions.

## Using EXU

Install `exu.dll` through the EXU Steam Workshop item or download it from the [latest release](../../releases/latest), then load it from a mission script:

```lua
local exu = require("exu")
```

Depending on the shared Workshop installation is preferred to bundling a private DLL copy with each mod. A shared installation receives fixes and avoids conflicts when multiple mods expect different EXU versions.

See [`examples/`](examples) for focused demonstrations. C++ consumers can include [`include/ExtraUtils.h`](include/ExtraUtils.h) and link against the import library produced by the build.

## Building

Requirements:

- Visual Studio 2022 with **Desktop development with C++**
- MSVC v143 14.43 or newer
- PowerShell for dependency setup

Run `setup-dev.ps1` once after cloning to fetch the required Ogre 1.10 headers. Then build `ExtraUtilities.sln` as **Release|x86**; the project-level target is **Release|Win32** and writes `Release/exu.dll`.

```powershell
.\setup-dev.ps1
msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86
```

Lua 5.1, OgreMain, and OgreOverlay build dependencies are included in the repository. If several MSVC toolsets are installed, pass `/p:VCToolsVersion=<version>` to select a recent one explicitly.

## Updating for a game patch

- Revalidate the addresses and signatures documented in `exu.json` against the new executable.
- Update the corresponding declarations in `src/bzr.h` and record the verified game version.
- Build **Release|x86** and smoke-test Lua loading plus the affected feature groups in game.
- Update the EXU version in `src/About.h` before tagging a release.

## Credits

- **VTrider** — original EXU implementation and much of the native script-extender foundation.
- **GrizzlyOne95** — ongoing maintenance, integrations, rendering features, and stability work.
- **Janne** — original Lua DLL project that helped establish the approach.
- **DivisionByZero** — DLL loader work used by later integrations.
- **Business Lawyer** — bug hunting and technical collaboration.

See [`COPYING`](COPYING) and [`COPYING.LESSER`](COPYING.LESSER) for license terms.
