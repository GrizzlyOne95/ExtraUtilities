# ISDFC constructor placement hologram

Implemented on `agent/isdfc-construction-hologram`, retaining the deployed
surface-raycast, radar and scrap work. No PR or public release is prepared;
native gameplay/visual acceptance remains pending. This pass is offline only.

## Proven reference behavior

BZ2 corpus `bz2-bzone-b131p-best-effort` (b131p, not Redux):

- `ConstructionRig::GetCommand`, `GetCommand-004d38c7.c`, SHA256
  `e841cf3d28e82b34b4340bdc0ad7fa085d6ba1277fedcf67ca892e32388e131e`:
  submits the chosen building geometry with its proposed construction matrix;
  sets green `0xff00ff00`, yellow `0xffffff00` or red `0xffff0000` from placement.
- `ConstructionDisplay::SetData`, `SetData-0059b214.c`, SHA256
  `cb6c2287a2d4b8700cbc38e6fc569501638cb488e9e4a2572aaf952a6f8fa238`:
  collects actual mesh vertices, triangles and a wire index list. Vertex height
  gets a 0.01 offset. This is world geometry rather than a small HUD icon.
- `ConstructionDisplay::Submit`, `Submit-0059ad71.c`, SHA256
  `c15ade57a98818d5d24ed64c00f786d89ee7c1f06647433552a48bf27518e39d`:
  uses the building matrix and active camera; default blink type 2 alternates
  on `(Vid::s_LastFrameRenderTime >> 9) & 1`, i.e. 512 ms phases. The wire
  vertices receive alpha 127, with faces also submitted in the solid phase.
- `ConstructionDisplay::SetColor`, `SetColor-0059b6c0.c`, SHA256
  `1aa02e049862008854c0d49c8aea49a2ee46cdfd4c029a501fd3d75c6b652540`.
  Assembly at 0x0059B6C0 resolves heavy alpha to byte 0x00708D8F (default 160),
  light alpha to 0x007091B0 (default 31). Blink type dword 0x00708D88 defaults
  to 2. Submit's wire alpha is explicitly 127; wire-only mode has no face pass.

These are source/decompiler/assembly observations, not a transferable BZ2 ABI.
The SetData decompiler parameter labels are misleading; no BZ2 function is
called from Redux and no original renderer structs are copied into EXU.

## Qualified Redux path

GOG Redux 2.2.301 executable SHA256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.

- 0x0049D230 is the rig placement GetCommand method: ECX rig, one stack
  position-pointer argument, RET 4 at 0x0049D54B. Its first six bytes are
  `55 8B EC 83 EC 54`, three complete relocation-free instructions.
- The original method calls SetConstructionMatrix at 0x0049CF90, then uses
  rig +0x370 for the selected class and +0x378 for the 64-byte MAT_3D. The
  matrix has float axes followed by double XYZ at +0x28. Reuse avoids an
  independently computed aiming ray or an approximate build position.
- The stock ConstructionDisplay lives at 0x009786D8, loaded by code at
  0x0049D335. Its active sprite is +0x68, green/yellow/red resources are
  +0x6C/+0x70/+0x74. Initialization 0x0049C390 loads `ground.G/Y/R` and
  status setters copy the matching resource to +0x68. This preserves Redux's
  own validity/power feedback; no new affordability or prerequisite logic.
- Selection/matrix and display-reference patterns are optional feature anchors
  alongside the GetCommand entry in exu.json and the build profile.
- Exact GOG decompiler identities: `FUN_0049d230-0049d230.c` SHA256
  `4e69ebe62ec8f1d2f9d5a0ea69596f1519a1bffc8552b87907c246aaf943fe21`;
  `FUN_0049cf90-0049cf90.c` SHA256
  `94648572bc84370cdf3165930f5a8869b9bc55af280c0b7bee25d8158f7bb4b8`.
  Relevant offsets, branches, original calls and RET were checked in the
  released PE, rather than accepting decompiler calling conventions.

## Implementation and lifetime

`SetConstructionHologramEnabled(enabled, meshes, materialPrefix)` is opt-in.
EXU installs one checked reversible entry detour and calls the original method
first. It preserves the return value and all native gameplay effects. Only a
live arena rig, active placement result and recognized native status produces
a snapshot (handle, decoded ODF, copied matrix/pose and status).

An Ogre frame listener consumes a fresh snapshot before rendering. Missing
updates, cancellation/confirmation, a dead handle or disabled/hidden HUD detach
the display entity. The ordinary native ground footprint always remains.
The listener never creates/removes objects during a render-queue traversal.
Mesh changes recreate only the preview entity; pose changes reuse its node.
The native-selected position includes terrain height; EXU applies the same
0.01 vertical offset and the documented OgreRenderSpace origin/Z conversion.

The display is not a GameObject: it has no AI, collision, arena handle,
team-material assignment, scrap cost or build reservation. Picking flags are
removed and shadows disabled. No camera/controls setters are involved.

The new bridge verifies 18 required exports against installed OgreMain.dll.
That DLL does not export the inline `setPosition`, `setOrientation`, or
`setVisible` methods assumed by some older helpers. This implementation uses
exported `_setDerivedPosition`/`_setDerivedOrientation`, and attaches/detaches
its own entity for visibility. It does not invoke those unsupported helpers.
Owned nodes are qualified against the current scene and their named parent;
stale pointers are forgotten when that scene disappears. Lua closure disables
the hook, removes the listener and destroys owned scene objects before EXU
unloads. A pinned DLL retains the inactive trampoline for its next Lua state.

Campaign `isdfcholo.lua` supplies actual mesh names from constructor build
items and toggles visibility through shared statusring.Update. The generator
uses physical ODF contents: baseName is a geometry name, not file inheritance.
All 68 constructor menu entries resolve: 34 build ODFs, 32 unique meshes,
including ISDF, orange variants and other available constructor menus.

Campaign materials use the BZ2 RGB palette, face alpha 160 and wire alpha 127.
They alternate wire-only / faces-plus-wire in 512 ms phases. Separate SM4,
SM3, GLSL and fixed-function techniques keep renderer requirements explicit.
Unlike BZ2's custom edge index builder, this first adaptation uses Ogre polygon
wireframe over the current port meshes. Dense remastered meshes can therefore
look busier; this is not a claim of identical rasterization or topology.

## Validation and limits

Release x86 compilation, all four HLSL shader profiles, 19 Lua lifecycle/fallback
checks, constructor mesh/index/finite-position checks, exact PE preimages,
stolen-instruction/RET checks and all 18 actual Ogre exports were validated.
Host C++ cases cover native status mapping, terrain offset, positive/negative
yaw, render origin/Z reflection, non-finite/invalid matrices and blink bounds.
The broader Linux host suite is recorded in the installation artifact folder.

Deployment/evidence/rollback artifacts are under the GOG game's
`output/isdfc-construction-hologram-20261104`. Its preview image is an offline
projection of real campaign mesh triangles, not a screenshot or runtime proof.
The installed DLL also retains prior radar, scrap suppression and surface-raycast
APIs. No game was launched, in line with the user's offline-check preference.

Unverified: actual renderer/input frame ordering, visible positioning/material
appearance, cancellation/confirmation timing, building skeleton bind poses,
normal-resolution gameplay and save/load transitions in a native mission.
Steam, DX9/OpenGL and Proton/Wine runtime acceptance also remains unverified.
Restart the mission/game before testing the new DLL and shared controller.
