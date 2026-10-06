# Path-grid footprint from collision faces (`exu.pathing`)

The stock AI path grid blocks the whole oriented SDF bounding box of every building. Tunnels, arcades and walk-in rooms are therefore solid to the planner, although vehicles and pilots fit through them. Local steering (`FindPotentialField`) ignores building classes 2 and 10, so the grid is the only obstacle the planner sees.

An ODF can opt out of the box. EXU then blocks only the cells whose centre is inside solid geometry. OpenShim implements the same contract natively; with both present the grid is identical, and running both (or either twice) is harmless.

## ODF keys

```ini
[GameObjectClass]
pathBlock = "faces"        ; "box" (default, stock) | "faces" | "none"; anything else is "box"
pathBlockHeight = 1.5      ; metres above the object's origin of the first cross-section
pathBlockHeight2 = 3.0     ; second cross-section; a cell is solid if solid at EITHER height
```

Keys are read through the engine's ParameterDB, as Lua `GetODFString` reads them, and are case-insensitive. Values may be quoted.

## Rules

- **`"none"`:** the object blocks no cells.
- **`"faces"`:** a cell is blocked only if its centre is strictly inside the object's oriented bbox footprint and inside solid geometry at either sample height. Cells outside the bbox are untouched.
- **Inside test:** nonzero winding.
  - Cast a ray in root-local +X from (cell centre, sample height).
  - Count +1 for each triangle whose normal `(b-a)x(c-a)` points against the ray, and -1 for each one pointing along it.
  - The point is solid when the sum is nonzero.
  - Parts are closed solids that may overlap, so even-odd parity would be wrong.
  - Each triangle is projected on root-local (y, z) and hit-tested with edge functions. On an edge, the triangle with `dv > 0 || (dv == 0 && du < 0)` owns it, where `(du, dv)` is the edge vector in (y, z) of the counter-clockwise-normalised triangle. A ray through a shared edge or vertex therefore counts exactly once.
  - Edge-on triangles never count. A hit counts only when its x is strictly greater than the start.
- **Geometry:** the LOD0 GEO faces of the SDF hierarchy (`GeoCache_SelectLOD(obj, 0)`, skipping objects with OBJ76 flag bit 0, as `get_obj_bounding_box` does), fan-triangulated, in the root's local frame. The root's own matrix is the identity.
- **Placement:** the root world matrix exactly as `BlockCells` uses it. World (x, z) = `right.xz * lx + front.xz * lz + posit.xz`, and the up row is ignored. A cell centre maps back to root-local (lx, lz) through the inverse of that 2x2. The sample height is root-local y.
- **Grid:**
  - Cell size is `Terrain.Grid_Size` at 0x02CC50E0, read at runtime: 5.0 m, stored by terrain init 0x0077E990.
  - Cell (gx, gz) = (`floor(x * Grid_Scale)`, `floor(z * Grid_Scale)`), with Grid_Scale = 1 / size at 0x02CC50E4. The byte index is `(gx - MinX) + (MaxX - MinX) * (gz - MinZ)`, with MinX/MaxX at 0x02CE99C0/0x02CE99A0 and MinZ/MaxZ at 0x02CD9984/0x02CE99C4. The cell centre is `(g + 0.5) * size`.
  - Cell byte bits: 0/1 slope/steep (3 = cliff), 2 lava, 3 perimeter. A building ORs in `0x0B`.

## Idempotence

Every edit recomputes the affected cells from scratch, so one, both, or repeated passes give the same grid:

1. Clear `0x0B`.
2. Re-derive the terrain bits as `ProcessCliffs` does: cliff 3, else steep 2, else slope 1. Lava and the high bits are never touched.
3. Re-add the perimeter bit 3 for cells in any `perimeterArea` rectangle that are not cliffs.
4. OR in `0x0B` for every other blocker in `buildingArea` that covers the cell:
   - unflagged objects use the stock `BuildingBlock` test (the oriented box grown by half a cell, with rounded corners);
   - flagged objects use their face mask.

The region recomputed is the object's stored world AABB rectangle, the same rectangle `UpdateCells` visits. `InvalidateStrips` is called over it afterwards.

## Coexistence with OpenShim (PR #407)

