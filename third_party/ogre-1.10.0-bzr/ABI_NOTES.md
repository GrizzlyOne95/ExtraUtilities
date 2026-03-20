# Ogre ABI Notes

These notes capture the ABI comparison work between the shipped Battlezone Redux Ogre DLLs and locally rebuilt `v120` Ogre 1.10.0 variants.

## Current result

The stock local Ogre source tree is not a drop-in ABI match for the shipped game binaries, but the Battlezone-oriented ABI profile gets much closer than the original stock-style rebuild.

The first live swap failed with this popup when only the rebuilt D3D9 render system was deployed:

```text
The procedure entry point ??0GpuProgram@Ogre@@QAE@ABV01@@Z could not be located in the dynamic link library RenderSystem_Direct3D9.dll
```

That symbol is imported by the rebuilt `RenderSystem_Direct3D9.dll`, but it is not exported by the shipped `OgreMain.dll`.

## Baseline stock-style rebuild

- Shipped `OgreMain.dll` exports: `9194`
- Rebuilt stock-source `OgreMain.dll` exports: `9100`
- Symbols only in rebuilt core: `416`
- Symbols only in shipped core: `510`

The game executable and multiple Ogre-side DLLs also import symbols that are missing from the rebuilt stock-source `OgreMain.dll`.

Representative gaps:

- `battlezone98redux.exe` misses `24` imports against the rebuilt core
- shipped `RenderSystem_Direct3D9.dll` misses `38`
- shipped `RenderSystem_Direct3D11.dll` misses `36`
- `OgreOverlay.dll` misses `25`

## Battlezone ABI profile rebuild

This rebuild used:

```text
OGRE_CONFIG_CONTAINERS_USE_CUSTOM_ALLOCATOR=TRUE
OGRE_CONFIG_STRING_USE_CUSTOM_ALLOCATOR=FALSE
OGRE_CONFIG_ENABLE_ZIP=TRUE
OGRE_CONFIG_ENABLE_FREEIMAGE=TRUE
OGRE_CONFIG_ENABLE_STBI=FALSE
```

It also uses `zziplib` and the official Win32 `FreeImage` package, and strips Ogre's upstream `FREEIMAGE_LIB` define so `FreeImage.h` matches the DLL import library calling convention.

Measured results:

- Shipped `OgreMain.dll` exports: `9194`
- Rebuilt ABI-profile `OgreMain.dll` exports: `9147`
- Symbols only in rebuilt core: `84`
- Symbols only in shipped core: `131`

Representative remaining import gaps:

- `battlezone98redux.exe` misses `13` imports against the rebuilt core
- shipped `RenderSystem_Direct3D9.dll` misses `25`
- shipped `RenderSystem_Direct3D11.dll` misses `23`
- `OgreOverlay.dll` misses `20`

Representative remaining gaps:

- shipped exports ctor/dtor symbols for `AllocatedObject<CategorisedAllocPolicy<...>>` across several allocator categories that the rebuild still does not provide
- shipped does not export the `GpuProgram` copy constructor, while the rebuild still does
- shipped still differs in a few class ctor/dtor surfaces such as `ConfigDialog` and `Particle`

`RelWithDebInfo` was also tested because the shipped DLLs carry `RelWithDebInfo` PDB paths. It was not a better ABI match than the ABI-profile `Release` build. After normalizing import-thunk aliases in the comparison script, the `RelWithDebInfo` build still widened the gap substantially, so the current best local match remains the ABI-profile `Release` configuration.

## Root causes seen so far

### 1. Allocator configuration mismatch

The rebuilt stock-source tree was configured with:

```text
OGRE_CONFIG_CONTAINERS_USE_CUSTOM_ALLOCATOR=OFF
OGRE_CONFIG_STRING_USE_CUSTOM_ALLOCATOR=OFF
```

But the shipped binaries clearly expect exported signatures that use OGRE's custom STL allocator types, such as:

- `STLAllocator<..., CategorisedAllocPolicy<...>>`
- `AllocatedObject<CategorisedAllocPolicy<...>>`

This alone changes many decorated symbol names and explains a large part of the import/export drift.

### 2. Feature set mismatch

The original stock-style rebuild also had:

```text
OGRE_CONFIG_ENABLE_FREEIMAGE=FALSE
OGRE_CONFIG_ENABLE_ZIP=FALSE
```

The shipped `OgreMain.dll` exports symbols for both feature areas, including:

- `EmbeddedZipArchiveFactory`
- `FreeImageCodec`

Those exports are missing from the rebuilt core.

### 3. API-level signature drift from stock Ogre 1.10.0

Some shipped exports do not just differ by feature toggles. They use different decorated function signatures than the stock `ogre-1.10.0` source tree.

Examples:

- Shipped `Image::getNumMipmaps` exports as `...QBEEXZ`
- Stock-source rebuild exports `Image::getNumMipmaps` as `...QBEIXZ`
- Shipped `Texture::setNumMipmaps` exports as `...UAEXE@Z`
- Stock-source rebuild exports `Texture::setNumMipmaps` as `...UAEXI@Z`
- Shipped `Image::loadDynamicImage` exports `..._NIE@Z`
- Stock-source rebuild exports `..._NII@Z`

This indicates the retail game is very likely using a Battlezone-specific Ogre branch, or at least an Ogre patch set beyond the stock upstream `1.10.0` tree.

### 4. Upstream FreeImage build integration mismatch

Upstream `OgreMain/CMakeLists.txt` unconditionally adds:

```text
FREEIMAGE_LIB
```

when `OGRE_CONFIG_ENABLE_FREEIMAGE=TRUE`. With the official Win32 FreeImage package, that macro is wrong: it makes `FreeImage.h` declare static-library style symbols instead of the DLL import `__stdcall` signatures exported by `FreeImage.lib`.

The ABI-profile rebuild had to strip that define before the FreeImage-enabled build would link successfully.

## Practical takeaway

Treat Ogre as a matched set, but do not assume a stock-source matched set is safe to deploy.

Before another live swap, we need at least one of:

- a closer source tree for the shipped Battlezone Ogre fork
- the missing Battlezone Ogre patch set
- a rebuild configuration that reproduces the shipped allocator and feature exports closely enough to eliminate the current import gaps

## Reproduce the comparison

Use:

```powershell
& .\third_party\ogre-1.10.0-bzr\Compare-Ogre-ABI.ps1 `
  -GameDir "C:\Program Files (x86)\Steam\steamapps\common\Battlezone 98 Redux" `
  -RebuiltOgreMain "C:\path\to\rebuilt\OgreMain.dll"
```
