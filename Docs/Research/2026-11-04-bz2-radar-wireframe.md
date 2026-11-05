# BZ2 radar detail adaptation for ISDFC

This extends the mission-scoped radar presentation with `SetRadarTerrain`.
The implementation belongs in EXU: the existing native line renderer supplies
the necessary drawing operations, so a second DLL would duplicate loading,
supported-build gating and mission cleanup without adding a needed capability.

## Proven reference behavior

Reference corpus: `bz2-bzone-b131p-best-effort` in Battlezone_Source, root
`BZ2/_analysis/global_decompile/bzone_b131p_best_effort/results/bins/bzone.exe-6f4367/decomps`.
These are decompilation references, not buildable portable BZ2 engine sources.

- `RenderLines-00528158.c`, SHA256
  `58a4932641f65b856a13b5566c276c06e10993de03cdd0ad3d3a33c3f32deb8b`:
  lines 140-192 establish alpha `64 + pingFrame * 112` and the cluster range;
  lines 230-350 sample terrain, keep nodes inside the range circle and connect
  adjacent nodes in both directions. Default terrain grid size is 8 metres
  (`Load-005d8176.c`), and a cluster contains four grids, hence 32 metres.
- `Render-00528792.c`, SHA256
  `7ef27989729a9b5cc5a0b6816fd94f38cac2aaff7a7f3bdfdf999396b29e3176`:
  camera tilt 30 degrees, distance 8, and a scan pulse drawn through
  `Graphic_Circle` with 32 segments, white RGB, alpha `pingFrame * 255` and
  radius `(1-pingFrame)*20`.
- The pulse is a **screen-space circle**, not an expanding terrain-range ring.
  b131p assembly 0x00528C24..0x00528C9B computes the radar viewport center into
  XMM2/XMM3, passes 32 in EDX, color/radius on the stack, then calls 0x005E855B.
  The decompiler's apparent four-argument signature omits those SSE arguments.
- Default RGB values are R=0, G=120, B=255: preference creation at 0x004B1C83,
  0x004B1C9A and 0x004B1CB1 reads 0x0072BA92 (zero-initialized), 0x007091B2
  (`78`) and 0x007091B1 (`FF`) respectively. These are visual defaults only.
- `RenderHeight-00527598.c`, SHA256
  `6926ac2e06dc000c732703bfb045abb1d12f8f47f0464259fdb2f57c25fb0f09`:
  terrain-to-object height stem, white RGB and caller alpha retained.

## Redux integration and boundary

Qualified GOG Redux 2.2.301 executable SHA256:
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
Native knowledge remains in exu.json; generated addresses are used by feature
code. No BZ2 memory layout, DX9 camera, vertex buffer or object pointer enters
the Redux implementation.

Two additional checked radar-only CALLs activate atomically:

- 0x00494074, original destination 0x00493E60: terrain drawing after the native
  scanner and radar matrix update. `DrawTerrain(void*,void*)` adapts the no-stack-
  argument thiscall using a fastcall wrapper. Original native terrain is the
  runtime failure fallback. It remains available with its existing inner hooks.
- 0x00495C03, original destination 0x0068AF40: a seven-stack-argument cdecl line
  draw for height stems, after native contact eligibility and range filtering.
  The original renderer already queries the ground and computes both endpoints;
  the wrapper replaces RGB with white and preserves alpha and mode.

Native 0x00687340 submits integer XY strips (three cdecl arguments). Native
0x0068AF40 draws viewport/x1/y1/x2/y2/color/mode (seven arguments, caller pops
0x1C). Both are independently guarded; the latter's seventh mode argument must
not be omitted based on misleading decompiler call formatting.

The terrain layer uses the native radar frame's player, position, range and
matrix. Grid samples are anchored at world multiples of 32 m, clipped to the
current range circle, and connected in rows and columns without bridging invalid
height samples. Heights refresh every 100 simulation milliseconds; projection,
range clipping and pulse alpha refresh every draw. Unusually large scanner
ranges coarsen the grid to stay within 65x65 samples. Empty/failed detail drawing
falls back to the original terrain. The stock temporary depth layer is restored.

The native scanner pulse (14, decaying by dt*20) triggers a one-simulation-second
BZ2 display pulse. Simulation time, rather than wall time, preserves pausing.
Player changes and clock reversals clear pulse/cache ownership. Closing the Lua
state disables both hooks explicitly, and the existing BasicPatch cleanup also
restores their original bytes. Disabling radar presentation disables detail.

Native contact discovery, symbols, objective/selection behavior and scanner
rules remain Redux behavior. This adapts the BZ2 terrain/pulse/height drawing;
it does not claim a complete binary backport or identical contact glyphs.

## Validation

Release x86 build, Windows x86 and Linux terrain geometry/pulse tests, Lua 5.1
lifecycle/fallback tests, definition syntax and address/API hardening passed.
The normal isdfms01 native run logged 275 terrain samples, 37 strips at 32 m,
and the native-triggered 32-segment scan ring. A live read confirmed all 22
presentation/detail CALL/operand sites active. The height-stem hook activated,
but a visible height-stem draw was not observed in this starting scene.

Computer use failed with CreateForMonitor 0x80070057, then GetCursorPos access
denied 0x80070005; visual acceptance remains pending. No gameplay camera commands
or test mission/controller injection were used.

The complete Linux suite subsequently passed, including all 46 C++ host tests.
The final DLL was loaded in a second normal isdfms01 run and again logged the
terrain mesh and scan ring. The user requested finishing with offline checks;
both owned tests were closed gracefully and Ogre.cfg restored byte for byte.
