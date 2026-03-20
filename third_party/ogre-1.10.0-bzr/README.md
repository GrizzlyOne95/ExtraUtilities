# Ogre 1.10.0 BZR Patch Project

This folder tracks the local Ogre work needed to build Battlezone Redux-compatible `v120` render-system DLLs on another machine.

## What is saved here

- `patches/0001-suppress-cross-renderer-hlsl-target-warnings.patch`
  - suppresses false HLSL target warnings when Ogre parses the inactive renderer's optional shader delegates
  - fixes the mislabeled D3D9 log string so it reports `D3D9 shader` instead of `D3D11 shader`
- `Build-Ogre-BZR.ps1`
  - applies the patch to a clean Ogre 1.10.0 source tree
  - downloads and builds `zlib` and `freetype`
  - can fetch `zziplib` and the official Win32 FreeImage package for the Battlezone ABI profile
  - configures Ogre for Win32
  - builds either D3D11 or D3D9 using the `v120` toolset
- `Compare-Ogre-ABI.ps1`
  - compares a rebuilt `OgreMain.dll` against the shipped game install
  - normalizes `dumpbin` import-thunk aliases so `RelWithDebInfo` exports compare sensibly
  - reports export drift and which game DLLs import symbols the rebuilt core does not provide
- `ABI_NOTES.md`
  - captures the current ABI findings from the first Battlezone-versus-stock Ogre comparison

## Requirements

- Visual Studio 2013 toolset (`v120`)
- Visual Studio 2022 generator support
- `cmake.exe`
- `git.exe`
- Ogre 1.10.0 source tree
- For D3D9: DirectX SDK (June 2010)

## Quick Start

Build D3D11:

```powershell
& .\third_party\ogre-1.10.0-bzr\Build-Ogre-BZR.ps1 `
  -OgreSourceDir C:\path\to\ogre-1.10.0 `
  -Toolset v120 `
  -RenderSystem D3D11 `
  -BuildRelease
```

Build D3D9:

```powershell
& .\third_party\ogre-1.10.0-bzr\Build-Ogre-BZR.ps1 `
  -OgreSourceDir C:\path\to\ogre-1.10.0 `
  -Toolset v120 `
  -RenderSystem D3D9 `
  -BuildRelease
```

Build a Battlezone-oriented ABI profile:

```powershell
& .\third_party\ogre-1.10.0-bzr\Build-Ogre-BZR.ps1 `
  -OgreSourceDir C:\path\to\ogre-1.10.0 `
  -Toolset v120 `
  -RenderSystem D3D9 `
  -BattlezoneAbiProfile `
  -BuildRelease
```

The script writes all downloaded dependencies and generated build trees under `third_party\ogre-1.10.0-bzr\_work\`.

## Notes

- The D3D9 build depends on `DXSDK_DIR`, but the script will automatically fall back to `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)` when present.
- The tested local path used the VS2022 generator with `-T v120`, which produced `MSVCR120` / `MSVCP120` outputs.
- `-BattlezoneAbiProfile` turns on custom container allocators, keeps the stock string allocator, enables ZIP support, disables `STBI`, and auto-fetches the official `FreeImage3180Win32Win64.zip` package unless `-DisableFreeImage` or `-FreeImageRoot` is supplied.
- When FreeImage is enabled, the helper also strips upstream Ogre's `FREEIMAGE_LIB` define so the build matches the Win32 DLL import library calling convention used by the official package.
- Do not replace only `RenderSystem_Direct3D9.dll` or `RenderSystem_Direct3D11.dll` in the live game install. The rebuilt render systems were not drop-in compatible with the shipped `OgreMain.dll`. Treat Ogre as a matched set when deploying.
- The current ABI comparison shows that the shipped game is not using an ABI-identical stock Ogre 1.10.0 build. In addition to allocator and feature-setting drift, some exported function signatures differ from upstream stock source.

## ABI Diff

Compare a rebuilt core against the live game install:

```powershell
& .\third_party\ogre-1.10.0-bzr\Compare-Ogre-ABI.ps1 `
  -GameDir "C:\Program Files (x86)\Steam\steamapps\common\Battlezone 98 Redux" `
  -RebuiltOgreMain C:\path\to\rebuilt\OgreMain.dll
```

See `ABI_NOTES.md` for the current known Battlezone-specific ABI gaps.

## Verified local outputs

- D3D11:
  - `OgreMain.dll`
  - `RenderSystem_Direct3D11.dll`
- D3D9:
  - `OgreMain.dll`
  - `RenderSystem_Direct3D9.dll`