OpenShim detours the **entry** of `BlockCells` (0x00468A70) from process start, so it sees every add and remove of every map. In `luaopen_exu`, EXU reads that entry:

- **Stock prologue `55 8B EC 83 EC 64`:** EXU installs its eight call-site redirects and runs its init refresh. `GetCapabilities().owner == "exu"`.
- **Anything else (OpenShim's `jmp` into winmm.dll, or another patch):** EXU stands down. It installs no redirects, and `Refresh`/`SetEnabled` recompute nothing. `owner` names the detour's module and `standDown == true`. `GetMode` and `DumpGrid` still work, read-only.

A map's grid is therefore written by exactly one implementation, whichever load order. There is no second recompute, so terrain bits are never unblocked twice and no stale pre-call bytes are applied.

Remaining differences between the two implementations, which matter only when comparing EXU-only grids with OpenShim-only grids:

| Point | EXU | OpenShim |
| --- | --- | --- |
| Opened cell | re-derives the terrain bits as `ProcessCliffs` does (cliff 3) and re-adds perimeter bit 3 | restores the byte from before the stock call, or uses `BuildingUnblock` (steep/slope only) for a cell already blocked |
| Faces | the engine's loaded LOD0 GEOs | `<baseName>.sdf`, GEOs whose 4th name character is `1` |
| Tie rule | top-left edge ownership | Sunday half-open rule |

Both use vertex-order normals, never the stored GEO "plane" values (not normals), and the same sign. Inside an outward part the count is -1. Points away from triangle edges get the same answer from both.

## Timing and removal

- **Hooks:** EXU redirects all eight `call BlockCells` sites to a stub (addresses in `exu.json` group `PathBlock`):
  - `ProcessBuildings` (map PostLoad);
  - `AiUtilFeature::AddObject`;
  - `AiUtilFeature::DeleteObject`;
  - five deployable-building sites.
- **Stub:** calls the stock function first. If the object, or any flagged object overlapping its rectangle, is flagged, it then recomputes that rectangle.
  - On removal the rectangle comes from `buildingArea` before the stock call erases it.
  - Stock unblocking clears the whole rectangle, including other buildings' bits. The recompute restores them.
- **Load order:** EXU is loaded by mission Lua, normally after `ProcessBuildings`. `luaopen_exu` therefore runs a refresh pass over every flagged object already in `buildingArea`. When EXU loads before the grid exists, the `ProcessBuildings` site goes through the stub instead.
- **`exu.pathing.Refresh()`** repeats the pass at any time.
- **Unload:** the patches are removed when the Lua state closes. The grid keeps its last state until the map unloads.

## Lua

| Function | Result |
| --- | --- |
| `exu.pathing.Refresh()` | flagged objects re-applied, or nil without a grid |
| `exu.pathing.SetEnabled(bool)` | `false` puts flagged objects back on the stock box now |
| `exu.pathing.IsEnabled()` | boolean |
| `exu.pathing.GetMode(h)` | `"box"`/`"faces"`/`"none"` and `{ odf, configured, effective, cellsInBox, cellsBlocked, triangles, inGrid }` |
| `exu.pathing.DumpGrid(pos or h, radius)` | ASCII grid: `#` blocked, `c`/`s`/`/` cliff/steep/slope, `p` perimeter, `O` centre |
| `exu.pathing.GetCapabilities()` | `{ pathBlock, available, hookedSites, hookSites, gridReady, cellSize, enabled }` |

`exu.log` gets one line per flagged object: `odf=… mode=… cells_in_bbox=… cells_blocked=… tris=…`.

## Offline check

`tools/pathblock_dryrun.py <folder> <name> --cell 5 --yaw 0,30,45 --offset 0,0 --offset 2.5,1` loads an SDF and its GEOs and prints the mask the native code writes. It mirrors `src/Game/PathBlockMath.h`; a cross-check on `bbsubtun` agreed on all 5681 sample points. With the real 5 m cells (`--cell 5`), the 16 m `bbsubtun` tunnel has a 4-connected open corridor at all 285 placements tested: yaw 0 to 90 degrees in 5-degree steps, x offsets 0 to 4 m, z offsets 0 to 3 m. The deck either side is blocked; its battered outer 2 m and the side control room are open. A corridor wider than 5√2 ≈ 7.1 m always contains one. The same check at 10 m passed 475/475.
