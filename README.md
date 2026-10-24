# Extra Utilities

Extra Utilities (EXU) is a native Lua extension for Battlezone 98 Redux. It adds engine, renderer, UI, object, multiplayer, and gameplay controls that are not exposed by the stock mission API.

EXU targets the 32-bit Windows build of Battlezone 98 Redux 2.2.301, including that same Win32 `exu.dll` under Proton/Wine. It does not support a native Linux or macOS game binary, or Battlezone 1.5. Native addresses and hooks are build-specific and must be revalidated when the game updates.

The compatibility policy covers Windows/GOG, Windows/Steam, Linux/Steam via Proton, and Linux/GOG through a compatible Wine/Proton prefix. Each affected runtime needs its own validation; see the shared [platform compatibility policy](Docs/BZR_PLATFORM_COMPATIBILITY.md).

**Source vs. release:** The features below describe the current `main` branch, which can be ahead of the [latest published DLL](../../releases/latest) or Workshop upload. Check the runtime's `exu.VERSION` against [`src/About.h`](src/About.h) before relying on newly added APIs.

## Features

- **Camera and rendering** — camera transforms, FOV, zoom and clipping, projection and screen/world conversion, display settings, fog, sky, lighting, shadows, culling, and render profiles.
- **Particles, materials, and terrain** — Ogre particle systems and live emitter/affector control, entity/material/texture changes, terrain re-theming, point lights, and static geometry.
- **Overlays and HUD** — custom text and overlays, stock HUD meter and sprite layout, radar, reticle, command-menu, and satellite controls.
- **Animation and first-person systems** — high-level `exu.animation` and `exu.fps` APIs for entity/cockpit/pilot animations, first-person layers and particles, plus opt-in player and vehicle control APIs. Availability varies by target and runtime capabilities.
- **Game objects, AI, and construction** — object properties and handles, weapon/rig selection, AI task inspection and tuning, selected command replacement, terrain placement checks, and buffered production events.
- **Gameplay and effects** — turbo, shot convergence, ordnance and ballistics, unit VO, damage tuning, visual effects, plants, Betty ammo warnings, shell casings, destruction chunks, and explosion events.
- **Multiplayer** — synchronous/asynchronous object creation, player identity, lives, scoreboard, kill messages, and recycler controls. Local gameplay/physics setters are not automatically synchronized; scripts must coordinate those settings on each peer.
- **Storage and continuity** — `exu.storage` for sandboxed persistent mod state and `exu.continuity` for mission-to-mission object snapshots.
- **Input and utilities** — game-key/menu state, play and sound options, save requests, Steam identities, diagnostic messages, and a monotonic profiling clock (`exu.GetClock`/`exu.GetClockMs`).
- **OpenShim and native integration** — optional OpenShim bridges for shared engine-owned behavior, plus C exports for version checks, Lua-state access, and native callbacks. Unsupported bridges fail closed.

`DrawLine`, `DrawBox`, and `ClearVisuals` are currently no-op placeholders. Features that depend on native hooks or optional providers should be checked with their documented capability probes.

