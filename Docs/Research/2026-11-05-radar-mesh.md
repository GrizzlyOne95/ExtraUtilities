# BZ2 radar mesh in the native radar pass

`exu.SetRadarMesh(enabled, material, vertices)` draws a textured triangle list
through Redux's own HUD batch, inside the radar pass, and hides the native
compass letters. ISDFC uses it with BZ2's `ihrad00` mesh, whose texture has the
N, S, E and W letters baked in, so the letters rotate with the radar.

Paths below: *Redux* is `Battlezone_Source/BZ1/Redux/openshim_re_corpus/bzr_gog_best_effort/ghidrecomp/results/bins/battlezone98redux.exe-6777ca/decomps/`;
*BZ2* is `Battlezone_Source/BZ2/_analysis/global_decompile/bzone_b131p_best_effort/results/bins/bzone.exe-6f4367/decomps/`.

## How BZ2 draws it

| Fact | Evidence |
| --- | --- |
| Mesh `ihrad00.xsi` on a dedicated camera: FOV 0.15708, near 6, far 10 | BZ2 `Init-00529d51.c`, ~65-83; constants read from `b131p/bzone.exe` at 0x6a1630, 0x6e974c, 0x6e9790 |
| View tilted by `RADAR_SIN`/`RADAR_COS`, seeded with 0x3f060a92 = 0.5236 rad = 30 degrees | BZ2 `` `dynamic_initializer_for_'RADAR_SIN''-00415dbc.c `` line 7; `Render-00528792.c:81-87` |
| Camera follows the player's flattened forward vector, 8 units back | `Render-00528792.c:65-79`, `:88-90` |
| Mesh scale = `Play_CockpitRadarXSIScale / radarRange`, 0.75 in retail | BZ2 `Control-00528d40.c:73`; `bz2r_res/config/game/bzgame_init.cfg:663` |

So one radar range is 0.75 mesh units and the camera sits 8 units from the mesh.
In radar ranges that is `8 / 0.75 = 10.667`. EXU's presentation hook takes this
as its distance, and ISDFC passes tilt 30, distance 10.667 when the mesh is on.

## How Redux lets us draw it

| Fact | Evidence |
| --- | --- |
| Radar order: backdrop, then terrain | Redux `FUN_00493ec0-00493ec0.c:247-248` |
| Compass letters drawn by `FUN_0068c560` from axis points | `FUN_00493ec0-00493ec0.c:262-279`; names in `FUN_004930e0-004930e0.c:24-27` |
| Textured path: `FUN_00684ce0(0)`, `FUN_00686590(record,depth)`, `FUN_00686600(0,verts,4,colour)`, `FUN_006856e0()` | `FUN_0068c560` decomp, ~110-135 |
| Fan indices, so a triangle is a three-vertex fan | `FUN_00686600-00686600.c:56-60` |
| A record's first 8 bytes are its `SharedPtr<Material>` | `FUN_0068ba00` decomp, ~100-110 |
| Native projection is `f = (radius*5) / (range*5 + p[2])` | `FUN_00493330` decomp |

While the mesh draws, the loop's exit test at 0x49444C (`0F 83 8B 00 00 00`, JAE to
0x4944DD) becomes an unconditional jump, so all four letters skip. It is restored on
any frame without the mesh. Nothing after the loop reads its results: the loop's
counter and screen points are only used inside it (`FUN_00493ec0-00493ec0.c:275-279`,
and `FUN_00493330` only returns values). The sprite call at 0x4944D0 cannot be patched
instead, because OpenShim's HUD fade already redirects it
(`BZR-OpenShim/src/patches/contextual_hud.cpp:173-179,205,223`) and EXU's expected-bytes
check then refused it in a live run. With the loop skipped, the shim's fade and its
`RadarCompassAlwaysVisible` setting have no effect on the letters.

## Mesh data

`d01m029.mesh` in ISDFC is the retail radar: one submesh, 288 vertices, 96
triangles, radii {0, 1.0, 1.11}. A top-down render of it shows the baked letters
N at +Z, S at -Z, E at -X and W at +X. Redux's radar has east at +X, so
`tools/radar_mesh.py` negates X and divides positions by 0.75. Negating X flips
the winding, which is why the material culls nothing.

## Not verified

- UNVERIFIED: that the baked letters turn correctly with the player. The test
  captures hold still. N-E-S-W read clockwise and unmirrored, and the mesh uses the
  matrix the terrain does (`DrawMeshTriangles`).
- UNVERIFIED: why black, below. Only the before and after captures show it.

## Capture findings

- `EXU_HudTint` draws correctly under the native batch (captures `ms06c`, `ms06d`).
- `d01_ihrad00.dds` has no mip levels. With the default mip request the lip drew pure
  black while the disc drew correctly, and `texture d01_ihrad00.dds 2d 0` in
  `ISDFC/RadarMesh` fixed it. The likely cause is an empty mip chain sampled where
  the lip's UVs are minified. The disc's fan has one constant UV.
- Stock radius and centre put the 1.48-range lip off the left and bottom of the
  screen. `FitProjection` shrinks the radius global before the mesh draws, so terrain
  and blips follow it. The layout builder rewrites that global each frame before the
  terrain draw (`FUN_00493ec0-00493ec0.c:246,248`, `FUN_00492ec0-00492ec0.c:14-16`), so
  nothing accumulates and turning the mesh off restores stock on the next frame.
  OpenShim's layout detour derives its radius from the stock base and UI scale, not
  from this global (`src/patches/radar_layout.cpp:144,164`), so EXU's value wins.
