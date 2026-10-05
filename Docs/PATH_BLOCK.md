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
  - Cell size is `Terrain.Grid_Size`, read at runtime (10 m on stock maps).
  - World x maps to cell `floor(x * Grid_Scale) - GridMinX`, clamped. Its centre is `(absoluteIndex + 0.5) * Grid_Size`.
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

`tools/pathblock_dryrun.py <folder> <name> --yaw 0,30,45 --offset 0,0 --offset 5,5` loads an SDF and its GEOs and prints the mask the native code writes. It mirrors `src/Game/PathBlockMath.h`; a cross-check on `bbsubtun` agreed on all 5681 sample points. For the 16 m `bbsubtun` tunnel with 10 m cells, a 4-connected open corridor exists at every yaw from 0 to 90 degrees in 5-degree steps and every offset from 0 to 8 m in 2 m steps. A corridor wider than 10√2 ≈ 14.1 m always contains one.