See [`Definitions/ExtraUtils.lua`](Definitions/ExtraUtils.lua) for editor annotations (never `require` this file). `src/luaexport.cpp` and the sub-API installers define what is actually registered at runtime. Further guides live in [`Docs/`](Docs/), including [animation](Docs/ANIMATION_API.md), [first-person APIs](Docs/FPS_API.md), [storage/continuity](Docs/PERSISTENCE_AND_CONTINUITY.md), [plants](Docs/PLANTS.md), and [Betty's ammo warning](Docs/BETTY_AMMO_WARNING.md).

## Using EXU

### Windows

Install EXU through the [Steam Workshop item](https://steamcommunity.com/sharedfiles/filedetails/?id=3144751838), or download `exu.dll` from the [latest release](../../releases/latest) and place it on the game's Lua DLL search path (`package.cpath`). Then load it from a mission script:

```lua
local exu = require("exu")
```

On Windows, `exu.dll` needs the **Microsoft Visual C++ 2015-2022 Redistributable (x86)**, which the game itself does not install. A missing runtime or incorrect `package.cpath` can make `require("exu")` report that the module could not be found. Proton/Wine normally supplies the required runtime DLLs.

Prefer the shared Workshop installation to bundling a private DLL with each mod, and confirm that the mod can actually resolve its location. A shared installation reduces conflicting versions, but does not guarantee a newly developed API is in the published DLL.

See [`examples/`](examples) for focused demonstrations. C++ consumers can include [`include/ExtraUtils.h`](include/ExtraUtils.h) and link against the import library produced by the build.

### Linux (Proton)

The shipped `exu.dll` is a **Win32** Lua C module. Linux hosts deploy that same DLL into a Proton game folder; there is no native Linux `.so`.

Native Steam or Flatpak — paste in a terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/ExtraUtilities/main/scripts/install_linux.sh | bash -s -- --native
```

Snap Steam — paste in a terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/ExtraUtilities/main/scripts/install_linux.sh | bash -s -- --snap
```

Both commands download `exu.dll` from the [latest release](../../releases/latest) and verify it against the `SHA256SUMS.txt` published with that release; a mismatched or unverifiable download installs nothing.

No Steam launch options are required. Proton loads `exu.dll` as a Windows DLL (this is not an OpenShim `winmm.dll` proxy).

To copy a local Windows build instead of a GitHub release:

```bash
./scripts/deploy_linux_proton.sh
```

## Building

Requirements:

- Visual Studio 2022 with **Desktop development with C++** for the Win32 `exu.dll`
- MSVC v143 14.43 or newer
- Python 3 for validation tooling; Linux host checks additionally need a C++17 compiler, Lua 5.1, Node.js, Pillow, and NumPy

The Ogre headers required to compile EXU are committed under `third_party/ogre-1.10.0-bzr/include/`; a normal build does **not** require downloading or cloning Ogre first.

Build `ExtraUtilities.sln` as **Release|x86** on Windows. The project-level target is **Release|Win32** and writes `Release/exu.dll`.

```powershell
msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86
```

Linux can run the host-side validation lane directly:

```bash
bash tests/linux/run.sh
```

Lua 5.1, OgreMain, and OgreOverlay build dependencies required by EXU are included in the repository. If several MSVC toolsets are installed, pass `/p:VCToolsVersion=<version>` to select a recent one explicitly.

The optional `third_party/ogre-1.10.0-bzr/Build-Ogre-BZR.ps1` workflow is only for rebuilding/comparing Battlezone-compatible Ogre binaries; it is not part of a normal EXU build.

## Updating for a game patch

- Revalidate the addresses and signatures documented in `exu.json` against the new executable.
- Update the corresponding declarations in `src/bzr.h` and record the verified game version.
- Run `python tools/qualify_bzr_build.py <path-to-battlezone98redux.exe> --write-report` and review missing/ambiguous targets. Qualify the GOG executable or an unpacked image: the Steam executable's code is SteamStub-encrypted on disk and every anchor reports MISS.
- Regenerate the build profile with `python tools/generate_bzr_build_profile.py`, then confirm with `--check` and `python tools/validate_hardening.py`.
- Build **Release|x86**, run the validation suites, and smoke-test Lua loading plus the affected feature groups in game.
- Update the EXU version in `src/About.h`, `include/ExtraUtils.h`, `Definitions/ExtraUtils.lua`, and `Resource/Resource.rc` together before tagging a release.

## Diagnostics

Once loaded, EXU writes runtime diagnostics to `logs/exu.log` beneath the game directory, falling back to the game directory if it cannot create `logs/`. Logging still requires a writable path; loader failures such as a missing VC++ runtime can occur before EXU starts. Verbose per-call tracing is off by default; set `EXU_DEBUG_LOG=1` in the game's environment (Steam launch options: `EXU_DEBUG_LOG=1 %command%`) when investigating an issue.

## Workshop publication

Workshop-source metadata is kept in `Workshop/`, `workshop_description.txt`, and `workshop_changenote.txt`. Maintainer notes for publishing are in [`Docs/WORKSHOP_RELEASE.md`](Docs/WORKSHOP_RELEASE.md).

## Credits

- **VTrider** — original EXU implementation and much of the native script-extender foundation.
- **GrizzlyOne95** — ongoing maintenance, integrations, rendering features, and stability work.
- **Janne** — original Lua DLL project that helped establish the approach.
- **DivisionByZero** — DLL loader work used by later integrations.
- **Business Lawyer** — bug hunting and technical collaboration.

See [`COPYING`](COPYING) and [`COPYING.LESSER`](COPYING.LESSER) for license terms.

### AI-Assisted Development

AI tools, including Claude and Codex, assist with analysis, code, tests, and documentation. The maintainer reviews and integrates their output; reverse-engineering and compatibility claims require evidence from game binaries, traces, or targeted tests. AI-generated analysis alone is not treated as proof.
